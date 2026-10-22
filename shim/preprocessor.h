// smith-preprocessor: a clang-tidy check that a .smith file has no preprocessor (issue #46). Registered by shim/tidy.cpp
// (smith lint, and smith lsp's built-in checks, since its name starts with smith-). In a .smith file it reports
//
//   1. every preprocessor directive: #define, #undef, #include, #if and its kin (#ifdef, #ifndef, #elif, #elifdef,
//      #elifndef, #else, #endif), #pragma, #error, #warning, #line, #embed, and _Pragma; and
//   2. every `c::` name that is a C macro (`c::assert`, `c::SIZE_MAX`, `c::isfinite`): the macro is the preprocessor
//      too, and the module that wraps the C API provides what the macro stood for.
//
// Each message says what to write instead. A `.c` file is left alone. smith's own inputs that use the preprocessor in
// a `.smith` file on purpose opt out where they use it, with clang-tidy's own `// NOLINT(smith-preprocessor)` on the
// line (or NOLINTBEGIN/NOLINTEND around a block).
//
// The directives are read from the file's text with the raw lexer, so those inside a branch the compiler did not take
// are found too, and #error, #warning and #line, which the preprocessor does not report to a callback, are not missed.
// The `c::` names are looked up when the file ends, among the macros the file's `import <h>;` lines (the lowering's
// `#include`s) defined: the lowering itself reports a bare macro the same way.
#pragma once

#include "clang-tidy/ClangTidyCheck.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "llvm/ADT/StringRef.h"

#include <memory>
#include <string>

namespace clang::tidy::smith_preprocessor {

// What to write instead of a directive.
inline const char *directive_advice(llvm::StringRef name) {
    if (name == "include") return "write `import <header>;` for a C header or `import module;` for a module";
    if (name == "define")
        return "a constant is `constexpr usize name = value;`, and a macro with arguments is a function; a C macro the "
               "program needs is provided by the module that wraps the C API";
    if (name == "undef") return "there are no macros to undefine";
    if (name == "if" || name == "ifdef" || name == "ifndef" || name == "elif" || name == "elifdef" ||
        name == "elifndef" || name == "else" || name == "endif") {
        return "ask the language: `import target;` and `if constexpr (target::os == target::macos) { ... } else { ... }`";
    }
    if (name == "pragma" || name == "_Pragma") {
        return "a compile flag a library needs is declared by its row of smith_libraries, and an attribute goes before "
               "the declaration";
    }
    if (name == "error") return "write `static_assert(false, \"message\")`";
    if (name == "warning") return "remove it, or write a comment";
    if (name == "line") return "a lowering writes its own line directives; remove it";
    if (name == "embed") return "embed the file in a build step and declare the data as `extern`";
    return "use the language: `import`, `constexpr`, `if constexpr` and `target::`";
}

// What to write instead of a C macro, or the general advice.
inline std::string macro_advice(llvm::StringRef name) {
    if (name == "assert") return "write `contract::assert(condition)` (import smith::contract;)";
    if (name == "isfinite") return "write `math::is_finite(x)` (import smith::math;)";
    if (name == "SIZE_MAX") return "write `math::max(usize)` (import smith::math;)";
    if (name == "INFINITY") return "write `math::infinity(f32)` or `math::infinity(f64)` (import smith::math;)";
    if (name == "NAN") return "write `math::nan(f32)` or `math::nan(f64)` (import smith::math;)";
    return "a C macro is the preprocessor's: the module that wraps the C API should provide it as a function or a "
           "constant";
}

class Callbacks final : public PPCallbacks {
  public:
    Callbacks(ClangTidyCheck &check, const SourceManager &sm, Preprocessor &pp) : check(check), sm(sm), pp(pp) {}

    void EndOfMainFile() override {
        FileID main = sm.getMainFileID();
        OptionalFileEntryRef entry = sm.getFileEntryRefForID(main);
        if (!entry || !entry->getName().ends_with(".smith")) return;
        llvm::StringRef text = sm.getBufferData(main);
        Lexer lexer(sm.getLocForStartOfFile(main), pp.getLangOpts(), text.begin(), text.begin(), text.end());
        Token token;
        Token before; // the token before the previous one, and the previous one: `c` `::` name
        Token previous;
        bool in_directive = false;
        bool first = true;
        for (;;) {
            lexer.LexFromRawLexer(token);
            if (token.is(tok::eof)) break;
            if (token.isAtStartOfLine()) in_directive = false;
            if (!in_directive && token.is(tok::hash) && token.isAtStartOfLine()) {
                in_directive = true;
                directive(lexer, token);
            } else if (token.is(tok::raw_identifier) && token.getRawIdentifier() == "_Pragma") {
                check.diag(token.getLocation(), "`_Pragma` is the preprocessor, and a .smith file has none: %0")
                    << llvm::StringRef(directive_advice("_Pragma"));
            } else if (!in_directive && !first && token.is(tok::raw_identifier) && previous.is(tok::coloncolon) &&
                       before.is(tok::raw_identifier) && before.getRawIdentifier() == "c") {
                macro(token);
            }
            before = previous;
            previous = token;
            first = false;
        }
    }

  private:
    ClangTidyCheck &check;
    const SourceManager &sm;
    Preprocessor &pp;

    // `# name`: the directive's name is the next token of the line.
    void directive(Lexer &lexer, const Token &hash) {
        Token name;
        lexer.LexFromRawLexer(name);
        std::string spelled = name.is(tok::raw_identifier) ? name.getRawIdentifier().str() : "";
        if (name.is(tok::eof) || name.isAtStartOfLine() || spelled.empty()) return; // a null directive, or `# 33 "file"`
        check.diag(hash.getLocation(), "`#%0` is the preprocessor, and a .smith file has none: %1") << llvm::StringRef(spelled)
            << llvm::StringRef(directive_advice(spelled));
    }

    void macro(const Token &name) {
        llvm::StringRef spelled = name.getRawIdentifier();
        IdentifierInfo *info = pp.getIdentifierInfo(spelled);
        if (info == nullptr || pp.getMacroInfo(info) == nullptr) return;
        check.diag(name.getLocation(), "`c::%0` is a C macro, and a .smith file has no preprocessor: %1")
            << spelled << llvm::StringRef(macro_advice(spelled));
    }
};

class PreprocessorCheck final : public ClangTidyCheck {
  public:
    PreprocessorCheck(llvm::StringRef Name, ClangTidyContext *Context) : ClangTidyCheck(Name, Context) {}

    void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP, Preprocessor *) override {
        PP->addPPCallbacks(std::make_unique<Callbacks>(*this, SM, *PP));
    }
};

} // namespace clang::tidy::smith_preprocessor
