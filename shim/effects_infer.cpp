// Effects, inferred (issue #207, skill smith-lang-effects): what a function does to the world, from its body and its
// callees, and the two annotations that state it.
//
//   [[smith::pure]]                    nothing at all
//   [[smith::effects(alloc, net)]]     a ceiling: what the function may do (a promise on a declaration with no body)
//   no attribute                       inferred
// `[[smith::alloc]]`, one attribute for each effect, is also accepted, undocumented. The arguments of a scoped C23
// attribute reach a plugin's handler only with patches/clang-plugin-attr-args.patch (LLVM r9): clang read them only for
// an attribute it knew without the plugins, and a bare identifier after the first as an expression.
//
// A function that has a body gets the set of the effects of its calls, followed through the functions of the file
// (and of the headers it includes) to the calls the file cannot see into: a function of libc or libSystem has the set
// the table gives it (shim/effects_table.h), a declaration with an attribute and no body has that set (a promise smith
// trusts), and any other function without a body is `c`, every effect. A call through a function pointer is `c`, as are
// inline assembly and an Objective-C message; writing a global or a function-local static is `globals`. Crashing is no
// effect. An annotation is a ceiling: an inferred effect beyond it is a compile error naming the chain
// (`bad -> log_it (log.c:12) -> printf`); the declared set, not the inferred one, is what a caller sees.
//
// Linked into smith and run on every compile with no flag, like [[smith::affine]] (shim/affine.cpp): a ParsedAttrInfo
// makes the attributes known (kept as AnnotateAttr, "smith::effects:alloc,net"), and a PluginASTAction of type
// AddAfterMainAction reads the translation unit when it is complete. clang-tidy cannot report a diagnostic it has no
// id for, so smith_effects_disable() (shim/tidy.cpp) turns it off there. Only the functions the main file defines are
// judged. Across files (a module's header, a C file's summary) is a later step: today a function with no body and no
// attribute is `c`.
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Basic/ParsedAttrInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/Path.h"

#include "effects_table.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace clang;
using namespace smith_effects;

static bool pluginEnabled = true;

namespace {

constexpr const char *prefix = "smith::effects:";

// ---- The annotations -----------------------------------------------------------------------------------------------

// The declared set of a function, when it has an annotation: the union of its attributes' sets.
bool declaredSet(const Decl *decl, uint32_t &set) {
    bool found = false;
    set = 0;
    for (const Decl *redecl : decl->redecls()) {
        for (const auto *attr : redecl->specific_attrs<AnnotateAttr>()) {
            llvm::StringRef text = attr->getAnnotation();
            if (!text.consume_front(prefix)) continue;
            found = true;
            while (!text.empty()) {
                auto [word, rest] = text.split(',');
                for (const Name &name : names)
                    if (word == name.text) set |= name.bit;
                text = rest;
            }
        }
    }
    return found;
}

// Sets the flag r9 adds to ParsedAttrInfo (patches/clang-plugin-attr-args.patch), when the headers have it: the parser
// then reads `alloc, net` as identifiers.
template <class Info> auto allowIdentifiers(Info &info, int) -> decltype(info.HasVariadicIdentifierArg = 1, void()) {
    info.HasVariadicIdentifierArg = 1;
}
template <class Info> void allowIdentifiers(Info &, long) {}

// `[[smith::effects(alloc, net)]]`, and `__attribute__((smith_effects("alloc", "net")))`: the names are identifiers or
// strings.
struct EffectsAttr final : ParsedAttrInfo {
    EffectsAttr() {
        OptArgs = 15;
        AcceptsExprPack = 0;
        allowIdentifiers(*this, 0);
        static constexpr Spelling spellings[] = {{ParsedAttr::AS_C23, "smith::effects"},
                                                 {ParsedAttr::AS_CXX11, "smith::effects"},
                                                 {ParsedAttr::AS_GNU, "smith_effects"}};
        Spellings = spellings;
    }
    bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {
        if (isa<FunctionDecl>(D)) return true;
        S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type)
            << Attr << Attr.isRegularKeywordAttribute() << ExpectedFunction;
        return false;
    }
    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        std::string text = prefix;
        for (unsigned at = 0; at < Attr.getNumArgs(); at++) {
            std::string written;
            if (Attr.isArgIdent(at)) {
                written = Attr.getArgAsIdent(at)->getIdentifierInfo()->getName().str();
            } else if (const auto *literal = dyn_cast_or_null<StringLiteral>(Attr.getArgAsExpr(at))) {
                written = literal->getString().str();
            }
            bool known = false;
            for (const Name &name : names)
                known = known || written == name.text;
            if (!known) {
                unsigned id = S.getDiagnostics().getCustomDiagID(
                    DiagnosticsEngine::Error, "smith-effect: an effect is one of alloc, files, net, console, clock, "
                                              "process, env, entropy, globals, c");
                S.Diag(Attr.getLoc(), id);
                return AttributeNotApplied;
            }
            if (at > 0) text += ",";
            text += written;
        }
        D->addAttr(AnnotateAttr::Create(S.Context, text, nullptr, 0, Attr.getRange()));
        return AttributeApplied;
    }
};

// `[[smith::alloc]]`, `[[smith::files]]`... one for each effect, with no arguments: the set of a function is the union
// of the ones it has.
template <size_t Index> struct EffectAttr final : ParsedAttrInfo {
    EffectAttr() {
        static const std::string scoped = std::string("smith::") + names[Index].text;
        static const std::string plain = std::string("smith_") + names[Index].text;
        static const Spelling spellings[] = {{ParsedAttr::AS_C23, scoped.c_str()},
                                             {ParsedAttr::AS_CXX11, scoped.c_str()},
                                             {ParsedAttr::AS_GNU, plain.c_str()}};
        Spellings = spellings;
    }
    bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {
        if (isa<FunctionDecl>(D)) return true;
        S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type)
            << Attr << Attr.isRegularKeywordAttribute() << ExpectedFunction;
        return false;
    }
    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        D->addAttr(
            AnnotateAttr::Create(S.Context, std::string(prefix) + names[Index].text, nullptr, 0, Attr.getRange()));
        return AttributeApplied;
    }
};

struct PureAttr final : ParsedAttrInfo {
    PureAttr() {
        static constexpr Spelling spellings[] = {{ParsedAttr::AS_C23, "smith::pure"},
                                                 {ParsedAttr::AS_CXX11, "smith::pure"},
                                                 {ParsedAttr::AS_GNU, "smith_pure"}};
        Spellings = spellings;
    }
    bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {
        if (isa<FunctionDecl>(D)) return true;
        S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type)
            << Attr << Attr.isRegularKeywordAttribute() << ExpectedFunction;
        return false;
    }
    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        D->addAttr(AnnotateAttr::Create(S.Context, prefix, nullptr, 0, Attr.getRange()));
        return AttributeApplied;
    }
};

// ---- What a body does ----------------------------------------------------------------------------------------------

// One call (or other effect) of a function: what it reaches, where, and the callee when the file defines it.
struct Edge {
    const FunctionDecl *callee = nullptr; // a function with a body in this file (its definition)
    uint32_t effects = 0;                 // a leaf: the effects of the raw call (callee null)
    std::string name;                     // what the leaf or callee is called
    SourceLocation loc;
};

struct Node {
    const FunctionDecl *function = nullptr;
    std::vector<Edge> edges;
    uint32_t inferred = 0;
    uint32_t declared = 0;
    bool annotated = false;
};

// The root variable of an lvalue (`g`, `g.a[1]`, `p->x` has none: a write through a pointer is the caller's).
const VarDecl *writtenGlobal(const Expr *expr) {
    for (int depth = 0; depth < 32 && expr != nullptr; depth++) {
        expr = expr->IgnoreParenImpCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            const auto *var = dyn_cast<VarDecl>(ref->getDecl());
            if (var == nullptr || !var->hasGlobalStorage() || var->getType().isConstQualified() || var->isConstexpr())
                return nullptr;
            return var;
        }
        if (const auto *member = dyn_cast<MemberExpr>(expr)) {
            if (member->isArrow()) return nullptr;
            expr = member->getBase();
        } else if (const auto *index = dyn_cast<ArraySubscriptExpr>(expr)) {
            expr = index->getBase();
            if (!expr->IgnoreParenImpCasts()->getType()->isArrayType()) return nullptr;
        } else {
            return nullptr;
        }
    }
    return nullptr;
}

// Walks a body with no recursion (a work list) and records the effects it finds.
class Collector {
  public:
    explicit Collector(Node &node) : node(node) {}

    void walk(const Stmt *body) {
        std::vector<const Stmt *> work{body};
        while (!work.empty()) {
            const Stmt *statement = work.back();
            work.pop_back();
            if (statement == nullptr) continue;
            visit(statement);
            for (const Stmt *child : statement->children())
                work.push_back(child);
        }
    }

  private:
    Node &node;

    void visit(const Stmt *statement) {
        if (const auto *call = dyn_cast<CallExpr>(statement)) {
            onCall(call);
        } else if (isa<ObjCMessageExpr>(statement)) {
            leaf(statement->getBeginLoc(), C, "an Objective-C message");
        } else if (isa<AsmStmt>(statement)) {
            leaf(statement->getBeginLoc(), C, "inline assembly");
        } else if (const auto *op = dyn_cast<BinaryOperator>(statement)) {
            if (op->isAssignmentOp()) global(op->getLHS(), op->getOperatorLoc());
        } else if (const auto *unary = dyn_cast<UnaryOperator>(statement)) {
            if (unary->isIncrementDecrementOp()) global(unary->getSubExpr(), unary->getOperatorLoc());
        }
    }

    void onCall(const CallExpr *call) {
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr) {
            leaf(call->getBeginLoc(), C, "a call through a function pointer");
            return;
        }
        if (const FunctionDecl *definition = callee->getDefinition()) {
            Edge edge;
            edge.callee = definition;
            edge.name = definition->getNameAsString();
            edge.loc = call->getBeginLoc();
            node.edges.push_back(edge);
            return;
        }
        std::string name = callee->getNameAsString();
        uint32_t set = 0;
        bool builtin = llvm::StringRef(name).starts_with("__builtin_") || callee->getBuiltinID() != 0;
        if (declaredSet(callee, set) || lookup(name, set)) {
            leaf(call->getBeginLoc(), set, name);
        } else {
            leaf(call->getBeginLoc(), builtin ? None : C, name); // a builtin the table does not name has no effect
        }
    }

    void leaf(SourceLocation loc, uint32_t set, const std::string &name) {
        if (set == 0) return;
        Edge edge;
        edge.effects = set;
        edge.name = name;
        edge.loc = loc;
        node.edges.push_back(edge);
    }

    void global(const Expr *target, SourceLocation loc) {
        if (const VarDecl *var = writtenGlobal(target))
            leaf(loc, Globals, "a write to `" + var->getNameAsString() + "`");
    }
};

// ---- The whole file ------------------------------------------------------------------------------------------------

class Analysis {
  public:
    explicit Analysis(ASTContext &context) : context(context), engine(context.getDiagnostics()) {
        bad =
            engine.getCustomDiagID(DiagnosticsEngine::Error, "smith-effect: `%0` is %1, but it has the effects %2: %3");
        note = engine.getCustomDiagID(DiagnosticsEngine::Note, "%0");
    }

    void run() {
        collect(context.getTranslationUnitDecl());
        solve();
        for (auto &entry : nodes) {
            Node &node = entry.second;
            if (node.annotated && inMain(node.function)) check(node);
        }
    }

  private:
    ASTContext &context;
    DiagnosticsEngine &engine;
    unsigned bad, note;
    std::map<const FunctionDecl *, Node> nodes;
    std::vector<const FunctionDecl *> order;

    bool inMain(const FunctionDecl *function) {
        const SourceManager &sm = context.getSourceManager();
        return sm.isWrittenInMainFile(sm.getExpansionLoc(function->getLocation()));
    }

    void collect(const DeclContext *scope) {
        for (const Decl *decl : scope->decls()) {
            if (decl->isImplicit()) continue;
            if (const auto *function = dyn_cast<FunctionDecl>(decl)) {
                if (!function->doesThisDeclarationHaveABody() || function->isInvalidDecl() ||
                    function->isDependentContext() || function->isTemplated())
                    continue;
                Node &node = nodes[function];
                node.function = function;
                node.annotated = declaredSet(function, node.declared);
                Collector(node).walk(function->getBody());
                order.push_back(function);
            } else if (const auto *linkage = dyn_cast<LinkageSpecDecl>(decl)) {
                collect(linkage);
            }
        }
    }

    // What a caller sees of a function with a body: its declared set when it has one, else what its body does.
    uint32_t visible(const FunctionDecl *function) {
        auto found = nodes.find(function);
        if (found == nodes.end()) return C; // a body the file could not read (a template, an error)
        return found->second.annotated ? found->second.declared : found->second.inferred;
    }

    // The sets to a fixed point: a set only grows, each pass adds, a cycle settles.
    void solve() {
        for (bool changed = true; changed;) {
            changed = false;
            for (auto &entry : nodes) {
                Node &node = entry.second;
                uint32_t set = node.inferred;
                for (const Edge &edge : node.edges)
                    set |= edge.callee != nullptr ? visible(edge.callee) : edge.effects;
                if (set != node.inferred) {
                    node.inferred = set;
                    changed = true;
                }
            }
        }
    }

    // The first edge of a function whose effects reach `bit` (what a caller of the edge sees).
    const Edge *witness(const Node &node, uint32_t bit) {
        for (const Edge &edge : node.edges) {
            uint32_t set = edge.callee != nullptr ? visible(edge.callee) : edge.effects;
            if ((set & bit) != 0) return &edge;
        }
        return nullptr;
    }

    std::string where(SourceLocation loc) {
        const SourceManager &sm = context.getSourceManager();
        PresumedLoc presumed = sm.getPresumedLoc(sm.getExpansionLoc(loc));
        if (presumed.isInvalid()) return "";
        return llvm::sys::path::filename(presumed.getFilename()).str() + ":" + std::to_string(presumed.getLine());
    }

    // `bad -> log_it (log.c:12) -> printf`, and the effect that ends it.
    std::string chain(const Node &start, uint32_t bit) {
        std::string text = start.function->getNameAsString();
        const Node *node = &start;
        for (int step = 0; step < 64 && node != nullptr; step++) {
            const Edge *edge = witness(*node, bit);
            if (edge == nullptr) break;
            text += " -> " + edge->name + " (" + where(edge->loc) + ")";
            if (edge->callee == nullptr) break;
            auto next = nodes.find(edge->callee);
            node = next != nodes.end() && !next->second.annotated ? &next->second : nullptr;
        }
        return text;
    }

    void check(const Node &node) {
        uint32_t extra = node.inferred & ~node.declared;
        if (extra == 0) return;
        const Edge *first = nullptr;
        uint32_t bit = 0;
        for (const Name &name : names) {
            if ((extra & name.bit) == 0) continue;
            const Edge *found = witness(node, name.bit);
            if (found != nullptr && (first == nullptr || found->loc.getRawEncoding() < first->loc.getRawEncoding())) {
                first = found;
                bit = name.bit;
            }
        }
        if (first == nullptr) return;
        std::string kind = node.declared == 0 ? "[[smith::pure]]" : "limited to " + describe(node.declared);
        engine.Report(first->loc, bad) << node.function->getName() << kind << describe(extra) << chain(node, bit);
        if ((extra & (extra - 1)) != 0)
            engine.Report(first->loc, note) << "this call is the first of the effects beyond the declared ones";
    }
};

class Consumer : public ASTConsumer {
  public:
    void HandleTranslationUnit(ASTContext &context) override {
        Analysis(context).run();
    }
};

struct EffectsPlugin final : PluginASTAction {
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, StringRef) override {
        if (!pluginEnabled) return std::make_unique<ASTConsumer>(); // never null: the multiplexer calls it
        return std::make_unique<Consumer>();
    }
    bool ParseArgs(const CompilerInstance &, const std::vector<std::string> &) override {
        return true;
    }
    ActionType getActionType() override {
        return AddAfterMainAction;
    }
};

const ParsedAttrInfoRegistry::Add<EffectsAttr> effects_attribute("smith-effects-attr", "smith_effects(\"alloc\", ...)");
const ParsedAttrInfoRegistry::Add<EffectAttr<0>> alloc_attribute("smith-alloc-attr", "[[smith::alloc]]");
const ParsedAttrInfoRegistry::Add<EffectAttr<1>> files_attribute("smith-files-attr", "[[smith::files]]");
const ParsedAttrInfoRegistry::Add<EffectAttr<2>> net_attribute("smith-net-attr", "[[smith::net]]");
const ParsedAttrInfoRegistry::Add<EffectAttr<3>> console_attribute("smith-console-attr", "[[smith::console]]");
const ParsedAttrInfoRegistry::Add<EffectAttr<4>> clock_attribute("smith-clock-attr", "[[smith::clock]]");
const ParsedAttrInfoRegistry::Add<EffectAttr<5>> process_attribute("smith-process-attr", "[[smith::process]]");
const ParsedAttrInfoRegistry::Add<EffectAttr<6>> env_attribute("smith-env-attr", "[[smith::env]]");
const ParsedAttrInfoRegistry::Add<EffectAttr<7>> entropy_attribute("smith-entropy-attr", "[[smith::entropy]]");
const ParsedAttrInfoRegistry::Add<EffectAttr<8>> globals_attribute("smith-globals-attr", "[[smith::globals]]");
const ParsedAttrInfoRegistry::Add<EffectAttr<9>> c_attribute("smith-c-attr", "[[smith::c]]");
const ParsedAttrInfoRegistry::Add<PureAttr> pure_attribute("smith-pure-attr", "[[smith::pure]]");
const FrontendPluginRegistry::Add<EffectsPlugin> plugin("smith-effect-inference", "what a function does to the world");

} // namespace

extern "C" void smith_effects_disable() {
    pluginEnabled = false;
}
