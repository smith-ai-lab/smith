// A C interface to clangd's own main, linked into smith as `smith lsp`.
//
// clangdMain is the function clangd's main calls (clangd/tool/ClangdMain.cpp), so every flag, the protocol and every
// diagnostic are clangd's own. See tool.h for how its initializers and its resource directory are handled.
#include "tool.h"

#include "clang/Format/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <memory>
#include <string>

// shim/hook.cpp: makes every translation unit with a .smith main file, clangd's included, offer its tokens to the
// lowering.
#ifdef SMITH_HOOK
extern "C" void smith_hook_install();
extern "C" void smith_hook_editor();
// shim/tidy.cpp: the module that makes the checks judge a .smith file's names as written.
extern "C" void smith_tidy_names_register(bool present);
// shim/lsp.cpp: runs clangd's main with the editor's messages and clangd's behind a layer that rewrites names.
extern "C" int smith_lsp_run(int argc, char **argv, int (*server)(int, char **));
#endif

namespace clang::clangd {
class Transport;

// NOLINTNEXTLINE(readability-identifier-naming): clangd's name
int clangdMain(int argc, char **argv);

// The .clang-tidy that clangd's checks use where the project has none (clangd/TidyProvider.cpp, patched by
// patches/smith-editor-defaults.patch).
// NOLINTNEXTLINE(readability-identifier-naming): clangd's name
extern const char *FallbackTidyConfigFile;

// clangd's XPC transport serves it as a macOS XPC service, which only `CLANGD_AS_XPC_SERVICE` asks for. It is left out:
// it links Foundation, whose loading would cost every smith process, compiles included, 10 ms. clangd's own message
// for a build without it is given.
// NOLINTNEXTLINE(readability-identifier-naming): clangd's name
std::unique_ptr<Transport> newXPCTransport() {
    llvm::errs() << "This clangd binary wasn't built with XPC support.\n";
    std::exit(2);
}
} // namespace clang::clangd

// smith's defaults for the editor: the fallback style of clangd's formatting is `file:<style>` (the default of its
// --fallback-style, so the flag still wins), and its checks use <tidy> where no .clang-tidy governs a file. A project's
// own file wins whole in both. The strings live for the process.
static std::string styleFile;

extern "C" void smith_clangd_defaults(const char *style, const char *tidy) {
    styleFile = style;
    static std::string fallback;
    static std::string checks;
    fallback = std::string("file:") + style;
    checks = tidy;
    clang::format::DefaultFallbackStyle = fallback.c_str();
    clang::clangd::FallbackTidyConfigFile = checks.c_str();
}

// Runs clangd on argv[1 .. argc), as argv[0], <folder>/bin/clangd, in clang's folder. Returns its exit
// status.
extern "C" int smith_clangd_main(int argc, char **argv) {
    smith_tool_start("__smith_lsp", argv[0]);
    clang::CommandHook = smith_command_hook; // a .smith file compiles in the editor as `smith cc` compiles it
#ifdef SMITH_HOOK
    smith_hook_install();
    smith_hook_editor();
    smith_tidy_names_register(false); // clangd's built-in checks judge the names as written, as `smith lint` does
    // The editor talks to clangd through the layer of shim/lsp.cpp, which shows the names as the source writes them.
    return smith_lsp_run(argc, argv,
                         [](int count, char **arguments) { return clang::clangd::clangdMain(count, arguments); });
#else
    return clang::clangd::clangdMain(argc, argv);
#endif
}

// The formatting of a C, Objective-C or C++ file by smith's own style (the file `smith_clangd_defaults` was given),
// with clang-format's library and no patch: `smith lsp` answers textDocument/formatting itself where the project has no
// .clang-format (shim/lsp.cpp). Calls `edit` once per replacement of [begin, end) of `text`; returns 0, or 1 when the
// style cannot be read or parsed.
extern "C" int smith_lsp_format(const char *name, const char *text, size_t size, size_t begin, size_t end,
                                void (*edit)(void *, size_t, size_t, const char *, size_t), void *context) {
    if (styleFile.empty()) return 1;
    auto buffer = llvm::MemoryBuffer::getFile(styleFile);
    if (!buffer) return 1;
    clang::format::FormatStyle style = clang::format::getLLVMStyle(clang::format::guessLanguage(name, {text, size}));
    if (clang::format::parseConfiguration((*buffer)->getMemBufferRef(), &style)) return 1;
    clang::tooling::Replacements fixes = clang::format::reformat(
        style, {text, size}, {clang::tooling::Range((unsigned)begin, (unsigned)(end - begin))}, name);
    for (const clang::tooling::Replacement &fix : fixes)
        edit(context, fix.getOffset(), fix.getLength(), fix.getReplacementText().data(),
             fix.getReplacementText().size());
    return 0;
}
