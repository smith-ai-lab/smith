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
// trusts), and any other function without a body is `c`, every effect. A call through a function pointer is `c`, as is
// an Objective-C message; inline assembly is read by the assembler (effects_asm.h); writing a global or a
// function-local static is `globals`. A call that stops the task (a checked index, a contract, `abort`, a trap) is a
// failure point: `fails` (issue #325). An annotation is a ceiling: an inferred effect beyond it is a compile error
// naming the chain
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
#include "clang/AST/Mangle.h"
#include "clang/Basic/ParsedAttrInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "effects_asm.h"
#include "effects_table.h"
#include "names.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

using namespace clang;
using namespace smith_effects;

static bool pluginEnabled = true;

// Where the summary of an object goes (runtime/build_runtime.c): `.smith/effects/<the object's path>.fx` in a project,
// beside the object otherwise.
extern "C" bool smith_effects_summary_path(const char *object, char *out, size_t capacity);

namespace {

// A declaration's name when it is a plain identifier, else empty: a constructor, destructor, operator or conversion
// has no identifier, and `getName()` on it asserts ("Name is not a simple identifier").
llvm::StringRef plain_name(const NamedDecl *decl) {
    return decl->getDeclName().isIdentifier() ? decl->getName() : llvm::StringRef();
}

constexpr const char *prefix = "smith::effects:";

// The effects a function type states after its parameter list (#396), read from the type of a callee: clang keeps
// `[[clang::annotate_type("smith::effects:alloc")]]` as sugar through a typedef, a field, a parameter and a call.
bool statedByType(QualType type, uint32_t &set) {
    bool found = false;
    set = 0;
    if (const auto *pointer = type->getAs<PointerType>()) type = pointer->getPointeeType();
    for (;;) {
        const Type *node = type.getTypePtr();
        if (const auto *attributed = dyn_cast<AttributedType>(node)) {
            if (const auto *note = dyn_cast_or_null<AnnotateTypeAttr>(attributed->getAttr())) {
                llvm::StringRef text = note->getAnnotation();
                if (text.consume_front(prefix)) {
                    found = true;
                    while (!text.empty()) {
                        auto [word, rest] = text.split(',');
                        for (const Name &name : names)
                            if (word == name.text) set |= name.bit;
                        text = rest;
                    }
                }
            }
            type = attributed->getModifiedType();
        } else if (const auto *named = dyn_cast<TypedefType>(node)) {
            type = named->desugar();
        } else if (const auto *paren = dyn_cast<ParenType>(node)) {
            type = paren->getInnerType();
        } else {
            return found;
        }
    }
}

// ---- The hover table -----------------------------------------------------------------------------------------------

// The line the editor's hover shows for a function with no attribute, by USR: filled at the end of each unit the editor
// parses, read by the provider below on clangd's threads.
std::mutex hoverMutex;
std::map<std::string, std::string> hoverTable;

std::string hoverText(uint32_t set, bool declared) {
    std::string line = set == 0 ? "pure" : "effects: " + describe(set);
    return declared ? line + " (declared)" : line;
}

// The key of a function in the table: its linkage name (the file's name too when only the file sees it).
std::string usrOf(const NamedDecl *decl) {
    ASTContext &context = decl->getASTContext();
    ASTNameGenerator generator(context);
    std::string name = generator.getName(decl);
    if (!name.empty() && name[0] == '\x01') name.erase(0, 1);
    if (!decl->isExternallyVisible()) {
        const SourceManager &sm = context.getSourceManager();
        name += "#" + std::string(sm.getFilename(sm.getExpansionLoc(decl->getLocation())));
    }
    return name;
}

void setHoverLine(const std::string &usr, const std::string &line) {
    std::lock_guard<std::mutex> lock(hoverMutex);
    hoverTable[usr] = line;
}

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
                                              "process, env, entropy, globals, c, fails, code");
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

// ---- The ownership idioms (#720)
// -------------------------------------------------------------------------------------

// What a returned pointer means, written on a function or on a function pointer's typedef (or field, variable): kept as
// an AnnotateAttr "smith::owns:owned:free", "smith::owns:borrowed:1" or "smith::owns:internal", recorded in the summary
// (`O` and `T` lines), and checked for nothing yet. The same meaning on a function pointer *type* is
// `[[clang::annotate_type("smith::owns:internal")]]` after the parameter list, which clang keeps as it keeps effects.
constexpr const char *ownsPrefix = "smith::owns:";

// The function type of a declaration that may carry an ownership attribute: the function itself, or the function a
// pointer (through typedefs) points to.
const FunctionType *ownerFunctionType(const Decl *decl) {
    QualType type;
    if (const auto *function = dyn_cast<FunctionDecl>(decl)) {
        type = function->getType();
    } else if (const auto *typedefDecl = dyn_cast<TypedefNameDecl>(decl)) {
        type = typedefDecl->getUnderlyingType();
    } else if (const auto *value = dyn_cast<ValueDecl>(decl)) {
        type = value->getType();
    } else {
        return nullptr;
    }
    if (const auto *pointer = type->getAs<PointerType>()) type = pointer->getPointeeType();
    return type->getAs<FunctionType>();
}

bool isSliceType(QualType type) {
    const auto *record = type->getAs<RecordType>();
    return record != nullptr && record->getDecl()->getName().starts_with("smith_array");
}

bool isPointerOrSlice(QualType type) {
    return type->isPointerType() || isSliceType(type);
}

struct OwnershipAttr : ParsedAttrInfo {
    const char *scoped;
    const char *plain;
    explicit OwnershipAttr(const char *scoped, const char *plain, unsigned optArgs) : scoped(scoped), plain(plain) {
        OptArgs = optArgs;
        AcceptsExprPack = 0;
        allowIdentifiers(*this, 0);
        spellings[0] = {ParsedAttr::AS_C23, scoped};
        spellings[1] = {ParsedAttr::AS_CXX11, scoped};
        spellings[2] = {ParsedAttr::AS_GNU, plain};
        Spellings = spellings;
    }
    bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr, const Decl *D) const override {
        if (isa<FunctionDecl>(D) || ownerFunctionType(D) != nullptr) return true;
        S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type)
            << Attr << Attr.isRegularKeywordAttribute() << ExpectedFunction;
        return false;
    }
    // The text after `smith::owns:` for this attribute, or empty after an error was reported.
    virtual std::string describe(Sema &S, const ParsedAttr &Attr, const FunctionType *type) const = 0;

    AttrHandling handleDeclAttribute(Sema &S, Decl *D, const ParsedAttr &Attr) const override {
        auto fail = [&](const std::string &message) {
            unsigned id = S.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "smith-ownership: %0");
            S.Diag(Attr.getLoc(), id) << message;
            return AttributeNotApplied;
        };
        const FunctionType *type = ownerFunctionType(D);
        if (type == nullptr) return fail("an ownership attribute is for a function or a function pointer");
        if (!isPointerOrSlice(type->getReturnType()))
            return fail(std::string("`") + scoped + "` is for a function that returns a pointer or a slice, and this " +
                        "one returns `" + type->getReturnType().getAsString() + "`");
        for (const auto *other : D->specific_attrs<AnnotateAttr>())
            if (other->getAnnotation().starts_with(ownsPrefix))
                return fail("a declaration has at most one ownership attribute (returns_owned, returns_borrowed or "
                            "returns_internal)");
        std::string text = describe(S, Attr, type);
        if (text.empty()) return AttributeNotApplied;
        D->addAttr(AnnotateAttr::Create(S.Context, std::string(ownsPrefix) + text, nullptr, 0, Attr.getRange()));
        return AttributeApplied;
    }
    ParsedAttrInfo::Spelling spellings[3];
    std::string error(Sema &S, const ParsedAttr &Attr, const std::string &message) const {
        unsigned id = S.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "smith-ownership: %0");
        S.Diag(Attr.getLoc(), id) << message;
        return "";
    }
};

// `[[smith::returns_owned(free)]]`: the caller owns the memory and frees it with `free`, a declared function that
// takes one pointer.
struct ReturnsOwnedAttr final : OwnershipAttr {
    ReturnsOwnedAttr() : OwnershipAttr("smith::returns_owned", "smith_returns_owned", 1) {}
    std::string describe(Sema &S, const ParsedAttr &Attr, const FunctionType *) const override {
        if (Attr.getNumArgs() != 1)
            return error(S, Attr, "`returns_owned` takes the function that frees: `returns_owned(free)`");
        std::string written;
        if (Attr.isArgIdent(0)) {
            written = Attr.getArgAsIdent(0)->getIdentifierInfo()->getName().str();
        } else if (const auto *literal = dyn_cast_or_null<StringLiteral>(Attr.getArgAsExpr(0))) {
            written = literal->getString().str();
        } else {
            return error(S, Attr, "`returns_owned` takes the name of the function that frees");
        }
        IdentifierInfo *name = &S.Context.Idents.get(written);
        LookupResult found(S, DeclarationName(name), Attr.getLoc(), Sema::LookupOrdinaryName);
        S.LookupName(found, S.TUScope);
        const FunctionDecl *release = found.empty() ? nullptr : dyn_cast<FunctionDecl>(found.getRepresentativeDecl());
        if (release == nullptr)
            return error(S, Attr, "`returns_owned(" + written + ")`: `" + written + "` is not a declared function");
        if (release->getNumParams() != 1 || !release->getParamDecl(0)->getType()->isPointerType())
            return error(S, Attr, "`returns_owned(" + written + ")`: `" + written + "` must take one pointer");
        return "owned:" + written;
    }
};

// `[[smith::returns_borrowed(n)]]`: the result points into argument n (1-based), a pointer or a slice.
struct ReturnsBorrowedAttr final : OwnershipAttr {
    ReturnsBorrowedAttr() : OwnershipAttr("smith::returns_borrowed", "smith_returns_borrowed", 1) {}
    std::string describe(Sema &S, const ParsedAttr &Attr, const FunctionType *type) const override {
        std::optional<llvm::APSInt> number;
        if (Attr.getNumArgs() == 1 && Attr.isArgExpr(0) && Attr.getArgAsExpr(0) != nullptr)
            number = Attr.getArgAsExpr(0)->getIntegerConstantExpr(S.Context);
        if (!number) return error(S, Attr, "`returns_borrowed` takes the number of a parameter: `returns_borrowed(1)`");
        llvm::APSInt value = *number;
        const auto *proto = type->getAs<FunctionProtoType>();
        unsigned count = proto == nullptr ? 0 : proto->getNumParams();
        if (value < 1 || value > count)
            return error(S, Attr,
                         "`returns_borrowed(" + std::to_string(value.getExtValue()) + ")`: there is no parameter " +
                             std::to_string(value.getExtValue()) + " (the function has " + std::to_string(count) + ")");
        QualType parameter = proto->getParamType(static_cast<unsigned>(value.getExtValue()) - 1);
        if (!isPointerOrSlice(parameter))
            return error(S, Attr,
                         "`returns_borrowed(" + std::to_string(value.getExtValue()) + ")`: parameter " +
                             std::to_string(value.getExtValue()) + " is `" + parameter.getAsString() +
                             "`, not a pointer or a slice");
        return "borrowed:" + std::to_string(value.getExtValue());
    }
};

// `[[smith::returns_internal]]`: the function's own memory, never freed by the caller.
struct ReturnsInternalAttr final : OwnershipAttr {
    ReturnsInternalAttr() : OwnershipAttr("smith::returns_internal", "smith_returns_internal", 0) {}
    std::string describe(Sema &, const ParsedAttr &, const FunctionType *) const override {
        return "internal";
    }
};

// The ownership a declaration states, "owned:free", "borrowed:1" or "internal" (empty when none): from its own
// attribute, or from the annotation clang keeps on a function pointer type.
std::string ownershipOf(const Decl *decl) {
    for (const Decl *redecl : decl->redecls())
        for (const auto *attr : redecl->specific_attrs<AnnotateAttr>())
            if (attr->getAnnotation().starts_with(ownsPrefix))
                return attr->getAnnotation().substr(strlen(ownsPrefix)).str();
    if (const auto *named = dyn_cast<TypedefNameDecl>(decl)) {
        QualType type = named->getUnderlyingType();
        if (const auto *pointer = type->getAs<PointerType>()) type = pointer->getPointeeType();
        for (;;) {
            const Type *node = type.getTypePtr();
            if (const auto *attributed = dyn_cast<AttributedType>(node)) {
                if (const auto *note = dyn_cast_or_null<AnnotateTypeAttr>(attributed->getAttr()))
                    if (note->getAnnotation().starts_with(ownsPrefix))
                        return note->getAnnotation().substr(strlen(ownsPrefix)).str();
                type = attributed->getModifiedType();
            } else if (const auto *paren = dyn_cast<ParenType>(node)) {
                type = paren->getInnerType();
            } else if (const auto *alias = dyn_cast<TypedefType>(node)) {
                type = alias->desugar();
            } else {
                break;
            }
        }
    }
    return "";
}

// ---- What a body does ----------------------------------------------------------------------------------------------

// One call (or other effect) of a function: what it reaches, where, and the callee when the file defines it.
struct Edge {
    const FunctionDecl *callee = nullptr; // a function with a body in this file (its definition)
    uint32_t effects = 0;                 // a leaf: the effects of the raw call (callee null)
    std::string name;                     // what the leaf or callee is called
    SourceLocation loc;
    std::string symbol;      // the callee's linkage name, for a call the summary resolves across files
    bool unresolved = false; // a project function the file has no body for: no effect here, see the summary
    bool tail = false;       // `[[clang::musttail]] return f(...);`: a jump, which makes a loop of a cycle
};

// A place a function pointer is made or called, for the report-only mode of the pointer rule (#396): a call through a
// pointer (target null) or a named function turned into a pointer (assigned, passed, put in a table).
struct PointerUse {
    SourceLocation loc;
    const FunctionDecl *target = nullptr;
    bool stated =
        false; // a call whose function type states its effects: `[[clang::annotate_type("smith::effects:...")]]`
    uint32_t statedSet = 0; // the set it states (none for `smith::effects:`)
};

struct Node {
    const FunctionDecl *function = nullptr;
    std::vector<PointerUse> pointers;
    std::vector<Edge> edges;
    uint32_t inferred = 0;
    uint32_t declared = 0;
    bool annotated = false;
    bool grows = false;   // a variable-length array or alloca: the frame is not one size
    bool trusted = false; // the body calls smith_trusted(): a promise the prover and this check do not examine
    std::string symbol;
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
    using Symbols = std::function<std::string(const FunctionDecl *)>;

    Collector(Node &node, ASTContext &context, Symbols symbols, Symbols names,
              std::vector<const StaticAssertDecl *> &asserts)
        : node(node), context(context), sm(context.getSourceManager()), symbols(std::move(symbols)),
          names(std::move(names)), asserts(asserts) {}

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
    ASTContext &context;
    const SourceManager &sm;
    Symbols symbols;
    Symbols names;                                  // how a function is shown in a chain
    std::vector<const StaticAssertDecl *> &asserts; // the static_asserts of the body, for effects::within
    std::set<const Expr *> callees;   // the callee expressions of the calls met, which are not conversions to a pointer
    std::set<const CallExpr *> tails; // the calls of `[[clang::musttail]] return` statements, met before the calls

    // The call a `[[clang::musttail]] return f(...);` makes, remembered: an attributed statement is visited before its
    // children.
    void noteTail(const AttributedStmt *attributed) {
        bool musttail = false;
        for (const Attr *attribute : attributed->getAttrs())
            musttail = musttail || isa<MustTailAttr>(attribute);
        const auto *returned = dyn_cast<ReturnStmt>(attributed->getSubStmt());
        if (!musttail || returned == nullptr || returned->getRetValue() == nullptr) return;
        if (const auto *call = dyn_cast<CallExpr>(returned->getRetValue()->IgnoreParenImpCasts())) tails.insert(call);
    }

    void visit(const Stmt *statement) {
        if (const auto *attributed = dyn_cast<AttributedStmt>(statement)) {
            noteTail(attributed);
        } else if (const auto *declarations = dyn_cast<DeclStmt>(statement)) {
            for (const Decl *decl : declarations->decls()) {
                if (const auto *assertion = dyn_cast<StaticAssertDecl>(decl)) asserts.push_back(assertion);
                if (const auto *var = dyn_cast<VarDecl>(decl))
                    node.grows = node.grows || var->getType()->isVariablyModifiedType();
            }
        } else if (const auto *cast = dyn_cast<ImplicitCastExpr>(statement)) {
            if (cast->getCastKind() == CK_FunctionToPointerDecay && callees.count(cast) == 0) {
                if (const auto *ref = dyn_cast<DeclRefExpr>(cast->getSubExpr()->IgnoreParens()))
                    if (const auto *target = dyn_cast<FunctionDecl>(ref->getDecl()))
                        node.pointers.push_back({cast->getBeginLoc(), target});
            }
        } else if (const auto *call = dyn_cast<CallExpr>(statement)) {
            callees.insert(call->getCallee()->IgnoreParens());
            if (const FunctionDecl *callee = call->getDirectCallee()) {
                llvm::StringRef called = plain_name(callee);
                node.grows = node.grows || called == "alloca" || called == "__builtin_alloca" ||
                             called == "__builtin_alloca_with_align";
            }
            onCall(call);
        } else if (isa<ObjCMessageExpr>(statement)) {
            leaf(statement->getBeginLoc(), C, "an Objective-C message");
        } else if (const auto *assembly = dyn_cast<AsmStmt>(statement)) {
            onAsm(assembly);
        } else if (const auto *op = dyn_cast<BinaryOperator>(statement)) {
            if (op->isAssignmentOp()) global(op->getLHS(), op->getOperatorLoc());
        } else if (const auto *unary = dyn_cast<UnaryOperator>(statement)) {
            if (unary->isIncrementDecrementOp()) global(unary->getSubExpr(), unary->getOperatorLoc());
        }
    }

    void onCall(const CallExpr *call) {
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr) {
            PointerUse use{call->getBeginLoc(), nullptr};
            use.stated = statedByType(call->getCallee()->getType(), use.statedSet);
            node.pointers.push_back(use);
            leaf(call->getBeginLoc(), C, "a call through a function pointer");
            return;
        }
        if (plain_name(callee) == "smith_trusted") node.trusted = true;
        if (const FunctionDecl *definition = callee->getDefinition()) {
            Edge edge;
            edge.callee = definition;
            edge.name = names(definition);
            edge.loc = call->getBeginLoc();
            edge.symbol = symbols(definition);
            edge.tail = tails.count(call) != 0;
            node.edges.push_back(edge);
            return;
        }
        std::string name = callee->getNameAsString();
        uint32_t set = 0;
        bool builtin = llvm::StringRef(name).starts_with("__builtin_") || callee->getBuiltinID() != 0;
        if (declaredSet(callee, set) || lookup(name, set)) {
            if ((name == "mmap" || name == "mprotect") && call->getNumArgs() >= 3) {
                // The protection is the third argument of both.
                Expr::EvalResult result;
                const Expr *protection = call->getArg(2);
                if (!protection->isValueDependent() && protection->EvaluateAsInt(result, context) &&
                    executable(name, result.Val.getInt().getExtValue()))
                    set |= Code;
            }
            leaf(call->getBeginLoc(), set, name);
        } else if (!builtin && !sm.isInSystemHeader(callee->getLocation())) {
            // A function of the project with no body here: another file may define it, and the summary says what it
            // does once the program is linked. Until then it adds nothing.
            Edge edge;
            edge.unresolved = true;
            edge.name = names(callee);
            edge.loc = call->getBeginLoc();
            edge.symbol = symbols(callee);
            node.edges.push_back(edge);
        } else {
            leaf(call->getBeginLoc(), builtin ? None : C, name); // a builtin the table does not name has no effect
        }
    }

    // An `asm` statement: the assembler reads it (shim/effects_asm.h), and each thing it does is a call: a system call
    // by its number, a call or jump to a symbol, an indirect call. A block it cannot read is `c`.
    void onAsm(const AsmStmt *assembly) {
        SourceLocation loc = assembly->getAsmLoc();
        AsmReading reading = readAsm(assembly, context);
        if (!reading.parsed) {
            leaf(loc, C, "inline assembly");
            return;
        }
        llvm::Triple triple = context.getTargetInfo().getTriple();
        for (const AsmSyscall &call : reading.syscalls) {
            std::string name = call.number ? syscallName(triple, *call.number) : "";
            uint32_t set = 0;
            if (name.empty() || !lookup(name, set)) {
                leaf(loc, C, "a system call with a number the table does not know");
                continue;
            }
            if (executable(name, call.protection)) set |= Code;
            leaf(loc, set, "the system call `" + name + "` in inline assembly");
        }
        for (const std::string &called : reading.calls)
            onSymbol(loc, called);
        if (reading.indirect) leaf(loc, C, "a call through a register in inline assembly");
    }

    // A call from assembly to the symbol `name`: a function of this file, the table's, or an unknown one.
    void onSymbol(SourceLocation loc, const std::string &name) {
        auto found = context.getTranslationUnitDecl()->lookup(&context.Idents.get(name));
        for (const NamedDecl *decl : found) {
            const auto *function = dyn_cast<FunctionDecl>(decl);
            if (function == nullptr) continue;
            uint32_t set = 0;
            if (const FunctionDecl *definition = function->getDefinition()) {
                Edge edge;
                edge.callee = definition;
                edge.name = names(definition);
                edge.loc = loc;
                edge.symbol = symbols(definition);
                node.edges.push_back(edge);
                return;
            }
            if (declaredSet(function, set)) {
                leaf(loc, set, name);
                return;
            }
        }
        uint32_t set = 0;
        leaf(loc, lookup(name, set) ? set : C, name);
    }

    // Whether a call of mmap or mprotect asks for memory that can run: a constant protection with PROT_EXEC (4).
    static bool executable(llvm::StringRef name, std::optional<int64_t> protection) {
        return (name == "mmap" || name == "mprotect") && protection && (static_cast<uint64_t>(*protection) & 4U) != 0;
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

// ---- The editor ----------------------------------------------------------------------------------------------------
//
// `smith lsp` (shim/lsp.cpp) runs clangd in this process and tells the plugin what it learns from the editor: that it
// is the editor's parse (the summaries of other files are judged), the object each source is built into (where its
// summary goes) and which source was just saved (its parse writes the summary). When a summary is written the layer
// is told, to have the files that call into it parsed again.

std::mutex editorMutex;
bool editorOn = false;
std::map<std::string, std::string> editorObjects; // canonical source -> object path
std::set<std::string> editorSaved;                // canonical sources saved and not yet written
void (*editorListener)(const char *) = nullptr;

std::string canonical(const std::string &path) {
    llvm::SmallString<256> real;
    if (llvm::sys::fs::real_path(path, real)) return path;
    return std::string(real);
}

bool editorPendingFlag = false;

bool editorPending() {
    std::lock_guard<std::mutex> lock(editorMutex);
    return editorPendingFlag;
}

bool editorActive() {
    std::lock_guard<std::mutex> lock(editorMutex);
    return editorOn;
}

// The object of a source saved since its summary was last written; "" for any other parse.
std::string savedObject(const std::string &source) {
    std::lock_guard<std::mutex> lock(editorMutex);
    auto saved = editorSaved.find(source);
    auto object = editorObjects.find(source);
    if (saved == editorSaved.end() || object == editorObjects.end()) return "";
    editorSaved.erase(saved);
    return object->second;
}

void editorWritten(const std::string &source) {
    void (*listener)(const char *) = nullptr;
    {
        std::lock_guard<std::mutex> lock(editorMutex);
        listener = editorListener;
    }
    if (listener != nullptr) listener(source.c_str());
}

struct EditorViolation {
    std::string name, place, kind, extra, chain, message;
    int ordinal;
};
struct EditorHover {
    std::string symbol;
    uint32_t set;
    bool declared;
};
std::vector<EditorViolation> editorCheck(const std::string &label, const std::string &own, const std::string &folder,
                                         std::vector<EditorHover> &hovers);

class Analysis {
  public:
    Analysis(ASTContext &context, std::string output, std::string flags, bool own, bool syntaxOnly)
        : context(context), engine(context.getDiagnostics()), generator(context), syntaxOnly(syntaxOnly),
          output(std::move(output)), ownSizes(own), flags(std::move(flags)) {
        bad =
            engine.getCustomDiagID(DiagnosticsEngine::Error, "smith-effect: `%0` is %1, but it has the effects %2: %3");
        note = engine.getCustomDiagID(DiagnosticsEngine::Note, "%0");
        assertion = engine.getCustomDiagID(
            DiagnosticsEngine::Error,
            "smith-effect: static assertion failed: %0: `%1` has the effects %2 beyond the allowed ones: %3");
        misuse = engine.getCustomDiagID(DiagnosticsEngine::Error,
                                        "smith-effect: effects::within takes the name of a function and a constant set "
                                        "of effects, as `effects::within(f, effects::alloc)`");
        recursion = engine.getCustomDiagID(
            DiagnosticsEngine::Error, "smith-effect: recursion in a module: %0 (write a loop; a call that ends the "
                                      "function can be `[[clang::musttail]] return f(...);`)");
    }

    void run() {
        collect(context.getTranslationUnitDecl());
        solve();
        if (pointerReport()) reportPointers();
        for (const FunctionDecl *function : order) { // source order: the map's order is the pointers'
            Node &node = nodes[function];
            if (node.annotated && inMain(node.function)) check(node);
        }
        if (isModule()) checkRecursion();
        checkAssertions();
        attachHover();
        writeSummary();
        if (syntaxOnly && output.empty() && getenv("SMITH_EFFECTS_OBJECT") == nullptr) checkInEditor();
    }

  private:
    ASTContext &context;
    DiagnosticsEngine &engine;
    unsigned bad, note, recursion, assertion, misuse;
    ASTNameGenerator generator;
    bool syntaxOnly;       // a parse that only reads the file (the editor's, not the preamble's)
    std::string output;    // the object file being written: the summary goes beside it, `<object>.fx`
    bool ownSizes = false; // the .su is smith's: removed after it is read
    std::string flags;     // what the frame sizes are for: `opt=0 sanitize=address,undefined`
    std::map<const FunctionDecl *, Node> nodes;
    std::vector<const FunctionDecl *> order;
    std::vector<const StaticAssertDecl *> asserts;

    // A `static_assert(effects::within(f, mask), "...")` of the file whose set cannot be judged here: `f` has no body
    // in this unit and is the project's, so the program's check judges it (a `W` line of the summary).
    struct Deferred {
        std::string target, name, message;
        uint32_t mask;
        SourceLocation loc;
    };
    std::vector<Deferred> deferred;

    // A promise smith trusts and does not check (a `P` line of the summary, listed by `smith check`): an effects
    // attribute on a declaration with no source, or a function that calls `smith_trusted()`.
    struct Promise {
        char kind; // e an effects promise, t a trusted function
        std::string symbol, name;
        SourceLocation loc;
    };
    std::vector<Promise> promises;
    std::set<std::string> promised; // one line a function, however many declarations it has

    // The linkage name a call to the function is known by in another file (the asm label of a module's export
    // included); a function with internal linkage is only this file's, so its name carries the file's.
    std::string symbolOf(const FunctionDecl *function) {
        std::string name = generator.getName(function);
        if (!name.empty() && name[0] == '\x01') name.erase(0, 1);
        if (!function->isExternallyVisible()) {
            const SourceManager &sm = context.getSourceManager();
            name = sm.getFileEntryRefForID(sm.getMainFileID())
                       ? sm.getFileEntryRefForID(sm.getMainFileID())->getName().str() + "#" + name
                       : name;
        }
        return name;
    }

    // The name to show: an export of a module is known by its written path (`fx::lib::noisy`, the asm label the
    // lowering gives it), anything else by its own name.
    std::string shownName(const FunctionDecl *function) {
        std::string symbol = symbolOf(function);
        size_t path = symbol.find("::");
        if (path != std::string::npos && symbol.find('#') == std::string::npos)
            return symbol.substr(symbol[0] == '_' ? 1 : 0);
        std::string name = function->getNameAsString();
        return writtenName(name);
    }

    // The name as a `.smith` file writes it (`bad`, not the `main_bad` the lowering made), when the lowering's table of
    // the file is there: every tool shows the names as written, the link's error included, so the summary carries it.
    std::string writtenName(const std::string &lowered) {
        const SourceManager &sm = context.getSourceManager();
        auto mainFile = sm.getFileEntryRefForID(sm.getMainFileID());
        if (!mainFile) return lowered;
        std::shared_ptr<const smith::FileNames> table = smith::namesOf(mainFile->getName().str());
        if (table == nullptr) return lowered;
        const smith::WrittenName *found = table->find(lowered);
        return found == nullptr || found->written.empty() ? lowered : found->written;
    }

    bool inMain(const FunctionDecl *function) {
        const SourceManager &sm = context.getSourceManager();
        return sm.isWrittenInMainFile(sm.getExpansionLoc(function->getLocation()));
    }

    void notePromise(const FunctionDecl *function) {
        const SourceManager &sm = context.getSourceManager();
        if (function->isInvalidDecl() || function->isTemplated() || sm.isInSystemHeader(function->getLocation()))
            return;
        auto node = nodes.find(function);
        if (!promised.insert(symbolOf(function)).second) return;
        if (node != nodes.end()) {
            if (node->second.trusted)
                promises.push_back({'t', node->second.symbol, shownName(function), function->getLocation()});
            return;
        }
        uint32_t set = 0;
        if (function->doesThisDeclarationHaveABody() || function->getDefinition() != nullptr ||
            !declaredSet(function, set))
            return;
        promises.push_back({'e', symbolOf(function), shownName(function), function->getLocation()});
    }

    // An ownership idiom (#720) a function or a typedef states: one `O` or `T` line of the summary, once a symbol.
    struct Ownership {
        char kind; // O a function, T a typedef of a function pointer
        std::string symbol, idiom;
        SourceLocation loc;
    };
    std::vector<Ownership> ownerships;
    std::set<std::string> owned;

    void noteOwnership(const Decl *decl) {
        const auto *function = dyn_cast<FunctionDecl>(decl);
        const auto *named = dyn_cast<TypedefNameDecl>(decl);
        if (function == nullptr && named == nullptr) return;
        const SourceManager &sm = context.getSourceManager();
        if (sm.isInSystemHeader(decl->getLocation())) return;
        std::string idiom = ownershipOf(decl);
        if (idiom.empty()) return;
        std::string symbol = function != nullptr ? symbolOf(function) : cast<NamedDecl>(decl)->getNameAsString();
        if (!owned.insert(symbol).second) return;
        ownerships.push_back({function != nullptr ? 'O' : 'T', symbol, idiom, decl->getLocation()});
    }

    void collect(const DeclContext *scope) {
        for (const Decl *decl : scope->decls()) {
            if (decl->isImplicit()) continue;
            noteOwnership(decl);
            if (const auto *function = dyn_cast<FunctionDecl>(decl)) {
                if (!function->doesThisDeclarationHaveABody() || function->isInvalidDecl() ||
                    function->isDependentContext() || function->isTemplated()) {
                    notePromise(function);
                    continue;
                }
                Node &node = nodes[function];
                node.function = function;
                node.annotated = declaredSet(function, node.declared);
                node.symbol = symbolOf(function);
                Collector(
                    node, context, [this](const FunctionDecl *f) { return symbolOf(f); },
                    [this](const FunctionDecl *f) { return shownName(f); }, asserts)
                    .walk(function->getBody());
                order.push_back(function);
                notePromise(function);
            } else if (const auto *assertion = dyn_cast<StaticAssertDecl>(decl)) {
                asserts.push_back(assertion);
            } else if (const auto *var = dyn_cast<VarDecl>(decl)) {
                if (pointerReport() && var->hasInit() && var->hasGlobalStorage() && !var->isInvalidDecl()) {
                    Node &holder = initializers.emplace_back();
                    Collector(
                        holder, context, [this](const FunctionDecl *f) { return symbolOf(f); },
                        [this](const FunctionDecl *f) { return shownName(f); }, asserts)
                        .walk(var->getInit());
                    holder.symbol = var->getNameAsString();
                }
            } else if (const auto *linkage = dyn_cast<LinkageSpecDecl>(decl)) {
                collect(linkage);
            }
        }
    }

    // What a caller sees of a function with a body: its declared set when it has one, else what its body does.
    std::deque<Node> initializers; // the initializers of globals, walked only to report function pointers

    // The report-only mode of the pointer rule (issue #396): `SMITH_EFFECTS_POINTERS=<file>` appends one line for each
    // place a function pointer is called (the effect today is `c`) or made from a named function, with the effects that
    // function has, i.e. the set the pointer's type would have to state. Nothing is reported to the compiler.
    static bool pointerReport() {
        return getenv("SMITH_EFFECTS_POINTERS") != nullptr;
    }

    uint32_t pointedEffects(const FunctionDecl *target) {
        if (const FunctionDecl *definition = target->getDefinition()) return visible(definition);
        uint32_t set = 0;
        if (declaredSet(target, set) || lookup(target->getNameAsString(), set)) return set;
        return C;
    }

    void reportPointers() {
        const SourceManager &sm = context.getSourceManager();
        std::string text;
        auto lines = [&](const Node &node, const std::string &where) {
            for (const PointerUse &use : node.pointers) {
                SourceLocation expansion = sm.getExpansionLoc(use.loc);
                if (!sm.isWrittenInMainFile(expansion)) continue;
                PresumedLoc presumed = sm.getPresumedLoc(expansion);
                text += std::string(use.target == nullptr ? "call" : "make") + "\t" + presumed.getFilename() + ":" +
                        std::to_string(presumed.getLine()) + "\t" + where + "\t";
                text += use.target == nullptr ? "-" : use.target->getNameAsString();
                text += "\t" + (use.target == nullptr ? std::string("c") : describe(pointedEffects(use.target)));
                // The last column: the effects the pointer's type states, `-` for a type that states none (it is `c`).
                text += "\t" +
                        (use.stated ? (use.statedSet == 0 ? std::string("pure") : describe(use.statedSet))
                                    : std::string("-")) +
                        "\n";
            }
        };
        for (const FunctionDecl *function : order)
            lines(nodes[function], function->getNameAsString());
        for (const Node &node : initializers)
            lines(node, "(initializer of " + node.symbol + ")");
        if (text.empty()) return;
        std::string path = getenv("SMITH_EFFECTS_POINTERS");
        FILE *file = fopen(path.c_str(), "a");
        if (file == nullptr) return;
        fwrite(text.data(), 1, text.size(), file);
        fclose(file);
    }

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
                    set |= edge.unresolved ? 0u : edge.callee != nullptr ? visible(edge.callee) : edge.effects;
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
            uint32_t set = edge.unresolved ? 0u : edge.callee != nullptr ? visible(edge.callee) : edge.effects;
            if ((set & bit) != 0) return &edge;
        }
        return nullptr;
    }

    // Whether the main file is a module: a `module a::b;` line begins a line of it.
    bool isModule() {
        const SourceManager &sm = context.getSourceManager();
        llvm::StringRef text = sm.getBufferData(sm.getMainFileID());
        for (size_t at = text.find("module "); at != llvm::StringRef::npos; at = text.find("module ", at + 1))
            if (at == 0 || text[at - 1] == '\n') return true;
        return false;
    }

    // No recursion in a module: a call that sits on a cycle of the functions the file defines is an error naming the
    // cycle, unless every call of the cycle is a `[[clang::musttail]]` return (a jump, which is a loop). Calls through
    // a pointer and into code with no body are not followed (they are `c`). Imports are acyclic, so a cycle is always
    // inside one module, and one file says it.
    void checkRecursion() {
        std::vector<const Node *> list;
        std::map<const FunctionDecl *, size_t> index;
        for (const FunctionDecl *function : order) {
            if (!inMain(function)) continue;
            index[function] = list.size();
            list.push_back(&nodes[function]);
        }
        size_t count = list.size();
        std::vector<std::vector<size_t>> next(count);
        for (size_t i = 0; i < count; i++)
            for (const Edge &edge : list[i]->edges) {
                auto found = edge.callee == nullptr ? index.end() : index.find(edge.callee);
                if (found != index.end()) next[i].push_back(found->second);
            }
        // Strongly connected components, with a work list in place of the recursion.
        std::vector<long> number(count, -1), low(count, 0), component(count, -1);
        std::vector<bool> open(count, false);
        std::vector<size_t> stack;
        long counter = 0, components = 0;
        for (size_t root = 0; root < count; root++) {
            if (number[root] >= 0) continue;
            std::vector<std::pair<size_t, size_t>> work{{root, 0}};
            number[root] = low[root] = counter++;
            stack.push_back(root);
            open[root] = true;
            while (!work.empty()) {
                size_t node = work.back().first;
                size_t &at = work.back().second;
                if (at < next[node].size()) {
                    size_t to = next[node][at++];
                    if (number[to] < 0) {
                        number[to] = low[to] = counter++;
                        stack.push_back(to);
                        open[to] = true;
                        work.push_back({to, 0});
                    } else if (open[to]) {
                        low[node] = std::min(low[node], number[to]);
                    }
                    continue;
                }
                if (low[node] == number[node]) {
                    for (size_t member = count; member != node;) {
                        member = stack.back();
                        stack.pop_back();
                        open[member] = false;
                        component[member] = components;
                    }
                    components++;
                }
                work.pop_back();
                if (!work.empty()) low[work.back().first] = std::min(low[work.back().first], low[node]);
            }
        }
        std::vector<bool> said(static_cast<size_t>(components), false);
        for (size_t i = 0; i < count; i++)
            for (const Edge &edge : list[i]->edges) {
                auto found = edge.callee == nullptr ? index.end() : index.find(edge.callee);
                if (found == index.end() || edge.tail || component[found->second] != component[i]) continue;
                if (said[static_cast<size_t>(component[i])]) continue;
                said[static_cast<size_t>(component[i])] = true;
                engine.Report(edge.loc, recursion) << cycleText(list, index, i, edge, found->second);
            }
    }

    // `a -> b (a.smith:4) -> a (b.smith:9)`: the call that closes a cycle, and the way back from its callee.
    std::string cycleText(const std::vector<const Node *> &list, const std::map<const FunctionDecl *, size_t> &index,
                          size_t from, const Edge &call, size_t to) {
        std::string text = shownName(list[from]->function);
        text += " -> " + call.name + " (" + where(call.loc) + ")";
        if (to == from) return text;
        // The shortest way back from `to` to `from`, by a search over the calls (tail calls too).
        std::vector<long> before(list.size(), -1);
        std::vector<const Edge *> via(list.size(), nullptr);
        std::vector<size_t> queue{to};
        before[to] = static_cast<long>(to);
        for (size_t head = 0; head < queue.size() && before[from] < 0; head++)
            for (const Edge &edge : list[queue[head]]->edges) {
                auto found = edge.callee == nullptr ? index.end() : index.find(edge.callee);
                if (found == index.end() || before[found->second] >= 0) continue;
                before[found->second] = static_cast<long>(queue[head]);
                via[found->second] = &edge;
                queue.push_back(found->second);
            }
        std::vector<const Edge *> back;
        for (size_t at = from; at != to && before[at] >= 0; at = static_cast<size_t>(before[at]))
            back.push_back(via[at]);
        for (size_t i = back.size(); i-- > 0;)
            text += " -> " + back[i]->name + " (" + where(back[i]->loc) + ")";
        return text;
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

    // `static_assert(effects::within(f, mask), "...")` (skill smith-lang-effects): the lowering and the compiler read
    // the call as true (the function takes any arguments and says yes); the effects of `f` exist only now, so the
    // assertion is judged here. A function of this unit is judged at once, a system function by the table, one
    // with an attribute by it, and a project function with no body here by the program's check (a `W` line).
    void checkAssertions() {
        for (const StaticAssertDecl *decl : asserts) {
            std::vector<const Expr *> work{decl->getAssertExpr()};
            while (!work.empty()) {
                const Expr *expr = work.back()->IgnoreParenImpCasts();
                work.pop_back();
                const auto *call = dyn_cast<CallExpr>(expr);
                const FunctionDecl *callee = call == nullptr ? nullptr : call->getDirectCallee();
                if (callee != nullptr && plain_name(callee) == "smith_effects_within") {
                    judgeWithin(decl, call);
                    continue;
                }
                for (const Stmt *child : expr->children()) {
                    if (const auto *sub = dyn_cast_or_null<Expr>(child)) work.push_back(sub);
                }
            }
        }
    }

    void judgeWithin(const StaticAssertDecl *decl, const CallExpr *call) {
        const Expr *first = call->getNumArgs() == 2 ? call->getArg(0)->IgnoreParenImpCasts() : nullptr;
        if (const auto *address = dyn_cast_or_null<UnaryOperator>(first)) {
            if (address->getOpcode() == UO_AddrOf) first = address->getSubExpr()->IgnoreParenImpCasts();
        }
        const auto *reference = dyn_cast_or_null<DeclRefExpr>(first);
        const FunctionDecl *target = reference == nullptr ? nullptr : dyn_cast<FunctionDecl>(reference->getDecl());
        Expr::EvalResult mask;
        if (target == nullptr || !call->getArg(1)->EvaluateAsInt(mask, context)) {
            engine.Report(call->getBeginLoc(), misuse);
            return;
        }
        uint32_t allowed = static_cast<uint32_t>(mask.Val.getInt().getZExtValue());
        std::string message;
        if (const auto *text = dyn_cast_or_null<StringLiteral>(decl->getMessage())) message = text->getString().str();
        SourceLocation loc = decl->getBeginLoc();
        const FunctionDecl *definition = target->getDefinition();
        auto found = definition == nullptr ? nodes.end() : nodes.find(definition);
        uint32_t set = 0;
        std::string chainText;
        if (found != nodes.end()) {
            const Node &node = found->second;
            set = node.annotated ? node.declared : node.inferred;
            uint32_t extra = set & ~allowed;
            if (extra == 0) return;
            chainText = shownName(node.function);
            if (node.annotated) {
                chainText += " (declared " + describe(node.declared) + ")";
            } else {
                for (const Name &name : names) {
                    if ((extra & name.bit) == 0 || witness(node, name.bit) == nullptr) continue;
                    chainText = chain(node, name.bit);
                    break;
                }
            }
        } else {
            std::string name = target->getNameAsString();
            bool builtin = llvm::StringRef(name).starts_with("__builtin_") || target->getBuiltinID() != 0;
            if (declaredSet(target, set) || lookup(name, set)) {
                chainText = shownName(target) + " (declared or known to the table)";
            } else if (!builtin && !context.getSourceManager().isInSystemHeader(target->getLocation())) {
                std::string clean = message;
                for (char &c : clean) {
                    if (c == '\t' || c == '\n') c = ' ';
                }
                deferred.push_back({symbolOf(target), shownName(target), clean, allowed, loc});
                return;
            } else {
                set = builtin ? None : C;
                chainText = shownName(target) + " (no source)";
            }
        }
        uint32_t extra = set & ~allowed;
        if (extra == 0) return;
        engine.Report(loc, assertion) << message << shownName(target) << describe(extra) << chainText;
    }

    void check(const Node &node) {
        uint32_t extra = node.inferred & ~node.declared;
        // The prover's own compile (SMITH_PROVING) keeps every check as a point to prove: a `fails` ceiling is judged
        // by the build that reads the proof, or the function the ceiling holds could never be proved (issue #325).
        if (getenv("SMITH_PROVING") != nullptr) extra &= ~Fails;
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
        { // the diagnostic is emitted when the builder goes, before the note
            auto report = engine.Report(first->loc, bad);
            report << node.function->getNameAsString() << kind << describe(extra) << chain(node, bit);
            // The fix: the function gets the whole set it needs, declared and inferred. With no attribute one is
            // written
            // before it; with `[[smith::effects(...)]]`, `[[smith::pure]]` or an effect's own attribute that one is
            // replaced (never a second one added).
            std::string whole = describe(node.declared | node.inferred);
            const SourceManager &sm = context.getSourceManager();
            const AnnotateAttr *own = nullptr;
            for (const auto *attribute : node.function->specific_attrs<AnnotateAttr>()) {
                if (own == nullptr && attribute->getAnnotation().starts_with(prefix)) own = attribute;
            }
            if (own != nullptr) {
                SourceRange range = own->getRange();
                if (range.isValid() && !range.getBegin().isMacroID() && sm.isWrittenInMainFile(range.getBegin())) {
                    report << FixItHint::CreateReplacement(CharSourceRange::getTokenRange(range),
                                                           "smith::effects(" + whole + ")");
                }
            } else {
                SourceLocation start = node.function->getBeginLoc();
                if (start.isValid() && !start.isMacroID() && !node.function->isImplicit())
                    report << FixItHint::CreateInsertion(start, "[[smith::effects(" + whole + ")]] ");
            }
        }
        if ((extra & (extra - 1)) != 0)
            engine.Report(first->loc, note) << "this call is the first of the effects beyond the declared ones";
    }

    // `file:line:column` of a place.
    std::string place(SourceLocation loc) {
        const SourceManager &sm = context.getSourceManager();
        PresumedLoc presumed = sm.getPresumedLoc(sm.getExpansionLoc(loc));
        if (presumed.isInvalid()) return "?";
        return std::string(presumed.getFilename()) + ":" + std::to_string(presumed.getLine()) + ":" +
               std::to_string(presumed.getColumn());
    }

    // The frames the backend gave the functions of this file: `-fstack-usage` (which smith adds to its compiles) writes
    // `<object without .o>.su`, one line `file:line:name<TAB>bytes<TAB>static|dynamic|bounded` a function, and this
    // returns one `S` line (symbol, bytes, kind) for each function of the file that has one, then the `H` line with
    // the flags the numbers are for (a frame at -O0 with sanitizers is larger than at -O2). The .su is removed when
    // smith asked for it. An object compiled without it has no `S` lines, and the program's stack is then unknown.
    std::string frames() {
        if (output.size() < 3) return ""; // no object: the editor's parse, a summary-only compile
        std::string base = output.substr(0, output.size() - 2);
        const char *asked = getenv("SMITH_STACK_USAGE_ADDED"); // "1": beside the object; else the file the driver chose
        std::string file = asked != nullptr && asked[0] != '\0' && std::strcmp(asked, "1") != 0 ? asked : base + ".su";
        auto buffer = llvm::MemoryBuffer::getFile(file);
        if (!buffer) return "";
        std::map<std::string, std::pair<std::string, std::string>> sizes;
        llvm::SmallVector<llvm::StringRef, 64> lines;
        (*buffer)->getBuffer().split(lines, '\n', -1, false);
        for (llvm::StringRef line : lines) {
            llvm::SmallVector<llvm::StringRef, 4> field;
            line.split(field, '\t');
            if (field.size() < 3) continue;
            llvm::StringRef head = field[0];
            size_t colon = 0; // the name starts after `:digits:`
            for (size_t at = head.find(':'); at != llvm::StringRef::npos; at = head.find(':', at + 1)) {
                size_t digits = at + 1;
                while (digits < head.size() && head[digits] >= '0' && head[digits] <= '9')
                    digits++;
                if (digits > at + 1 && digits < head.size() && head[digits] == ':') {
                    colon = digits + 1;
                    break;
                }
            }
            if (colon == 0) continue;
            std::string key = head.substr(colon).str();
            if (!key.empty() && key[0] == '\x01') key.erase(0, 1); // an asm label's marker
            sizes[key] = {field[1].str(), field[2].str()};
        }
        if (ownSizes) {
            std::error_code ignored = llvm::sys::fs::remove(file);
            (void)ignored;
        }
        std::string text;
        for (const FunctionDecl *function : order) {
            const Node &node = nodes[function];
            std::string name =
                node.symbol.substr(node.symbol.find('#') == std::string::npos ? 0 : node.symbol.find('#') + 1);
            auto found = sizes.find(name);
            if (found == sizes.end() && !name.empty() && name[0] == '_') found = sizes.find(name.substr(1));
            if (found == sizes.end()) found = sizes.find("_" + name); // a module's export, named by its asm label
            if (found == sizes.end()) continue;
            // The backend calls a frame `dynamic` for more than a run-time size (a sanitizer's realigned arrays), so
            // the kind is what the body says.
            text +=
                "S\t" + node.symbol + "\t" + found->second.first + "\t" + (node.grows ? "dynamic" : "static") + "\n";
        }
        if (text.empty()) return "";
        return text + "H\t" + flags + "\n";
    }

    // What the editor's hover says of each function the file defines without an attribute: the effects it has, kept in
    // a table of the plugin's own (by the declaration's linkage name) for the hover provider below. It is not kept on
    // the declaration: clangd shows every `AnnotateAttr` of a declaration as an "Annotation:" line.
    void attachHover() {
        for (const FunctionDecl *function : order) {
            const Node &node = nodes[function];
            if (node.annotated) continue; // the provider reads a declared set from the attribute itself
            std::string usr = usrOf(function);
            if (!usr.empty()) setHoverLine(usr, hoverText(node.inferred, false));
        }
    }

    // The summary of this file beside its object: one line a function (F: symbol, declared set or -1, what its own body
    // reaches in this file, where, its name) and one for each thing it calls (L a leaf with its effects, E a call to
    // a function by symbol, with a body here or not: the program's check follows it across files). The file is replaced
    // whole, written beside and renamed, so no reader sees half of one.
    std::string summaryText() {
        std::string text;
        for (const FunctionDecl *function : order) { // source order, so the same source writes the same summary
            const Node &node = nodes[function];
            text += "F\t" + node.symbol + "\t" + (node.annotated ? std::to_string(node.declared) : "-1") + "\t" +
                    std::to_string(node.inferred) + "\t" + place(node.function->getLocation()) + "\t" +
                    shownName(node.function) + "\n";
            for (const Edge &edge : node.edges) {
                if (edge.callee == nullptr && !edge.unresolved) {
                    text += "L\t" + node.symbol + "\t" + std::to_string(edge.effects) + "\t" + place(edge.loc) + "\t" +
                            edge.name + "\n";
                } else {
                    text += "E\t" + node.symbol + "\t" + edge.symbol + "\t" + place(edge.loc) + "\t" + edge.name +
                            "\t" + (edge.tail ? "1" : "0") + "\n";
                }
            }
        }
        for (const Promise &promise : promises) {
            text += "P\t" + std::string(1, promise.kind) + "\t" + promise.symbol + "\t" + place(promise.loc) + "\t" +
                    promise.name + "\n";
        }
        for (const Ownership &o : ownerships) { // what a returned pointer means (#720): O a function, T a typedef
            text += std::string(1, o.kind) + "\t" + o.symbol + "\t" + o.idiom + "\t" + place(o.loc) + "\n";
        }
        text += frames(); // the sizes of the frames, when the compile wrote them
        for (const Deferred &w : deferred) {
            text += "W\t" + w.target + "\t" + std::to_string(w.mask) + "\t" + place(w.loc) + "\t" + w.message + "\t" +
                    w.name + "\n";
        }
        return text;
    }

    // `SMITH_EFFECTS_OBJECT` names the object whose summary a compile writes (the editor's background pass compiles a
    // source for its summary only, in the place the build keeps it).
    void writeSummary() {
        const char *named = getenv("SMITH_EFFECTS_OBJECT");
        if (named != nullptr && *named != '\0') {
            writeText(named, summaryText());
            return;
        }
        if (output.empty()) return;
        writeText(output, summaryText());
    }

    // The summary put in the place the object's one goes.
    static bool writeText(const std::string &object, const std::string &text) {
        char where[4096];
        if (!smith_effects_summary_path(object.c_str(), where, sizeof where)) return false;
        std::string path = where;
        std::string temporary = path + ".part" + std::to_string(getpid());
        std::error_code error;
        llvm::StringRef folder = llvm::sys::path::parent_path(path); // empty for an object in the working folder
        if (!folder.empty() && llvm::sys::fs::create_directories(folder)) return false;
        {
            llvm::raw_fd_ostream file(temporary, error);
            if (error) return false;
            file << text;
        }
        if (llvm::sys::fs::rename(temporary, path)) {
            std::error_code ignored =
                llvm::sys::fs::remove(temporary); // a summary that cannot be put in place is left out
            (void)ignored;
            return false;
        }
        return true;
    }

    // The editor's parse of a file (clangd, `smith lsp --check`): when the file was just saved its summary is written
    // (the build's own place, so a build after the editor does no extra work), and the calls that cross files are
    // judged with the summaries of the other files on disk, the way the link judges them: an error at the call with
    // the link's text and chain. The link stays the authority.
    void checkInEditor() {
        if (!editorActive()) return;
        const SourceManager &sm = context.getSourceManager();
        auto mainFile = sm.getFileEntryRefForID(sm.getMainFileID());
        if (!mainFile) return;
        std::string source = canonical(mainFile->getName().str());
        std::string text = summaryText();
        std::string object = savedObject(source);
        if (!object.empty() && writeText(object, text)) editorWritten(source);
        char folder[4096];
        if (!smith_effects_summary_path("x.o", folder, sizeof folder)) return;
        std::string directory = llvm::sys::path::parent_path(folder).str();
        std::vector<EditorHover> hovers;
        std::vector<EditorViolation> found = ::editorCheck("editor:" + source, text, directory, hovers);
        // What hover says of a function that is defined or called here, from the same solve that judged the file, so
        // it does not depend on which file the editor parsed first (the key is the one `usrOf` makes).
        for (const EditorHover &hover : hovers)
            setHoverLine(hover.symbol, hoverText(hover.set, hover.declared));
        for (const EditorViolation &violation : found) {
            SourceLocation loc = locate(violation.place);
            if (loc.isInvalid()) continue;
            engine.Report(loc, bad) << violation.name << violation.kind << violation.extra << violation.chain;
        }
    }

    // A place `path:line:column` as a location of the main file; invalid when it is in another file.
    SourceLocation locate(const std::string &placed) {
        size_t last = placed.rfind(':'), before = last == std::string::npos ? last : placed.rfind(':', last - 1);
        if (before == std::string::npos) return SourceLocation();
        unsigned line = std::strtoul(placed.c_str() + before + 1, nullptr, 10);
        unsigned column = std::strtoul(placed.c_str() + last + 1, nullptr, 10);
        const SourceManager &sm = context.getSourceManager();
        PresumedLoc main = sm.getPresumedLoc(sm.getLocForStartOfFile(sm.getMainFileID()));
        if (main.isInvalid() || placed.substr(0, before) != main.getFilename()) return SourceLocation();
        return sm.translateLineCol(sm.getMainFileID(), line, column);
    }
};

class Consumer : public ASTConsumer {
  public:
    Consumer(std::string output, std::string flags, bool own, bool syntaxOnly)
        : output(std::move(output)), flags(std::move(flags)), own(own), syntaxOnly(syntaxOnly) {}
    void HandleTranslationUnit(ASTContext &context) override {
        Analysis(context, output, flags, own, syntaxOnly).run();
    }

  private:
    std::string output, flags;
    bool own;
    bool syntaxOnly;
};

struct EffectsPlugin final : PluginASTAction {
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &instance, StringRef) override {
        if (!pluginEnabled) return std::make_unique<ASTConsumer>(); // never null: the multiplexer calls it
        // A summary only for a compile that writes an object (not the editor's syntax check, not -E or -S). A -flto
        // compile writes bitcode as the object (EmitBC; `-emit-llvm` names a `.bc`), and the build still names its
        // summary as an output.
        const FrontendOptions &options = instance.getFrontendOpts();
        bool writes_object = options.ProgramAction == frontend::EmitObj || options.ProgramAction == frontend::EmitBC;
        std::string output = writes_object && StringRef(options.OutputFile).ends_with(".o") ? options.OutputFile : "";
        std::string flags = "opt=" + std::to_string(instance.getCodeGenOpts().OptimizationLevel);
        const SanitizerSet &sanitize = instance.getLangOpts().Sanitize;
        if (sanitize.has(SanitizerKind::Address)) flags += " address";
        if (sanitize.hasOneOf(SanitizerKind::Undefined)) flags += " undefined";
        if (sanitize.has(SanitizerKind::Thread)) flags += " thread";
        bool own = !output.empty() && getenv("SMITH_STACK_USAGE_ADDED") != nullptr; // smith asked (shim/clang.cpp)
        return std::make_unique<Consumer>(output, flags, own, options.ProgramAction == frontend::ParseSyntaxOnly);
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
const ParsedAttrInfoRegistry::Add<EffectAttr<10>> fails_attribute("smith-fails-attr", "[[smith::fails]]");
const ParsedAttrInfoRegistry::Add<PureAttr> pure_attribute("smith-pure-attr", "[[smith::pure]]");
const ParsedAttrInfoRegistry::Add<ReturnsOwnedAttr> owned_attribute("smith-owned-attr", "[[smith::returns_owned]]");
const ParsedAttrInfoRegistry::Add<ReturnsBorrowedAttr> borrowed_attribute("smith-borrowed-attr",
                                                                          "[[smith::returns_borrowed]]");
const ParsedAttrInfoRegistry::Add<ReturnsInternalAttr> internal_attribute("smith-internal-attr",
                                                                          "[[smith::returns_internal]]");
const FrontendPluginRegistry::Add<EffectsPlugin> plugin("smith-effect-inference", "what a function does to the world");

} // namespace

// The editor's hover (patches/clangd-plugin-hover.patch, LLVM r10): clangd calls this for each hovered declaration, and
// the lines of its `smith::hover:` annotations are shown. The declarations below are clangd's (Hover.h), written out
// because the shim has no clangd headers; they are the same types.
namespace clang::clangd {
struct HoverSection {
    std::string Heading;
    std::vector<std::string> Lines;
};
using HoverProvider = bool (*)(const NamedDecl &, HoverSection &);
void registerHoverProvider(HoverProvider provider);
} // namespace clang::clangd

namespace {

bool hoverProvider(const NamedDecl &decl, clang::clangd::HoverSection &section) {
    if (!isa<FunctionDecl>(decl)) return false;
    uint32_t declared = 0;
    if (declaredSet(&decl, declared)) { // a ceiling, a promise or [[smith::pure]]: the attribute says it
        section.Lines.push_back(hoverText(declared, true));
        return true;
    }
    std::string usr = usrOf(&decl);
    if (usr.empty()) return false;
    std::lock_guard<std::mutex> lock(hoverMutex);
    auto found = hoverTable.find(usr);
    if (found == hoverTable.end()) return false;
    section.Lines.push_back(found->second);
    return true;
}

const bool hoverRegistered = (clang::clangd::registerHoverProvider(hoverProvider), true);

} // namespace

extern "C" void smith_effects_disable() {
    pluginEnabled = false;
}

// ---- The program's check -------------------------------------------------------------------------------------------
//
// `smith cc --smith-effects a.o.fx b.o.fx ...`, which smith-cc runs when it links objects that have a summary: the
// summaries are merged (a function is known by its linkage name), each function's set is solved again with the calls
// that cross files, and a function with a ceiling whose set reaches past it only now, through another file, is an
// error naming the whole chain. A function no summary defines has no source: `c`. What the files each found alone was
// already said when they were compiled.

namespace {

struct Edit {
    char kind = 'L'; // L a leaf with effects, E a call by symbol
    uint32_t effects = 0;
    std::string callee, place, name;
    bool tail = false; // a `[[clang::musttail]]` call: a jump, which leaves a cycle no deeper
};

struct Function {
    std::string name, place;
    long declared = -1;
    uint32_t local = 0, inferred = 0;
    long frame = -1;      // bytes of its own frame from the backend, -1 when the object had none
    bool dynamic = false; // the frame grows at run time (a variable-length array, alloca): no bound
    std::vector<Edit> edges;
};

// A `W` line: `effects::within(target, mask)` in a static_assert whose target the file could not judge.
struct Assertion {
    std::string label, target, place, message, name;
    uint32_t mask = 0;
    int ordinal = 0; // among the W lines of its summary, in order
};

class Program {
  public:
    bool read(const char *path) {
        auto buffer = llvm::MemoryBuffer::getFile(path);
        if (!buffer) return false;
        readText(path, (*buffer)->getBuffer());
        return true;
    }

    // A summary given as text, known by `path` (the editor's own, not yet on disk).
    void readText(const std::string &path, llvm::StringRef text) {
        llvm::SmallVector<llvm::StringRef, 8> lines;
        text.split(lines, '\n', -1, false);
        for (llvm::StringRef line : lines) {
            llvm::SmallVector<llvm::StringRef, 8> field;
            line.split(field, '\t');
            if (field.size() >= 2 && field[0] == "H") {
                if (std::find(builds.begin(), builds.end(), field[1].str()) == builds.end())
                    builds.push_back(field[1].str());
                continue;
            }
            if (field.size() >= 4 && field[0] == "S") {
                auto owner = owners.find(field[1].str());
                if (owner == owners.end() || owner->second != path) continue;
                Function &function = functions[field[1].str()];
                field[2].getAsInteger(10, function.frame);
                function.dynamic = field[3] != "static";
                continue;
            }
            if (field.size() >= 6 && field[0] == "W") {
                Assertion w;
                w.label = path;
                w.target = field[1].str();
                uint64_t mask = 0;
                field[2].getAsInteger(10, mask);
                w.mask = static_cast<uint32_t>(mask);
                w.place = field[3].str();
                w.message = field[4].str();
                w.name = field[5].str();
                w.ordinal = ordinals[path]++;
                assertions.push_back(w);
                continue;
            }
            if (field.size() < 5) continue;
            if (field[0] == "F" && field.size() >= 6) {
                if (functions.count(field[1].str()) != 0) continue; // the first definition stands
                Function &function = functions[field[1].str()];
                field[2].getAsInteger(10, function.declared);
                uint64_t local = 0;
                field[3].getAsInteger(10, local);
                function.local = static_cast<uint32_t>(local);
                function.place = field[4].str();
                function.name = field[5].str();
                owners[field[1].str()] = path;
            } else if (field[0] == "L" || field[0] == "E") {
                auto owner = owners.find(field[1].str());
                if (owner == owners.end() || owner->second != path) continue;
                Edit edge;
                edge.kind = field[0][0];
                if (edge.kind == 'L') {
                    uint64_t set = 0;
                    field[2].getAsInteger(10, set);
                    edge.effects = static_cast<uint32_t>(set);
                } else {
                    edge.callee = field[2].str();
                }
                edge.place = field[3].str();
                edge.name = field[4].str();
                edge.tail = field.size() >= 6 && field[5] == "1";
                functions[field[1].str()].edges.push_back(edge);
            }
        }
    }

    // What the link and the editor report: a function whose ceiling is passed only through another file.
    struct Violation {
        std::string symbol, name, place, kind, extra, chain, message;
        int ordinal = -1; // the W line of an assertion, among its summary's
    };

    // The violations (solved again with the calls that cross files); `only` limits them to the functions one summary
    // defines (the editor judges the open file only), "" for all.
    std::vector<Violation> violations(const std::string &only = "") {
        solve();
        std::vector<Violation> found;
        for (auto &entry : functions) {
            if (!only.empty() && owners[entry.first] != only) continue;
            Function &function = entry.second;
            if (function.declared < 0) continue;
            uint32_t declared = static_cast<uint32_t>(function.declared);
            uint32_t extra = function.inferred & ~declared;
            uint32_t now = extra & ~(function.local & ~declared); // what only another file brings in
            if (now == 0) continue;
            const Edit *first = nullptr;
            uint32_t bit = 0;
            for (const Name &name : names) {
                if ((now & name.bit) == 0) continue;
                const Edit *found = witness(function, name.bit);
                if (found != nullptr) {
                    first = found;
                    bit = name.bit;
                    break;
                }
            }
            if (first == nullptr) continue;
            std::string kind = declared == 0 ? "[[smith::pure]]" : "limited to " + describe(declared);
            found.push_back(
                {entry.first, function.name, first->place, kind, describe(extra), chain(entry.first, bit), "", -1});
        }
        for (const Assertion &w : assertions) { // the static_asserts of effects::within that wait for the program
            if (!only.empty() && w.label != only) continue;
            uint32_t extra = visible(w.target) & ~w.mask;
            if (extra == 0) continue;
            std::string text = w.name + " (no source)";
            for (const Name &name : names) {
                auto defined = functions.find(w.target);
                if ((extra & name.bit) == 0 || defined == functions.end() ||
                    witness(defined->second, name.bit) == nullptr)
                    continue;
                text = defined->second.declared >= 0 ? w.name + " (declared " + describe(defined->second.declared) + ")"
                                                     : chain(w.target, name.bit);
                break;
            }
            found.push_back({w.target, w.name, w.place, "assertion", describe(extra), text, w.message, w.ordinal});
        }
        return found;
    }

    // Returns the number of errors, printed as the compiler prints its own.
    int check() {
        int errors = 0;
        for (const Violation &violation : violations()) {
            if (violation.kind == "assertion") {
                llvm::errs() << violation.place
                             << ": error: smith-effect: static assertion failed: " << violation.message << ": `"
                             << violation.name << "` has the effects " << violation.extra
                             << " beyond the allowed ones: " << violation.chain << "\n";
                errors++;
                continue;
            }
            llvm::errs() << violation.place << ": error: smith-effect: `" << violation.name << "` is " << violation.kind
                         << ", but it has the effects " << violation.extra << ": " << violation.chain << "\n";
            errors++;
        }
        return errors;
    }

    // The worst-case stack of every function with a frame, written to `path` (skill smith-lang-effects): its own frame
    // plus the deepest of its callees, over the merged call graph. A tail call that closes a cycle leaves it no
    // deeper; any other cycle, and a frame that grows at run time, make it `unbounded`. A call into code with no
    // source (a leaf with effects, a pointer call, a function no summary defines) is not measured: the row says
    // `at least N + unknown (calls: printf, qsort)`, and what to do with an unknown is the task runtime's to decide
    // (#241). Nothing is written when no object had frame sizes.
    void writeStack(const std::string &path) {
        bool any = false;
        for (const auto &entry : functions)
            any = any || entry.second.frame >= 0;
        if (!any) return;
        std::vector<std::string> order = postorder();
        std::map<std::string, Depth> depths;
        for (const std::string &symbol : order) {
            const Function &function = functions[symbol];
            Depth depth;
            depth.bytes = function.frame < 0 ? 0 : function.frame;
            depth.unbounded = function.dynamic || cyclic.count(symbol) != 0;
            const Depth *deepest = nullptr;
            for (const Edit &edge : function.edges) {
                if (edge.kind == 'L') {
                    if (edge.effects != 0) depth.outside.insert(edge.name);
                } else if (functions.count(edge.callee) == 0) {
                    depth.outside.insert(edge.name);
                } else if (depths.count(edge.callee) != 0 && backs.count(&edge) == 0) {
                    const Depth &child = depths[edge.callee];
                    depth.unbounded = depth.unbounded || child.unbounded;
                    depth.outside.insert(child.outside.begin(), child.outside.end());
                    if (deepest == nullptr || child.bytes > deepest->bytes) deepest = &child;
                }
            }
            depth.path = function.name;
            if (deepest != nullptr) {
                depth.bytes += deepest->bytes;
                depth.path += " -> " + deepest->path;
            }
            depths[symbol] = depth;
        }
        std::string text = "# smith stack 1: bytes of stack a call of each function needs at most, for the build\n";
        text += "# build:";
        for (const std::string &build : builds)
            text += " [" + build + "]";
        text += "\n# columns: function, stack (bytes; `at least N + unknown (calls: ...)` when it calls code with no "
                "source), "
                "deepest path\n";
        for (const auto &entry : functions) {
            if (entry.second.frame < 0 || depths.count(entry.first) == 0) continue;
            const Depth &depth = depths[entry.first];
            std::string outside;
            for (const std::string &name : depth.outside)
                outside += (outside.empty() ? "" : ", ") + name;
            std::string stack = std::to_string(depth.bytes);
            if (!outside.empty()) stack = "at least " + stack + " + unknown (calls: " + outside + ")";
            if (depth.unbounded) stack = "unbounded";
            text += entry.second.name + "\t" + stack + "\t" + depth.path + "\n";
        }
        std::string temporary = path + ".part" + std::to_string(getpid());
        std::error_code error;
        {
            llvm::raw_fd_ostream file(temporary, error);
            if (error) return;
            file << text;
        }
        if (llvm::sys::fs::rename(temporary, path)) {
            std::error_code ignored = llvm::sys::fs::remove(temporary);
            (void)ignored;
        }
    }

    // The solved set of every function the summary `label` defines and of each function they call that another summary
    // defines (a name only the project sees, `file#name`, is left out: hover keys on the linkage name).
    void hovers(const std::string &label, std::vector<EditorHover> &out) {
        std::set<std::string> seen;
        auto add = [&](const std::string &symbol) {
            auto found = functions.find(symbol);
            if (found == functions.end() || symbol.find('#') != std::string::npos || !seen.insert(symbol).second)
                return;
            bool declared = found->second.declared >= 0;
            out.push_back(
                {symbol, declared ? static_cast<uint32_t>(found->second.declared) : found->second.inferred, declared});
        };
        for (const auto &entry : functions) {
            if (owners[entry.first] != label) continue;
            add(entry.first);
            for (const Edit &edge : entry.second.edges) {
                if (edge.kind == 'E') add(edge.callee);
            }
        }
    }

    // The editor, while the summaries of the project are still being made: a function no summary defines yet is not
    // judged (the link will, with every summary).
    void setLenient(bool value) {
        lenient = value;
    }

  private:
    struct Depth {
        long bytes = 0;
        bool unbounded = false;
        std::set<std::string> outside;
        std::string path;
    };
    bool lenient = false;
    std::map<std::string, Function> functions;
    std::map<std::string, std::string> owners;
    std::vector<Assertion> assertions;
    std::map<std::string, int> ordinals;
    std::vector<std::string> builds;
    std::set<std::string> cyclic; // functions with a call that closes a cycle and is no tail call
    std::set<const Edit *> backs; // the calls that close a cycle

    // The functions callees first, by a walk with its own stack; the calls that close a cycle are noted.
    std::vector<std::string> postorder() {
        std::vector<std::string> order;
        std::map<std::string, int> color; // 1 on the walk, 2 done
        for (const auto &root : functions) {
            if (color[root.first] != 0) continue;
            std::vector<std::pair<std::string, size_t>> walk;
            walk.push_back({root.first, 0});
            color[root.first] = 1;
            while (!walk.empty()) {
                std::string symbol = walk.back().first;
                const Function &function = functions[symbol];
                if (walk.back().second >= function.edges.size()) {
                    color[symbol] = 2;
                    order.push_back(symbol);
                    walk.pop_back();
                    continue;
                }
                const Edit &edge = function.edges[walk.back().second++];
                if (edge.kind != 'E' || functions.count(edge.callee) == 0) continue;
                int state = color[edge.callee];
                if (state == 0) {
                    color[edge.callee] = 1;
                    walk.push_back({edge.callee, 0});
                } else if (state == 1) {
                    backs.insert(&edge);
                    if (!edge.tail) cyclic.insert(symbol);
                }
            }
        }
        return order;
    }

    uint32_t visible(const std::string &symbol) {
        auto found = functions.find(symbol);
        if (found == functions.end()) return lenient ? 0 : C; // no summary defines it: no source
        return found->second.declared >= 0 ? static_cast<uint32_t>(found->second.declared) : found->second.inferred;
    }

    uint32_t reach(const Edit &edge) {
        return edge.kind == 'L' ? edge.effects : visible(edge.callee);
    }

    void solve() {
        for (bool changed = true; changed;) {
            changed = false;
            for (auto &entry : functions) {
                Function &function = entry.second;
                uint32_t set = function.local;
                for (const Edit &edge : function.edges)
                    set |= reach(edge);
                if (set != function.inferred) {
                    function.inferred = set;
                    changed = true;
                }
            }
        }
    }

    const Edit *witness(const Function &function, uint32_t bit) {
        for (const Edit &edge : function.edges)
            if ((reach(edge) & bit) != 0) return &edge;
        return nullptr;
    }

    // `bad -> log_it (log.c:12) -> printf (log.c:5)`: the places without their columns.
    std::string chain(const std::string &start, uint32_t bit) {
        std::string text = functions[start].name;
        std::string symbol = start;
        for (int step = 0; step < 64; step++) {
            auto found = functions.find(symbol);
            if (found == functions.end()) break;
            const Edit *edge = witness(found->second, bit);
            if (edge == nullptr) break;
            text += " -> " + edge->name + " (" + line(edge->place) + ")";
            if (edge->kind == 'L') break;
            auto next = functions.find(edge->callee);
            if (next == functions.end() || next->second.declared >= 0) break;
            symbol = edge->callee;
        }
        return text;
    }

    // `file:line` of a `path:line:column`.
    static std::string line(const std::string &place) {
        size_t last = place.rfind(':');
        size_t before = last == std::string::npos ? last : place.rfind(':', last - 1);
        std::string path = before == std::string::npos ? place : place.substr(0, before);
        std::string rest = before == std::string::npos ? "" : place.substr(before, last - before);
        return llvm::sys::path::filename(path).str() + rest;
    }
};

// The editor's check of the open file: its own summary (fresh, from this parse) merged with the others' on disk, the
// violations of its own functions that come through another file.
std::vector<EditorViolation> editorCheck(const std::string &label, const std::string &own, const std::string &folder,
                                         std::vector<EditorHover> &hovers) {
    Program program;
    program.setLenient(editorPending());
    program.readText(label, own); // first: its functions win over the stale copy in its own file's summary
    std::error_code error;
    for (llvm::sys::fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (llvm::sys::path::extension(it->path()) == ".fx") program.read(it->path().c_str());
    }
    std::vector<EditorViolation> result;
    for (const auto &violation : program.violations(label))
        result.push_back({violation.name, violation.place, violation.kind, violation.extra, violation.chain,
                          violation.message, violation.ordinal});
    program.hovers(label, hovers);
    return result;
}

} // namespace

extern "C" int smith_effects_check(int count, char **files, const char *stack) {
    Program program;
    bool any = false;
    for (int i = 0; i < count; i++)
        any = program.read(files[i]) || any;
    if (!any) return 0;
    if (program.check() > 0) return 1;
    if (stack != nullptr) {
        program.writeStack(stack);
    }
    return 0;
}

// The API of the layer between the editor and clangd (shim/lsp.cpp).
extern "C" void smith_effects_editor_start(void (*listener)(const char *source)) {
    std::lock_guard<std::mutex> lock(editorMutex);
    editorOn = true;
    editorListener = listener;
}

extern "C" void smith_effects_editor_object(const char *source, const char *object) {
    std::lock_guard<std::mutex> lock(editorMutex);
    editorObjects[canonical(source)] = object;
}

extern "C" void smith_effects_editor_saved(const char *source) {
    std::lock_guard<std::mutex> lock(editorMutex);
    editorSaved.insert(canonical(source));
}

// Whether summaries of the project are still being made (the layer's background pass).
extern "C" void smith_effects_editor_pending(int pending) {
    std::lock_guard<std::mutex> lock(editorMutex);
    editorPendingFlag = pending != 0;
}
