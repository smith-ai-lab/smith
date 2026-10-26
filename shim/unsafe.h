// smith-unsafe (issue #422): three C constructs with no Smith-safe use, errors in a module and nowhere else (a .c file,
// or a .smith file with no `module` line, is C as it is). A clang-tidy check beside the idiom checks, registered by
// shim/tidy.cpp, on in smith's default `.clang-tidy`, so `smith check` and the editor fail on it. Each message names the
// replacement; the table is knowledge/module-diagnostics.md.
//
//   setjmp, longjmp, sigsetjmp, siglongjmp   the jump skips every `defer` and `errdefer` on the way
//   an array parameter, `i32 a[10]`          `a` is a pointer: the bound means nothing and sizeof(a) is a pointer's
//   a run-time-sized stack array, alloca     it can overflow the stack unchecked and breaks compile-time stack sizes
//
// A fixed array whose bound is a constant expression is fine. `// NOLINT(smith-unsafe)` opts a line out.
#pragma once

#include "clang-tidy/ClangTidyCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
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
    static const char *const names[] = {"setjmp",   "_setjmp",   "longjmp",          "_longjmp",
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
            on_call(call, sm);
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
        diag(at, "the stack cannot hold a size known only at run time: use `arena::push_slice(&a, i32, n)`, or a fixed "
                 "array when `n` has a maximum");
    }

    void on_call(const CallExpr *call, const SourceManager &sm) {
        const FunctionDecl *callee = call->getDirectCallee();
        if (callee == nullptr || callee->getIdentifier() == nullptr) return;
        llvm::StringRef name = callee->getName();
        bool jump = is_jump(name);
        if (!jump && !is_stack_allocation(name)) return;
        SourceLocation at = own(call->getBeginLoc(), sm);
        if (at.isInvalid()) return;
        if (!jump) return report_stack(at);
        name.consume_front("__builtin_");
        name.consume_front("_");
        diag(at, "`%0` skips every `defer` and `errdefer` on the way, so cleanup silently does not run: return a "
                 "`result`, or let the task fail")
            << name;
    }

    void on_parameter(const ParmVarDecl *parameter, const SourceManager &sm) {
        if (!parameter->getOriginalType()->isArrayType()) return;
        SourceLocation at = own(parameter->getBeginLoc(), sm);
        if (at.isInvalid()) return;
        std::string name = parameter->getName().str();
        diag(at, "%0 is a pointer, not an array: take `i32 a[:]` (a slice), or `i32 (*a)[10]` for a fixed array")
            << (name.empty() ? std::string("this parameter") : "`" + name + "`");
    }

    void on_variable(const VarDecl *variable, const SourceManager &sm) {
        if (isa<ParmVarDecl>(variable) || !variable->getType()->isVariableArrayType()) return;
        SourceLocation at = own(variable->getBeginLoc(), sm);
        if (at.isValid()) report_stack(at);
    }
};

} // namespace clang::tidy::smith_unsafe
