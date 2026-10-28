// smith-mem: a clang-tidy check that a module does not call C's raw memory functions (issue #399). Registered by
// shim/tidy.cpp beside the idiom checks (so `smith check`, `smith lint` and smith lsp's built-in checks run it). In a
// .smith file with a `module` line it reports every call of `memcpy`, `memmove`, `memset` or `memcmp` (`c::memcpy`,
// `__builtin_memcpy` and the fortified `__builtin___memcpy_chk` the SDK's macro makes of it) and names the typed
// function in smith::mem that replaces it: the type and the length of a buffer travel apart from its data in C, and
// `mem::copy(dst, src)` checks that they agree.
//
// The list is a table (`replacements` below), one row for each wrapped C function, so a wrapper joins by adding a row;
// a function joins only when its typed replacement exists. A `.c` file and a `.smith` file with no `module` line are
// left alone (the C rule). A function that calls `contract::trusted()` (`smith_trusted()` in C) is the edge: smith's
// own wrappers (lib/std/mem.smith) mark themselves with it, and so does any function that implements one, so the
// check skips it. `// NOLINT(smith-mem)` opts a line out as for any check.
//
// `memset(p, 0, sizeof *p)` and `memset(&x, 0, sizeof x)` carry a fix-it, `mem::zero(p)` and `mem::zero(&x)`, which
// `smith lint --fix` applies; the other calls map to slices by hand, and the message says how.
#pragma once

#include "clang-tidy/ClangTidyCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringRef.h"

#include <set>
#include <string>
#include <vector>

#include "idiom.h"

namespace clang::tidy::smith_mem {

// One row for each wrapped C function: its name, and what to write instead.
struct Replacement {
    const char *name;
    const char *use;
};

inline const std::vector<Replacement> &replacements() {
    static const std::vector<Replacement> table = {
        {"memcpy", "`mem::copy(dst, src)`, two slices of one element type and one length"},
        {"memmove", "`mem::move(dst, src)`, as mem::copy and the slices may overlap"},
        {"memset", "`mem::zero(xs)` or `mem::zero(&x)` to clear, `mem::fill(xs, value)` to set"},
        {"memcmp", "`mem::equal(a, b)`, two slices of one element type; `mem::bytes(&x)` is the bytes of a value"},
    };
    return table;
}

// The row for a function's name: `memcpy`, `__builtin_memcpy`, `__builtin___memcpy_chk` and `__memcpy_chk` are one.
inline const Replacement *replacement_for(llvm::StringRef name) {
    for (const Replacement &row : replacements()) {
        llvm::StringRef base = row.name;
        if (name == base || name == ("__builtin_" + base).str() || name == ("__builtin___" + base + "_chk").str() ||
            name == ("__" + base + "_chk").str())
            return &row;
    }
    return nullptr;
}

// Whether a function's body calls the marker: `smith_trusted`, which `contract::trusted()` lowers to in a .smith file
// as in C, or `smith_contract_trusted`, the marker beneath it that `contract::check_trusted()` calls directly.
class TrustedFinder final : public RecursiveASTVisitor<TrustedFinder> {
  public:
    bool found = false;

    bool VisitCallExpr(CallExpr *call) {
        if (const FunctionDecl *callee = call->getDirectCallee()) {
            const StringRef name = callee->getName();
            if (name == "smith_trusted" || name == "smith_contract_trusted") found = true;
        }
        return !found;
    }
};

class MemCheck final : public ClangTidyCheck {
  public:
    MemCheck(llvm::StringRef Name, ClangTidyContext *Context) : ClangTidyCheck(Name, Context) {}

    bool isLanguageVersionSupported(const LangOptions &options) const override {
        return !options.CPlusPlus; // the check is for .smith
    }

    void registerMatchers(ast_matchers::MatchFinder *Finder) override {
        using namespace ast_matchers;
        Finder->addMatcher(
            callExpr(callee(functionDecl().bind("callee")), optionally(hasAncestor(functionDecl().bind("inside"))))
                .bind("call"),
            this);
    }

    void check(const ast_matchers::MatchFinder::MatchResult &Result) override {
        const SourceManager &sm = *Result.SourceManager;
        if (!is_module_file(sm, *Result.Context)) return;
        const auto *callee = Result.Nodes.getNodeAs<FunctionDecl>("callee");
        const Replacement *row = replacement_for(callee->getName());
        if (row == nullptr) return;
        const auto *call = Result.Nodes.getNodeAs<CallExpr>("call");
        SourceLocation at = sm.getExpansionLoc(call->getBeginLoc());
        if (!at.isValid() || sm.getFileID(at) != sm.getMainFileID()) return;
        if (!seen.insert(at.getRawEncoding()).second) return; // a generic's instances are copies of one source
        if (const auto *inside = Result.Nodes.getNodeAs<FunctionDecl>("inside")) {
            if (inside->hasBody()) {
                TrustedFinder finder;
                finder.TraverseStmt(inside->getBody());
                if (finder.found) return;
            }
        }
        std::string written = row->name;
        auto report = diag(at, "`%0` is unchecked, since its type and its length travel apart from the data: use %1 "
                               "(import smith::mem;), or mark the function `contract::trusted()` if it implements a "
                               "wrapper")
                      << written << llvm::StringRef(row->use);
        fix_zero(report, call, written, Result);
    }

    void onEndOfTranslationUnit() override {
        seen.clear();
        module_file = -1;
    }

  private:
    std::set<unsigned> seen;
    int module_file = -1; // -1 not yet looked, 0 no, 1 yes

    // Whether the main file is a .smith file with a `module` line (the raw text, as smith-module reads it).
    bool is_module_file(const SourceManager &sm, ASTContext &context) {
        if (module_file >= 0) return module_file == 1;
        module_file = 0;
        FileID main = sm.getMainFileID();
        if (!smith_idiom::is_smith_file(sm, main) || smith_idiom::is_generated(sm, main)) return false;
        smith_idiom::Lexed lexed = smith_idiom::lex(sm, context.getLangOpts(), main);
        for (size_t i = 0; i + 1 < lexed.tokens.size(); i++) {
            if (smith_idiom::is_name(lexed.tokens[i], "module") && lexed.tokens[i].isAtStartOfLine() &&
                lexed.tokens[i + 1].is(tok::raw_identifier)) {
                module_file = 1;
                break;
            }
        }
        return module_file == 1;
    }

    // The range as written in the file, also for an argument of a macro (`memset` is the SDK's, so its call and its
    // arguments are macro expansions whose text is in the file); invalid when it cannot be mapped there.
    static CharSourceRange file_range(SourceRange range, const ast_matchers::MatchFinder::MatchResult &Result) {
        return Lexer::makeFileCharRange(CharSourceRange::getTokenRange(range), *Result.SourceManager,
                                        Result.Context->getLangOpts());
    }

    static std::string text_of(const Expr *expr, const ast_matchers::MatchFinder::MatchResult &Result) {
        CharSourceRange range = file_range(expr->getSourceRange(), Result);
        if (range.isInvalid()) return "";
        return Lexer::getSourceText(range, *Result.SourceManager, Result.Context->getLangOpts()).str();
    }

    // `memset(p, 0, sizeof *p)` and `memset(&x, 0, sizeof x)`: the size is the one of the object the first argument
    // points at, so mem::zero(first argument) says it.
    static void fix_zero(DiagnosticBuilder &report, const CallExpr *call, const std::string &name,
                         const ast_matchers::MatchFinder::MatchResult &Result) {
        if (name != "memset" || call->getNumArgs() < 3) return; // the fortified form has a fourth argument
        const Expr *value = call->getArg(1)->IgnoreParenImpCasts();
        const auto *zero = dyn_cast<IntegerLiteral>(value);
        if (zero == nullptr || !zero->getValue().isZero()) return;
        const auto *size = dyn_cast<UnaryExprOrTypeTraitExpr>(call->getArg(2)->IgnoreParenImpCasts());
        if (size == nullptr || size->getKind() != UETT_SizeOf || size->isArgumentType()) return;
        std::string pointer = text_of(call->getArg(0)->IgnoreParenImpCasts(), Result);
        std::string operand = text_of(size->getArgumentExpr()->IgnoreParens(), Result);
        if (pointer.empty() || operand.empty()) return;
        bool address_of = pointer == "&" + operand;
        bool dereference = operand == "*" + pointer;
        if (!address_of && !dereference) return;
        const SourceManager &sm = *Result.SourceManager;
        SourceRange written = call->getSourceRange();
        if (written.getBegin().isMacroID()) written = sm.getExpansionRange(written.getBegin()).getAsRange();
        CharSourceRange whole = file_range(written, Result);
        if (whole.isInvalid()) return;
        SourceLocation begin = whole.getBegin();
        llvm::StringRef before = sm.getBufferData(sm.getFileID(begin)).substr(0, sm.getFileOffset(begin));
        if (before.ends_with("c::")) begin = begin.getLocWithOffset(-3); // `c::memset(...)` as the author wrote it
        report << FixItHint::CreateReplacement(CharSourceRange::getCharRange(begin, whole.getEnd()),
                                               "mem::zero(" + pointer + ")");
    }
};

} // namespace clang::tidy::smith_mem
