// A C interface to clangd's own main, linked into smith as `smith lsp`.
//
// clangdMain is the function clangd's main calls (clangd/tool/ClangdMain.cpp), so every flag, the protocol and every
// diagnostic are clangd's own. See tool.h for how its initializers and its resource directory are handled.
#include "tool.h"

#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <memory>

namespace clang::clangd {
class Transport;

// NOLINTNEXTLINE(readability-identifier-naming): clangd's name
int clangdMain(int argc, char **argv);

// clangd's XPC transport serves it as a macOS XPC service, which only `CLANGD_AS_XPC_SERVICE` asks for. It is left out:
// it links Foundation, whose loading would cost every smith process, compiles included, 10 ms. clangd's own message
// for a build without it is given.
// NOLINTNEXTLINE(readability-identifier-naming): clangd's name
std::unique_ptr<Transport> newXPCTransport() {
    llvm::errs() << "This clangd binary wasn't built with XPC support.\n";
    std::exit(2);
}
} // namespace clang::clangd

// Runs clangd on argv[1 .. argc), as argv[0], <folder>/bin/clangd, in clang's folder. Returns its exit
// status.
extern "C" int smith_clangd_main(int argc, char **argv) {
    smith_tool_start("__smith_lsp", argv[0]);
    return clang::clangd::clangdMain(argc, argv);
}
