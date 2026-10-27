// A C interface to clangd's own main, linked into smith as `smith lsp`.
//
// clangdMain is the function clangd's main calls (clangd/tool/ClangdMain.cpp), so every flag, the protocol and every
// diagnostic are clangd's own. See tool.h for how its initializers and its resource directory are handled.
#include "tool.h"

#include "clang/Format/Format.h"
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
// runtime/lsp.smith: runs clangd's main with the editor's messages and clangd's behind a layer that rewrites names.
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
extern "C" void smith_clangd_defaults(const char *style, const char *tidy) {
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
    // The editor talks to clangd through the layer of runtime/lsp.smith, which shows the names as the source writes
    // them. What clangd wrote to its output is sent before the layer ends the pipe.
    return smith_lsp_run(argc, argv, [](int count, char **arguments) {
        int status = clang::clangd::clangdMain(count, arguments);
        llvm::outs().flush();
        return status;
    });
#else
    return clang::clangd::clangdMain(argc, argv);
#endif
}
