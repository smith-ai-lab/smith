// [[smith::affine]]: a value of a type marked with it is used at most once (issue #189, skill smith-lang-affine).
// The same plugin narrows payload enums (issue #198, below: "Narrowing of a payload enum").
//
// Linked into smith and run on every compile, with no flag, by two entries of clang's static registries:
//   - a ParsedAttrInfo makes `[[smith::affine]]` a known attribute (the compiler, clang-tidy and clangd all read the
//     registry), kept in the AST as AnnotateAttr("smith::affine"), on a typedef or a tag;
//   - a PluginASTAction of type AddAfterMainAction, which clang adds to every frontend action (the compiler's,
//   clangd's)
//     without a command-line flag, runs the analysis. clang-tidy cannot report a diagnostic it has no id for, so
//     smith_affine_disable() (shim/tidy.cpp) turns it off there: every compile fails on a second use already.
//
// The analysis is one forward dataflow over clang's CFG per function that has a variable of an affine type (found by a
// walk of the body first; no other function pays for a CFG). The state is the set of places already used, each with
// the place of its first use. A place is a variable, a member path (`o.key`), and a constant index or the counter of a
// plain loop (`keys[0]`, `keys[i]`) for an array. A slice (`Key keys[:]`) is lowered by .smith to a statement
// expression over a temporary copy of the slice; its shape is recognised below (slicePlace), which needs no marker
// from the lowering. Reading a place is a use, `&key` included; an assignment to a place makes it fresh; passing a
// whole array or slice to a call (`random::split(key, keys)`) makes its elements fresh; a join is a union.
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/ParsedAttrInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Lex/Lexer.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace clang;

static bool pluginEnabled = true;

namespace {

// ---- The type ------------------------------------------------------------------------------------------------------

bool annotated(const Decl *decl) {
    for (const auto *attr : decl->specific_attrs<AnnotateAttr>())
        if (attr->getAnnotation() == "smith::affine") return true;
    return false;
}

bool affineType(QualType type) {
    for (int depth = 0; depth < 16 && !type.isNull(); depth++) {
        if (const auto *typedef_type = type->getAs<TypedefType>()) {
            if (annotated(typedef_type->getDecl())) return true;
            type = typedef_type->getDecl()->getUnderlyingType();
            continue;
        }
        const TagDecl *tag = type->getAsTagDecl();
        return tag != nullptr && annotated(tag);
    }
    return false;
}

// The element type of an array or of a slice (`struct smith_array_<T>_n { T *data; ... }`), else null.
QualType elementOf(QualType type) {
    if (const auto *array = type->getAsArrayTypeUnsafe()) return array->getElementType();
    if (const RecordDecl *record = type->getAsRecordDecl()) {
        if (record->getName().starts_with("smith_array_"))
            for (const FieldDecl *field : record->fields())
                if (field->getName() == "data" && field->getType()->isPointerType())
                    return field->getType()->getPointeeType();
    }
    return QualType();
}

bool holdsAffine(QualType type) {
    if (affineType(type)) return true;
    QualType element = elementOf(type);
    if (!element.isNull()) return affineType(element);
    const RecordDecl *record = type->getAsRecordDecl();
    if (record == nullptr || !record->isCompleteDefinition()) return false;
    for (const FieldDecl *field : record->fields())
        if (affineType(field->getType())) return true; // a struct with an affine member: reading the member is a use
    return false;
}

// ---- Places and what the body does with them -----------------------------------------------------------------------

// One step of a path: a member (field, 0), a constant index (null, n) or the counter of a plain loop (variable, -1).
using Step = std::pair<const void *, long long>;
using Key = std::pair<const VarDecl *, std::vector<Step>>;
constexpr long long counterIndex = -1;

enum class Action { Use, Assign, AssignAll, Reset, BadIndex };

struct Site {
    Action action;
    Key key;
    const VarDecl *variable = nullptr; // Reset: the variable that changed (a counter may be indexed by it)
    std::string text;
    SourceLocation loc;
};

bool isPrefix(const std::vector<Step> &prefix, const std::vector<Step> &path) {
    return prefix.size() <= path.size() && std::equal(prefix.begin(), prefix.end(), path.begin());
}

// Does the expression mention the variable? (for the bound of a loop)
bool mentions(const Stmt *stmt, const VarDecl *variable) {
    if (stmt == nullptr) return false;
    if (const auto *ref = dyn_cast<DeclRefExpr>(stmt))
        if (ref->getDecl() == variable) return true;
    for (const Stmt *child : stmt->children())
        if (mentions(child, variable)) return true;
    return false;
}

const VarDecl *variableOf(const Expr *expr) {
    if (const auto *ref = dyn_cast<DeclRefExpr>(expr->IgnoreParenImpCasts())) return dyn_cast<VarDecl>(ref->getDecl());
    return nullptr;
}

// Is `variable` changed (assigned, stepped, its address taken) anywhere in the statement?
bool changes(const Stmt *stmt, const VarDecl *variable) {
    if (stmt == nullptr) return false;
    const Expr *target = nullptr;
    if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
        if (unary->isIncrementDecrementOp() || unary->getOpcode() == UO_AddrOf) target = unary->getSubExpr();
    } else if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
        if (binary->isAssignmentOp()) target = binary->getLHS();
    }
    if (target != nullptr && variableOf(target) == variable) return true;
    for (const Stmt *child : stmt->children())
        if (changes(child, variable)) return true;
    return false;
}

class Classifier {
  public:
    Classifier(ASTContext &context) : context(context) {}

    std::map<const Stmt *, Site> sites;
    bool poisoned = false; // the body has an error: clang's own warnings skip it too
    bool interesting = false;

    void walk(const Stmt *stmt) {
        if (stmt == nullptr) return;
        if (const auto *expr = dyn_cast<Expr>(stmt)) {
            if (expr->containsErrors()) poisoned = true;
            if (site(expr)) return;
        }
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
            if (assignment(binary)) return;
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
            if (unary->isIncrementDecrementOp()) reset(unary, unary->getSubExpr());
        }
        if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            for (const Expr *argument : call->arguments())
                wholeArray(argument);
        }
        if (const auto *loop = dyn_cast<ForStmt>(stmt)) {
            loops.push_back(loop);
            for (const Stmt *child : stmt->children())
                walk(child);
            loops.pop_back();
            return;
        }
        for (const Stmt *child : stmt->children())
            walk(child);
    }

  private:
    ASTContext &context;
    std::vector<const ForStmt *> loops;

    bool assignment(const BinaryOperator *binary) {
        if (!binary->isAssignmentOp()) return false;
        Site found;
        if (binary->getOpcode() == BO_Assign && place(binary->getLHS(), found) && found.action == Action::Use &&
            affineType(binary->getLHS()->getType())) {
            found.action = Action::Assign;
            found.loc = binary->getExprLoc();
            sites[binary] = found;
            walk(binary->getRHS());
            return true;
        }
        if (binary->getOpcode() == BO_Assign && writesPart(binary->getLHS())) { // `key.tap = tap;` reads nothing
            walk(binary->getRHS());
            return true;
        }
        reset(binary, binary->getLHS());
        return false;
    }

    // `v.a.b = ...` or `v.a[0] = ...` where v holds affine values: a write to a part of a variable, which is no use.
    bool writesPart(const Expr *target) {
        bool part = false;
        for (target = target->IgnoreParens();; target = target->IgnoreParens()) {
            if (const auto *member = dyn_cast<MemberExpr>(target); member != nullptr && !member->isArrow()) {
                target = member->getBase();
            } else if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(target);
                       subscript != nullptr && subscript->getBase()->IgnoreParenImpCasts()->getType()->isArrayType()) {
                target = subscript->getBase()->IgnoreParenImpCasts();
            } else {
                break;
            }
            part = true;
        }
        const VarDecl *variable = variableOf(target);
        return part && variable != nullptr && variable->isLocalVarDeclOrParm() && holdsAffine(variable->getType());
    }

    // `i = ...`, `i++`: the places indexed by the counter `i` are other elements from here.
    void reset(const Stmt *node, const Expr *target) {
        const VarDecl *variable = variableOf(target);
        if (variable == nullptr) return;
        Site found;
        found.action = Action::Reset;
        found.variable = variable;
        sites[node] = found;
    }

    // An array or slice of affine values passed to a call (`split(key, keys)`) is filled by it: its elements are fresh.
    void wholeArray(const Expr *argument) {
        const VarDecl *variable = variableOf(argument);
        if (variable == nullptr || elementOf(variable->getType()).isNull() || !holdsAffine(variable->getType())) return;
        Site found;
        found.action = Action::AssignAll;
        found.key.first = variable;
        sites[argument->IgnoreParens()] = found;
    }

    // Is this expression a use of a place of an affine type? Then it is a site, and nothing below it is looked at.
    bool site(const Expr *expr) {
        if (!affineType(expr->getType())) return false;
        Site found;
        if (!place(expr, found)) return false;
        found.loc = expr->getBeginLoc();
        interesting = true;
        sites[expr->IgnoreParens()] = found;
        return true;
    }

    bool place(const Expr *expr, Site &out) {
        expr = expr->IgnoreParens();
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            const auto *variable = dyn_cast<VarDecl>(ref->getDecl());
            if (variable == nullptr || !variable->isLocalVarDeclOrParm() || !holdsAffine(variable->getType()))
                return false;
            out.action = Action::Use;
            out.key.first = variable;
            out.text = variable->getName().str();
            return true;
        }
        if (const auto *member = dyn_cast<MemberExpr>(expr)) {
            if (member->isArrow() || !place(member->getBase(), out)) return false;
            out.key.second.push_back({member->getMemberDecl(), 0});
            out.text += "." + member->getMemberDecl()->getName().str();
            return true;
        }
        if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(expr)) {
            const auto *base = subscript->getBase()->IgnoreParenImpCasts();
            if (!base->getType()->isArrayType() || !place(base, out)) return false;
            return index(subscript->getIdx(), out);
        }
        return slicePlace(expr, out);
    }

    // `*__extension__({ auto smith_s = (keys); size_t smith_i = (index); check; &smith_s.data[smith_i]; })`
    bool slicePlace(const Expr *expr, Site &out) {
        const auto *deref = dyn_cast<UnaryOperator>(expr);
        if (deref == nullptr || deref->getOpcode() != UO_Deref) return false;
        const auto *statement = dyn_cast<StmtExpr>(deref->getSubExpr()->IgnoreParens()); // which strips __extension__
        if (statement == nullptr || statement->getSubStmt()->body_empty()) return false;
        const auto *address = dyn_cast<UnaryOperator>(statement->getSubStmt()->body_back());
        if (address == nullptr || address->getOpcode() != UO_AddrOf) return false;
        const auto *subscript = dyn_cast<ArraySubscriptExpr>(address->getSubExpr()->IgnoreParens());
        if (subscript == nullptr) return false;
        const auto *data = dyn_cast<MemberExpr>(subscript->getBase()->IgnoreParenImpCasts());
        const VarDecl *copy = data == nullptr ? nullptr : variableOf(data->getBase());
        const VarDecl *number = variableOf(subscript->getIdx());
        if (copy == nullptr || number == nullptr || copy->getInit() == nullptr || number->getInit() == nullptr ||
            !copy->getName().starts_with("smith_s"))
            return false;
        if (!place(copy->getInit()->IgnoreParenImpCasts(), out)) return false;
        return index(number->getInit()->IgnoreParenImpCasts(), out);
    }

    // The index of an element: a constant, or the counter of a plain loop over the array. Any other is an error.
    bool index(const Expr *expr, Site &out) {
        Expr::EvalResult result;
        if (!expr->isValueDependent() && expr->EvaluateAsInt(result, context)) {
            out.key.second.push_back({nullptr, result.Val.getInt().getExtValue()});
            out.text += "[" + std::to_string(result.Val.getInt().getExtValue()) + "]";
            return true;
        }
        const VarDecl *counter = variableOf(expr);
        if (counter != nullptr && plainLoop(counter, out.key.first)) {
            out.key.second.push_back({counter, counterIndex});
            out.text += "[" + counter->getName().str() + "]";
            return true;
        }
        out.action = Action::BadIndex;
        out.text += "[...]";
        return true;
    }

    // for (usize i = 0; i < countof(a); i++) with `i` not changed in the body.
    bool plainLoop(const VarDecl *counter, const VarDecl *array) {
        for (auto at = loops.rbegin(); at != loops.rend(); ++at) {
            const ForStmt *loop = *at;
            const auto *declaration = dyn_cast_or_null<DeclStmt>(loop->getInit());
            if (declaration == nullptr || !declaration->isSingleDecl() || declaration->getSingleDecl() != counter)
                continue;
            return plainShape(loop, counter, array);
        }
        return false;
    }

    bool plainShape(const ForStmt *loop, const VarDecl *counter, const VarDecl *array) {
        Expr::EvalResult zero;
        if (counter->getInit() == nullptr || !counter->getInit()->EvaluateAsInt(zero, context) ||
            zero.Val.getInt().getExtValue() != 0)
            return false;
        const auto *condition =
            dyn_cast_or_null<BinaryOperator>(loop->getCond() ? loop->getCond()->IgnoreParens() : nullptr);
        if (condition == nullptr || condition->getOpcode() != BO_LT || variableOf(condition->getLHS()) != counter)
            return false;
        Expr::EvalResult size;
        bool bound = mentions(condition->getRHS(), array);
        if (!bound && !condition->getRHS()->isValueDependent() && condition->getRHS()->EvaluateAsInt(size, context))
            bound =
                array->getType()->isConstantArrayType() &&
                size.Val.getInt().getExtValue() ==
                    static_cast<long long>(
                        cast<ConstantArrayType>(array->getType()->getAsArrayTypeUnsafe())->getSize().getZExtValue());
        if (!bound || !steps(loop->getInc(), counter)) return false;
        return !changes(loop->getBody(), counter);
    }

    static bool steps(const Expr *inc, const VarDecl *counter) {
        if (inc == nullptr) return false;
        inc = inc->IgnoreParens();
        if (const auto *unary = dyn_cast<UnaryOperator>(inc))
            return (unary->getOpcode() == UO_PostInc || unary->getOpcode() == UO_PreInc) &&
                   variableOf(unary->getSubExpr()) == counter;
        if (const auto *binary = dyn_cast<BinaryOperator>(inc)) {
            Expr::EvalResult one;
            return binary->getOpcode() == BO_AddAssign && variableOf(binary->getLHS()) == counter &&
                   binary->getRHS()->EvaluateAsInt(one, counter->getASTContext()) &&
                   one.Val.getInt().getExtValue() == 1;
        }
        return false;
    }
};

// ---- The dataflow --------------------------------------------------------------------------------------------------

using State = std::map<Key, SourceLocation>;

class Flow {
  public:
    Flow(const CFG &cfg, const std::map<const Stmt *, Site> &sites) : cfg(cfg), sites(sites) {}

    struct Finding {
        const Site *site;
        SourceLocation first;
    };
    std::vector<Finding> findings;

    void run() {
        size_t count = cfg.getNumBlockIDs();
        in.assign(count, State());
        reached.assign(count, false);
        std::deque<const CFGBlock *> work;
        reached[cfg.getEntry().getBlockID()] = true;
        work.push_back(&cfg.getEntry());
        while (!work.empty()) {
            const CFGBlock *block = work.front();
            work.pop_front();
            State out = in[block->getBlockID()];
            transfer(*block, out, false);
            for (const CFGBlock::AdjacentBlock &next : block->succs()) {
                const CFGBlock *target = next.getReachableBlock();
                if (target == nullptr) continue;
                if (join(target->getBlockID(), out)) work.push_back(target);
            }
        }
        for (const CFGBlock *block : cfg) { // what each block finds, once the states are settled
            if (!reached[block->getBlockID()]) continue;
            State state = in[block->getBlockID()];
            transfer(*block, state, true);
        }
    }

  private:
    const CFG &cfg;
    const std::map<const Stmt *, Site> &sites;
    std::vector<State> in;
    std::vector<bool> reached;
    std::set<const Stmt *> reported;

    bool join(unsigned id, const State &out) {
        bool changed = !reached[id];
        reached[id] = true;
        for (const auto &entry : out)
            if (in[id].insert(entry).second) changed = true;
        return changed;
    }

    static void erase(State &state, const VarDecl *root, const std::vector<Step> &prefix) {
        for (auto at = state.begin(); at != state.end();) {
            if (at->first.first == root && isPrefix(prefix, at->first.second))
                at = state.erase(at);
            else
                ++at;
        }
    }

    static void forget(State &state, const VarDecl *counter) {
        for (auto at = state.begin(); at != state.end();) {
            bool indexed = false;
            for (const Step &step : at->first.second)
                if (step.first == counter && step.second == counterIndex) indexed = true;
            if (indexed || at->first.first == counter)
                at = state.erase(at);
            else
                ++at;
        }
    }

    void transfer(const CFGBlock &block, State &state, bool report) {
        for (const CFGElement &element : block) {
            std::optional<CFGStmt> statement = element.getAs<CFGStmt>();
            if (!statement) continue;
            const Stmt *stmt = statement->getStmt();
            if (const auto *declaration = dyn_cast<DeclStmt>(stmt)) {
                for (const Decl *decl : declaration->decls())
                    if (const auto *variable = dyn_cast<VarDecl>(decl)) forget(state, variable);
                continue;
            }
            auto found = sites.find(stmt);
            if (found != sites.end()) apply(found->second, stmt, state, report);
        }
    }

    void apply(const Site &site, const Stmt *stmt, State &state, bool report) {
        switch (site.action) {
        case Action::Use: {
            auto used = state.find(site.key);
            if (used == state.end()) {
                state.insert({site.key, site.loc});
            } else if (report && reported.insert(stmt).second) {
                findings.push_back({&site, used->second});
            }
            break;
        }
        case Action::BadIndex:
            if (report && reported.insert(stmt).second) findings.push_back({&site, SourceLocation()});
            break;
        case Action::Assign:
            erase(state, site.key.first, site.key.second);
            break;
        case Action::AssignAll:
            for (auto at = state.begin(); at != state.end();)
                if (at->first.first == site.key.first && !at->first.second.empty())
                    at = state.erase(at);
                else
                    ++at;
            break;
        case Action::Reset:
            forget(state, site.variable);
            break;
        }
    }
};

// ---- Narrowing of a payload enum (issue #198, skill smith-lang-enums) ----------------------------------------------
//
// A struct with the annotation `smith::enum` is a payload enum as the lowering writes it: a member `tag` of an enum
// type whose constants are `<struct>_tag_<variant>`, and an anonymous union with one member per variant that has a
// payload. Reading the member `x.circle` (or `x->circle`) is an error unless on every path the tag was established: by
// `x.tag == ..._tag_circle` (an if, `&&`, `||`, `!`, a conditional, a loop's condition) or by a `case` of a switch on
// `x.tag`. The state is the set of places known to hold a variant, joined by intersection; it is a forward dataflow
// over the same CFG as the affine one. An assignment to the place (or a part of it, or to a pointer it is read
// through), the address of it, and a call that is given a pointer into it end what is known about it.

constexpr long long derefStep = -2;

bool enumRecord(const RecordDecl *record) {
    if (record == nullptr) return false;
    for (const Decl *each : record->redecls())
        for (const auto *attr : each->specific_attrs<AnnotateAttr>())
            if (attr->getAnnotation() == "smith::enum") return true;
    return false;
}

// The record of the enum a `tag` member belongs to, or null.
const RecordDecl *tagOwner(const MemberExpr *member) {
    const auto *field = dyn_cast<FieldDecl>(member->getMemberDecl());
    if (field == nullptr || field->getName() != "tag" || !enumRecord(field->getParent())) return nullptr;
    return field->getParent();
}

// The variant a constant names: `<name>_tag_<variant>`, for the struct `smith_enum_<name>`.
std::string variantOf(const RecordDecl *record, const EnumConstantDecl *constant) {
    // A module's enum is `smith_enum_<name>` with the constants `<name>_tag_<variant>`; a type function's
    // (`option(usize)`) is `smith_enum_<function>_<arguments>` with the constants
    // `smith_enum_<function>_<arguments>_tag_<variant>`.
    std::string name = constant->getName().str();
    llvm::StringRef whole = record->getName();
    llvm::StringRef stripped = whole;
    stripped.consume_front("smith_enum_");
    for (llvm::StringRef each : {stripped, whole}) {
        std::string prefix = each.str() + "_tag_";
        if (name.rfind(prefix, 0) == 0) return name.substr(prefix.size());
    }
    return name;
}

bool isAnonymousField(const ValueDecl *decl) {
    const auto *field = dyn_cast<FieldDecl>(decl);
    return field != nullptr && field->isAnonymousStructOrUnion();
}

// The place an expression names: a variable, then members and dereferences. The unnamed union is no step.
bool placeOf(const Expr *expr, Key &out) {
    expr = expr->IgnoreParenImpCasts();
    if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
        const auto *variable = dyn_cast<VarDecl>(ref->getDecl());
        if (variable == nullptr || !variable->isLocalVarDeclOrParm()) return false;
        out.first = variable;
        return true;
    }
    if (const auto *member = dyn_cast<MemberExpr>(expr)) {
        if (!placeOf(member->getBase(), out)) return false;
        if (member->isArrow()) out.second.push_back({nullptr, derefStep});
        if (!isAnonymousField(member->getMemberDecl())) out.second.push_back({member->getMemberDecl(), 0});
        return true;
    }
    if (const auto *unary = dyn_cast<UnaryOperator>(expr)) {
        if (unary->getOpcode() != UO_Deref || !placeOf(unary->getSubExpr(), out)) return false;
        out.second.push_back({nullptr, derefStep});
        return true;
    }
    return false;
}

std::string render(const Key &place) {
    std::string text = place.first->getName().str();
    for (size_t i = 0; i < place.second.size(); i++) {
        const Step &step = place.second[i];
        bool field = step.first != nullptr;
        if (field) {
            text += "." + cast<FieldDecl>(static_cast<const Decl *>(step.first))->getName().str();
        } else if (i + 1 < place.second.size() && place.second[i + 1].first != nullptr) {
            text += "->" + cast<FieldDecl>(static_cast<const Decl *>(place.second[i + 1].first))->getName().str();
            i++;
        } else {
            text = "*" + text;
        }
    }
    return text;
}

// `x.tag` or `x->tag`: the place of x (the enum), else false.
bool tagPlace(const Expr *expr, Key &out) {
    const auto *member = dyn_cast<MemberExpr>(expr->IgnoreParenImpCasts());
    if (member == nullptr || tagOwner(member) == nullptr || !placeOf(member->getBase(), out)) return false;
    if (member->isArrow()) out.second.push_back({nullptr, derefStep});
    return true;
}

// The variant a constant of the tag's type names, as `x.tag == K` compares it.
bool variantConstant(const Expr *tag, const Expr *constant, ASTContext &context, std::string &variant) {
    const auto *member = cast<MemberExpr>(tag->IgnoreParenImpCasts());
    const auto *type = member->getType()->getAs<EnumType>();
    Expr::EvalResult result;
    if (type == nullptr || constant->isValueDependent() || !constant->EvaluateAsInt(result, context)) return false;
    for (const EnumConstantDecl *each : type->getDecl()->enumerators())
        if (llvm::APSInt::isSameValue(each->getInitVal(), result.Val.getInt())) {
            variant = variantOf(tagOwner(member), each);
            return true;
        }
    return false;
}

// What `cond` being `wanted` says about a place: `x.tag == K` true, `x.tag != K` false, through `!`.
bool conditionFact(const Expr *cond, bool wanted, ASTContext &context, Key &place, std::string &variant) {
    cond = cond->IgnoreParenImpCasts();
    if (const auto *unary = dyn_cast<UnaryOperator>(cond))
        return unary->getOpcode() == UO_LNot && conditionFact(unary->getSubExpr(), !wanted, context, place, variant);
    const auto *binary = dyn_cast<BinaryOperator>(cond);
    if (binary == nullptr || (binary->getOpcode() != BO_EQ && binary->getOpcode() != BO_NE)) return false;
    if ((binary->getOpcode() == BO_EQ) != wanted) return false;
    for (int side = 0; side < 2; side++) {
        const Expr *tag = side == 0 ? binary->getLHS() : binary->getRHS();
        const Expr *constant = side == 0 ? binary->getRHS() : binary->getLHS();
        place = Key();
        if (tagPlace(tag, place) && variantConstant(tag, constant, context, variant)) return true;
    }
    return false;
}

enum class NarrowAction { Read, Kill };

struct NarrowSite {
    NarrowAction action;
    Key place;               // Read: the enum that is read; Kill: the first place that ends
    std::vector<Key> others; // Kill: more places (a call's pointers)
    std::string variant;
    SourceLocation loc;
};

// The place a write or an address ends: the enum itself when it is its tag.
bool endedPlace(const Expr *expr, Key &out) {
    return tagPlace(expr, out) || placeOf(expr, out);
}

class NarrowClassifier {
  public:
    std::map<const Stmt *, NarrowSite> sites;
    bool poisoned = false;
    bool interesting = false;

    void walk(const Stmt *stmt) {
        if (stmt == nullptr) return;
        if (const auto *expr = dyn_cast<Expr>(stmt))
            if (expr->containsErrors()) poisoned = true;
        if (const auto *member = dyn_cast<MemberExpr>(stmt)) read(member);
        if (const auto *binary = dyn_cast<BinaryOperator>(stmt)) {
            if (binary->isAssignmentOp()) kill(binary, binary->getLHS());
        } else if (const auto *unary = dyn_cast<UnaryOperator>(stmt)) {
            if (unary->isIncrementDecrementOp() || unary->getOpcode() == UO_AddrOf) kill(unary, unary->getSubExpr());
        } else if (const auto *call = dyn_cast<CallExpr>(stmt)) {
            calls(call);
        }
        for (const Stmt *child : stmt->children())
            walk(child);
    }

  private:
    // `x.circle`: the member of the unnamed union of an enum, read through the unnamed member of x.
    void read(const MemberExpr *member) {
        const auto *field = dyn_cast<FieldDecl>(member->getMemberDecl());
        if (field == nullptr || field->isAnonymousStructOrUnion() || !field->getParent()->isAnonymousStructOrUnion())
            return;
        const auto *owner = dyn_cast<RecordDecl>(field->getParent()->getDeclContext());
        if (!enumRecord(owner)) return;
        interesting = true;
        NarrowSite site;
        site.action = NarrowAction::Read;
        site.variant = field->getName().str();
        site.loc = member->getBeginLoc();
        if (!placeOf(member->getBase(), site.place)) site.place = Key(); // no place: never narrowed
        sites[member] = site;
    }

    void kill(const Stmt *node, const Expr *target) {
        NarrowSite site;
        site.action = NarrowAction::Kill;
        if (!endedPlace(target, site.place)) return;
        sites[node] = site;
    }

    void calls(const CallExpr *call) {
        NarrowSite site;
        site.action = NarrowAction::Kill;
        for (const Expr *argument : call->arguments()) {
            Key place;
            if (!argument->getType()->isPointerType() || !placeOf(argument, place)) continue;
            if (site.place.first == nullptr)
                site.place = place;
            else
                site.others.push_back(place);
        }
        if (site.place.first != nullptr) sites[call] = site;
    }
};

using Facts = std::map<Key, std::string>;

class NarrowFlow {
  public:
    NarrowFlow(const CFG &cfg, const std::map<const Stmt *, NarrowSite> &sites, ASTContext &context)
        : cfg(cfg), sites(sites), context(context) {}

    std::vector<const NarrowSite *> findings;

    void run() {
        size_t count = cfg.getNumBlockIDs();
        in.assign(count, Facts());
        reached.assign(count, false);
        std::deque<const CFGBlock *> work;
        reached[cfg.getEntry().getBlockID()] = true;
        work.push_back(&cfg.getEntry());
        while (!work.empty()) {
            const CFGBlock *block = work.front();
            work.pop_front();
            Facts out = in[block->getBlockID()];
            transfer(*block, out, false);
            size_t index = 0;
            for (const CFGBlock::AdjacentBlock &next : block->succs()) {
                const CFGBlock *target = next.getReachableBlock();
                if (target != nullptr) {
                    Facts along = out;
                    establish(*block, index, target, along);
                    if (join(target->getBlockID(), along)) work.push_back(target);
                }
                index++;
            }
        }
        for (const CFGBlock *block : cfg) {
            if (!reached[block->getBlockID()]) continue;
            Facts state = in[block->getBlockID()];
            transfer(*block, state, true);
        }
    }

  private:
    const CFG &cfg;
    const std::map<const Stmt *, NarrowSite> &sites;
    ASTContext &context;
    std::vector<Facts> in;
    std::vector<bool> reached;
    std::set<const Stmt *> reported;

    // The first arrival copies; the others keep what both agree on.
    bool join(unsigned id, const Facts &out) {
        if (!reached[id]) {
            reached[id] = true;
            in[id] = out;
            return true;
        }
        bool changed = false;
        for (auto at = in[id].begin(); at != in[id].end();) {
            auto other = out.find(at->first);
            if (other == out.end() || other->second != at->second) {
                at = in[id].erase(at);
                changed = true;
            } else {
                ++at;
            }
        }
        return changed;
    }

    // What the edge from `block` to `target` says: the true edge of `x.tag == K`, the `case K:` edge of a switch.
    void establish(const CFGBlock &block, size_t index, const CFGBlock *target, Facts &state) {
        Key place;
        std::string variant;
        const Stmt *terminator = block.getTerminatorStmt();
        if (terminator == nullptr) return;
        if (const auto *selection = dyn_cast<SwitchStmt>(terminator)) {
            const auto *label = dyn_cast_or_null<CaseStmt>(target->getLabel());
            if (label == nullptr || label->getRHS() != nullptr || !tagPlace(selection->getCond(), place)) return;
            if (!variantConstant(selection->getCond(), label->getLHS(), context, variant)) return;
            state[place] = variant;
            return;
        }
        if (block.succ_size() != 2 || block.empty()) return;
        std::optional<CFGStmt> last = block.back().getAs<CFGStmt>();
        const auto *cond = last ? dyn_cast<Expr>(last->getStmt()) : nullptr;
        if (cond != nullptr && conditionFact(cond, index == 0, context, place, variant)) state[place] = variant;
    }

    static bool within(const Key &prefix, const Key &key) {
        return prefix.first == key.first && isPrefix(prefix.second, key.second);
    }

    static void end(Facts &state, const Key &prefix) {
        for (auto at = state.begin(); at != state.end();) {
            if (within(prefix, at->first))
                at = state.erase(at);
            else
                ++at;
        }
    }

    void transfer(const CFGBlock &block, Facts &state, bool report) {
        for (const CFGElement &element : block) {
            std::optional<CFGStmt> statement = element.getAs<CFGStmt>();
            if (!statement) continue;
            const Stmt *stmt = statement->getStmt();
            if (const auto *declaration = dyn_cast<DeclStmt>(stmt)) {
                for (const Decl *decl : declaration->decls())
                    if (const auto *variable = dyn_cast<VarDecl>(decl)) {
                        Key place;
                        place.first = variable;
                        end(state, place);
                    }
                continue;
            }
            auto found = sites.find(stmt);
            if (found == sites.end()) continue;
            const NarrowSite &site = found->second;
            if (site.action == NarrowAction::Kill) {
                end(state, site.place);
                for (const Key &other : site.others)
                    end(state, other);
            } else if (report && reported.insert(stmt).second) {
                auto known = state.find(site.place);
                if (site.place.first == nullptr || known == state.end() || known->second != site.variant)
                    findings.push_back(&site);
            }
        }
    }
};

// ---- The messages --------------------------------------------------------------------------------------------------

const char *const hint = "write the plain loop `for (usize i = 0; i < countof(keys); i++)` and use `keys[i]` once in "
                         "its body, or reassign the value in the loop (`key = next[0];`)";

class Reporter {
  public:
    Reporter(ASTContext &context) : context(context), engine(context.getDiagnostics()) {
        again = engine.getCustomDiagID(DiagnosticsEngine::Error,
                                       "smith-affine: '%0' is used again; it was already used at line %1");
        first = engine.getCustomDiagID(DiagnosticsEngine::Note, "'%0' was first used here");
        loop = engine.getCustomDiagID(DiagnosticsEngine::Error,
                                      "smith-affine: '%0' is used in a loop, so it would be used once per iteration");
        index = engine.getCustomDiagID(DiagnosticsEngine::Error,
                                       "smith-affine: '%0': an element of an array of affine values is used through a "
                                       "constant index or the counter of a plain loop");
        advice = engine.getCustomDiagID(DiagnosticsEngine::Note, "%0");
        narrow = engine.getCustomDiagID(
            DiagnosticsEngine::Error,
            "smith-enum: '%0' is read where '%1' may not be a %2; test '%1 == .%2' or switch on '%1'");
    }

    void report(const NarrowSite &site) {
        if (site.place.first == nullptr) {
            engine.Report(site.loc, narrow) << "the payload" << "the enum" << site.variant;
            return;
        }
        std::string owner = render(site.place);
        Key through = site.place;
        bool arrow = !through.second.empty() && through.second.back().first == nullptr;
        if (arrow) through.second.pop_back();
        std::string access = arrow ? render(through) + "->" + site.variant : owner + "." + site.variant;
        engine.Report(site.loc, narrow) << access << owner << site.variant;
    }

    void report(const Flow::Finding &finding) {
        const Site &site = *finding.site;
        if (site.action == Action::BadIndex) {
            engine.Report(site.loc, index) << site.text;
            engine.Report(site.loc, advice) << hint;
            return;
        }
        if (finding.first == site.loc) {
            engine.Report(site.loc, loop) << site.text;
            engine.Report(site.loc, advice) << hint;
            return;
        }
        unsigned line = context.getSourceManager().getSpellingLineNumber(finding.first);
        engine.Report(site.loc, again) << site.text << line;
        engine.Report(finding.first, first) << site.text;
    }

  private:
    ASTContext &context;
    DiagnosticsEngine &engine;
    unsigned again, first, loop, index, advice, narrow;
};

// ---- `void *` does not convert to `T *` in a module (issue #398)
// ------------------------------------------------------
//
// C converts `void *` to any `T *` with no cast. In a file with a `module` line it is an error at every position
// (assignment, initialization, argument, return, a `?:` arm): clang records each as an implicit pointer cast whose
// operand is a `void *`, which is what the visitor looks for. `T *` to `void *` loses nothing and stays implicit, a
// null pointer constant is not a `void *` value (clang makes it a null-to-pointer cast), and a written cast is an
// explicit cast, not an implicit one. A file without a `module` line is not looked at: a module may reject C, never
// change it.

bool isModuleFile(const ASTContext &context) {
    const SourceManager &sm = context.getSourceManager();
    llvm::StringRef text = sm.getBufferData(sm.getMainFileID());
    for (size_t at = text.find("module "); at != llvm::StringRef::npos; at = text.find("module ", at + 1))
        if (at == 0 || text[at - 1] == '\n') return true;
    return false;
}

// Whether `(T *)` can be written in front of the text as it is: a name with members, indexes and calls, no operator.
bool isPostfix(const std::string &text) {
    return text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.>-[](), ") ==
           std::string::npos;
}

class VoidPointerVisitor : public RecursiveASTVisitor<VoidPointerVisitor> {
  public:
    explicit VoidPointerVisitor(ASTContext &context)
        : context(context), engine(context.getDiagnostics()), policy(context.getPrintingPolicy()) {
        policy.SuppressTagKeyword = true;
        error = engine.getCustomDiagID(DiagnosticsEngine::Error,
                                       "`void *` doesn't convert to `%0` silently; write `(%0)%1`, or use a typed API "
                                       "(`mem::view`, a lambda)");
    }

    bool shouldVisitTemplateInstantiations() const {
        return false;
    }

    bool TraverseDecl(Decl *decl) {
        if (decl != nullptr && !decl->isImplicit() && decl->getLocation().isValid() &&
            context.getSourceManager().isInSystemHeader(decl->getLocation()))
            return true;
        return RecursiveASTVisitor::TraverseDecl(decl);
    }

    bool VisitImplicitCastExpr(ImplicitCastExpr *cast) {
        if (cast->getCastKind() != CK_BitCast) return true;
        QualType to = cast->getType();
        const Expr *from = cast->getSubExpr();
        QualType source = from->getType();
        if (!to->isPointerType() || !source->isPointerType()) return true;
        if (!source->getPointeeType()->isVoidType() || to->getPointeeType()->isVoidType()) return true;
        if (from->isNullPointerConstant(context, Expr::NPC_ValueDependentIsNull)) return true; // `p == NULL`
        SourceManager &sm = context.getSourceManager();
        SourceLocation loc = from->getExprLoc();
        if (loc.isInvalid() || !sm.isWrittenInMainFile(sm.getExpansionLoc(loc))) return true;
        SourceRange range = from->getSourceRange();
        std::string text = Lexer::getSourceText(CharSourceRange::getTokenRange(range), sm, context.getLangOpts()).str();
        if (text.empty() || text.size() > 60 || text.find('\n') != std::string::npos)
            text = "(...)";
        else if (!isPostfix(text))
            text = "(" + text + ")";
        engine.Report(loc, error) << to.getAsString(policy) << text;
        return true;
    }

  private:
    ASTContext &context;
    DiagnosticsEngine &engine;
    PrintingPolicy policy;
    unsigned error;
};

// ---- Every function that has a body
// ----------------------------------------------------------------------------------

class Consumer : public ASTConsumer {
  public:
    explicit Consumer(CompilerInstance &) {}

    void HandleTranslationUnit(ASTContext &context) override {
        if (isModuleFile(context)) VoidPointerVisitor(context).TraverseDecl(context.getTranslationUnitDecl());
        Reporter reporter(context);
        visit(context.getTranslationUnitDecl(), context, reporter);
    }

  private:
    static bool skipped(const Decl *decl, ASTContext &context) {
        if (decl->isImplicit()) return true;
        SourceLocation loc = decl->getLocation();
        return loc.isValid() && context.getSourceManager().isInSystemHeader(loc);
    }

    void visit(const DeclContext *scope, ASTContext &context, Reporter &reporter) {
        for (const Decl *decl : scope->decls()) {
            if (skipped(decl, context)) continue;
            if (const auto *function = dyn_cast<FunctionDecl>(decl)) {
                analyse(function, context, reporter);
            } else if (isa<NamespaceDecl>(decl) || isa<LinkageSpecDecl>(decl)) {
                visit(cast<DeclContext>(decl), context, reporter);
            } else if (const auto *record = dyn_cast<CXXRecordDecl>(decl)) {
                if (!record->isDependentContext() && !isa<ClassTemplateSpecializationDecl>(record))
                    visit(record, context, reporter);
            }
        }
    }

    // A function that is a template, an instance of one, dependent, invalid or with an error in its body is left alone:
    // building a CFG of it can crash, and clang's own warnings skip it too.
    static void analyse(const FunctionDecl *function, ASTContext &context, Reporter &reporter) {
        if (!function->doesThisDeclarationHaveABody() || function->isInvalidDecl() || function->isDependentContext() ||
            function->isTemplated() || function->isTemplateInstantiation() || function->isDeleted())
            return;
        const Stmt *body = function->getBody();
        if (body == nullptr) return;
        Classifier classifier(context);
        classifier.walk(body);
        NarrowClassifier narrowing;
        narrowing.walk(body);
        bool affine = classifier.interesting && !classifier.poisoned;
        bool narrow = narrowing.interesting && !narrowing.poisoned;
        if (!affine && !narrow) return;
        CFG::BuildOptions options;
        options.setAllAlwaysAdd();
        std::unique_ptr<CFG> cfg = CFG::buildCFG(function, const_cast<Stmt *>(body), &context, options);
        if (!cfg) return;
        if (affine) {
            Flow flow(*cfg, classifier.sites);
            flow.run();
            for (const Flow::Finding &finding : flow.findings)
                reporter.report(finding);
        }
        if (narrow) {
            NarrowFlow flow(*cfg, narrowing.sites, context);
            flow.run();
            for (const NarrowSite *finding : flow.findings)
                reporter.report(*finding);
        }
    }
};

// ---- The registrations
// -----------------------------------------------------------------------------------------------

struct AffineAttr final : ParsedAttrInfo {
    AffineAttr() {
        static constexpr Spelling spellings[] = {{ParsedAttr::AS_C23, "smith::affine"},
                                                 {ParsedAttr::AS_CXX11, "smith::affine"},
                                                 {ParsedAttr::AS_GNU, "smith_affine"}};
        Spellings = spellings;
    }
    bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {
        if (isa<TypedefNameDecl>(D) || isa<TagDecl>(D)) return true;
        S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type)
            << Attr << Attr.isRegularKeywordAttribute() << ExpectedTypeOrNamespace;
        return false;
    }
    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        D->addAttr(AnnotateAttr::Create(S.Context, "smith::affine", nullptr, 0, Attr.getRange()));
        return AttributeApplied;
    }
};

struct AffinePlugin final : PluginASTAction {
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &instance, StringRef) override {
        if (!pluginEnabled) return std::make_unique<ASTConsumer>(); // never null: the multiplexer calls it
        return std::make_unique<Consumer>(instance);
    }
    bool ParseArgs(const CompilerInstance &, const std::vector<std::string> &) override {
        return true;
    }
    ActionType getActionType() override {
        return AddAfterMainAction;
    }
};

const ParsedAttrInfoRegistry::Add<AffineAttr> attribute("smith-affine-attr", "[[smith::affine]]");
const FrontendPluginRegistry::Add<AffinePlugin> plugin("smith-affine", "a value of an affine type is used once");

} // namespace

// clang-tidy's consumer of diagnostics cannot name a custom id (it crashes in getCheckName): smith lint turns the
// analysis off, and smith's compile of the same file reports it.
extern "C" void smith_affine_disable() {
    pluginEnabled = false;
}
