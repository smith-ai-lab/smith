// [[smith::affine]]: a value of a type marked with it is used at most once (issue #189, skill smith-lang-affine).
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
#include "clang/AST/Stmt.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/ParsedAttrInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
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
    unsigned again, first, loop, index, advice;
};

// ---- Every function that has a body
// ----------------------------------------------------------------------------------

class Consumer : public ASTConsumer {
  public:
    explicit Consumer(CompilerInstance &) {}

    void HandleTranslationUnit(ASTContext &context) override {
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
        if (!classifier.interesting || classifier.poisoned) return;
        CFG::BuildOptions options;
        options.setAllAlwaysAdd();
        std::unique_ptr<CFG> cfg = CFG::buildCFG(function, const_cast<Stmt *>(body), &context, options);
        if (!cfg) return;
        Flow flow(*cfg, classifier.sites);
        flow.run();
        for (const Flow::Finding &finding : flow.findings)
            reporter.report(finding);
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
