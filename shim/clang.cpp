// The only C++ in smith: a C interface to clang's own main, linked into smith so that it compiles in its own process.
//
// clang_main is the function the clang binary's main calls (clang/tools/driver/driver.cpp), so every flag, response
// file, job, diagnostic and crash report is clang's own. scripts/llvm.sh keeps it, with cc1, cc1as and
// cc1gen-reproducer, in libclangmain.a. This file is clang-driver.cpp's main, with the program path chosen here.
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/LLVMDriver.h"

#include <cstring>

int clang_main(int argc, char **argv, const llvm::ToolContext &context);

// shim/hook.cpp: makes every compile of a .smith file offer its tokens to the lowering.
extern "C" void smith_hook_install();

// Runs clang as `program` (the clang or clang++ in the LLVM install smith was linked against) on argv[1 .. argc);
// argv[0] is ignored. Returns clang's exit status.
//
// `lld` says the linker is lld (the folder's `bin/ld` is smith, which links with lld: shim/lld.cpp). The driver's
// defaults for what it compiles depend on the linker's version, which it knows only from -mlinker-version= or from the
// ld it was built beside: Apple's current one, which makes it send Objective-C messages to classes through
// `objc_msgSendClass$` stubs that only that ld writes. So for lld smith gives the version of the last ld without them,
// which lld matches: it writes `objc_msgSend$` stubs per selector, and nothing else the driver does changes. A
// -mlinker-version= of the user's comes later and wins.
// -no-canonical-prefixes goes first, so a later -canonical-prefixes from the user wins. It makes the driver take
// `program` as its own path, not this executable's, so it finds the resource directory (builtin headers, compiler-rt),
// dsymutil and the other tools beside `program`, exactly as the clang binary there does.
extern "C" int smith_clang_main(const char *program, int argc, char **argv, bool lld, const char *resource) {
    smith_hook_install();
    llvm::SmallVector<char *, 64> arguments;
    arguments.push_back(const_cast<char *>(program));
    bool cc1 = argc > 1 && std::strncmp(argv[1], "-cc1", 4) == 0; // an internal tool, which takes no driver flags
    if (!cc1) arguments.push_back(const_cast<char *>("-no-canonical-prefixes"));
    if (!cc1) { // the programs are in a folder of their own, the files the driver reads in another
        arguments.push_back(const_cast<char *>("-resource-dir"));
        arguments.push_back(const_cast<char *>(resource));
    }
    if (!cc1 && lld) arguments.push_back(const_cast<char *>("-mlinker-version=1115.7.3"));
    for (int i = 1; i < argc; i++)
        arguments.push_back(argv[i]);
    arguments.push_back(nullptr);
    int count = static_cast<int>(arguments.size()) - 1;
    char **vector = arguments.data();
    llvm::InitLLVM init(count, vector, /*InstallPipeSignalExitHandler=*/true,
                        /*NeedsPOSIXUtilitySignalHandling=*/true);
    return clang_main(count, vector, {program, nullptr, false});
}
