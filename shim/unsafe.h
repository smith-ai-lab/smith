// smith-unsafe (issue #422): three C constructs with no Smith-safe use, errors in a module and nowhere else (a .c file,
// or a .smith file with no `module` line, is C as it is). A clang-tidy check beside the idiom checks, registered by
// shim/tidy.cpp, on in smith's default `.clang-tidy`, so `smith check` and the editor fail on it. Each message names
// the replacement; the table is knowledge/module-diagnostics.md.
//
//   setjmp, longjmp, sigsetjmp, siglongjmp   the jump skips every `defer` and `errdefer` on the way
//   an array parameter that is no checked one  `i32 a[n]` (the bound is no constant) and the forms the lowering leaves
//   a
//                                            plain pointer (`i32 a[const 4]`, a typedef'd array, a function type's)
//   a run-time-sized stack array, alloca     it can overflow the stack unchecked and breaks compile-time stack sizes
//
// A module's `i32 xs[4]` is a checked parameter (`xs[static 4]`, countof, checked indexes) and a fixed array whose
// bound is a constant expression is fine. A function that calls `smith_trusted()` may jump (the test runner, until
// #324). `// NOLINT(smith-unsafe)` opts a line out.
#pragma once

#include "clang-tidy/ClangTidyCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/StringRef.h"

#include <set>
#include <string>

namespace clang::tidy::smith_unsafe {

// Whether the main file is a .smith file whose first token is `module`.
inline bool in_module(const SourceManager &sm, const LangOptions &options) {
    FileID main = sm.getMainFileID();
    OptionalFileEntryRef entry = sm.getFileEntryRefForID(main);
    if (!entry || !entry->getName().ends_with(".smith")) return false;
    llvm::StringRef text = sm.getBufferData(main);
    Lexer lexer(sm.getLocForStartOfFile(main), options, text.begin(), text.begin(), text.end());
    Token token;
    lexer.LexFromRawLexer(token);
    return token.is(tok::raw_identifier) && token.getRawIdentifier() == "module";
}

inline bool is_jump(llvm::StringRef name) {
    static const char *const names[] = {"setjmp",    "_setjmp",    "longjmp",          "_longjmp",
                                        "sigsetjmp", "siglongjmp", "__builtin_setjmp", "__builtin_longjmp"};
    for (const char *candidate : names) {
        if (name == candidate) return true;
    }
    return false;
}

inline bool is_stack_allocation(llvm::StringRef name) {
    static const char *const names[] = {"alloca", "__builtin_alloca", "__builtin_alloca_with_align",
                                        "__builtin_alloca_uninitialized"};
    for (const char *candidate : names) {
        if (name == candidate) return true;
    }
    return false;
}

// Whether a function's body calls smith_trusted() (the macro's own function included).
class TrustedFinder : public RecursiveASTVisitor<TrustedFinder> {
  public:
    bool found = false;
    bool VisitCallExpr(CallExpr *call) {
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee != nullptr && callee->getIdentifier() != nullptr) {
            llvm::StringRef name = callee->getName();
            if (name == "smith_trusted" || name == "smith_contract_trusted") found = true;
        }
        return !found;
    }
};

inline bool is_trusted(const FunctionDecl *function) {
    if (function == nullptr || !function->hasBody()) return false;
    TrustedFinder finder;
    finder.TraverseStmt(const_cast<Stmt *>(function->getBody()));
    return finder.found;
}

class UnsafeCheck final : public ClangTidyCheck {
  public:
    UnsafeCheck(llvm::StringRef Name, ClangTidyContext *Context) : ClangTidyCheck(Name, Context) {}

    bool isLanguageVersionSupported(const LangOptions &options) const override {
        return !options.CPlusPlus; // the check is for .smith
    }

    void registerMatchers(ast_matchers::MatchFinder *Finder) override {
        using namespace ast_matchers;
        Finder->addMatcher(callExpr().bind("call"), this);
        Finder->addMatcher(parmVarDecl().bind("parameter"), this);
        Finder->addMatcher(varDecl().bind("variable"), this);
    }

    void check(const ast_matchers::MatchFinder::MatchResult &Result) override {
        const SourceManager &sm = *Result.SourceManager;
        if (!module_known) {
            module_known = true;
            module = in_module(sm, Result.Context->getLangOpts());
        }
        if (!module) return;
        if (const auto *call = Result.Nodes.getNodeAs<CallExpr>("call")) {
            on_call(call, sm, *Result.Context);
        } else if (const auto *parameter = Result.Nodes.getNodeAs<ParmVarDecl>("parameter")) {
            on_parameter(parameter, sm);
        } else if (const auto *variable = Result.Nodes.getNodeAs<VarDecl>("variable")) {
            on_variable(variable, sm);
        }
    }

    void onEndOfTranslationUnit() override {
        seen.clear();
        module_known = false;
        module = false;
    }

  private:
    std::set<unsigned> seen; // a generic's instances are copies of one source
    bool module_known = false;
    bool module = false;

    // The place in the module's own file where the construct is written (through a macro, where the macro is used), or
    // an invalid location for a construct that is elsewhere or already reported.
    SourceLocation own(SourceLocation at, const SourceManager &sm) {
        if (!at.isValid()) return {};
        SourceLocation file = sm.getFileLoc(at);
        if (sm.getFileID(file) != sm.getMainFileID()) return {};
        return seen.insert(file.getRawEncoding()).second ? file : SourceLocation();
    }

    void report_stack(SourceLocation at) {
        diag(at, "the stack cannot hold a size known only at run time: use `arena::push_slice(&a, I32, n)`, or a fixed "
                 "array when `n` has a maximum");
    }

    static const FunctionDecl *enclosing(const CallExpr *call, ASTContext &context) {
        DynTypedNode node = DynTypedNode::create(*call);
        while (true) {
            auto parents = context.getParents(node);
            if (parents.empty()) return nullptr;
            node = parents[0];
            if (const auto *function = node.get<FunctionDecl>()) return function;
        }
    }

    void on_call(const CallExpr *call, const SourceManager &sm, ASTContext &context) {
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr || callee->getIdentifier() == nullptr) return;
        llvm::StringRef name = callee->getName();
        bool jump = is_jump(name);
        if (!jump && !is_stack_allocation(name)) return;
        SourceLocation at = own(call->getBeginLoc(), sm);
        if (at.isInvalid()) return;
        if (!jump) return report_stack(at);
        if (is_trusted(enclosing(call, context))) return;
        name.consume_front("__builtin_");
        name.consume_front("_");
        diag(at, "`%0` skips every `defer` and `errdefer` on the way, so cleanup silently does not run: return a "
                 "`result`, or let the task fail")
            << name;
    }

    // A module's array parameter is checked when the lowering wrote `xs[static N]` with every bound a constant:
    // anything else is a plain pointer or a bound the compiler cannot count.
    void on_parameter(const ParmVarDecl *parameter, const SourceManager &sm) {
        QualType type = parameter->getOriginalType();
        if (!type->isArrayType()) return;
        // va_list is an array on x86_64 (`__va_list_tag[1]`), which C passes as a pointer: the platform's own type,
        // not an array parameter the program wrote (on arm64 it is a pointer and never comes here).
        const ASTContext &context = parameter->getASTContext();
        if (context.hasSameType(type, context.getBuiltinVaListType())) return;
        const ArrayType *outer = type->getAsArrayTypeUnsafe();
        bool constant = true;
        for (const ArrayType *level = outer; level != nullptr;
             level = level->getElementType()->getAsArrayTypeUnsafe()) {
            if (!isa<ConstantArrayType>(level)) constant = false;
        }
        bool checked = constant && outer->getSizeModifier() == ArraySizeModifier::Static;
        if (checked) return;
        SourceLocation at = own(parameter->getBeginLoc(), sm);
        if (at.isInvalid()) return;
        std::string name = parameter->getName().str();
        std::string who = name.empty() ? std::string("this parameter") : "`" + name + "`";
        if (!constant) {
            diag(at,
                 "the bound of %0 is not a constant expression, so the compiler cannot count it: take `I32 a[:]` (a "
                 "slice) for a length known at run time, or a constant bound `I32 a[4]`")
                << who;
        } else {
            diag(at, "%0 is a plain pointer here, with no size to check: write the array parameter as `I32 a[4]` (a "
                     "checked array) or take `I32 a[:]` (a slice)")
                << who;
        }
    }

    void on_variable(const VarDecl *variable, const SourceManager &sm) {
        if (isa<ParmVarDecl>(variable) || !variable->getType()->isVariableArrayType()) return;
        if (variable->getName() == "smith_main_slots") return; // the lowering's own: the slices of `main(str args[:])`
        SourceLocation at = own(variable->getBeginLoc(), sm);
        if (at.isValid()) report_stack(at);
    }
};

} // namespace clang::tidy::smith_unsafe
