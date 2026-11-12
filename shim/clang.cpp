// The only C++ in smith: a C interface to clang's own main, linked into smith so that it compiles in its own process.
//
// clang_main is the function the clang binary's main calls (clang/tools/driver/driver.cpp), so every flag, response
// file, job, diagnostic and crash report is clang's own. build_llvm_run_macos keeps it, with cc1, cc1as and
// cc1gen-reproducer, in libclangmain.a. This file is clang-driver.cpp's main, with the program path chosen here.
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/LLVMDriver.h"

#include "platform.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int clang_main(int argc, char **argv, const llvm::ToolContext &context);

// shim/hook.cpp: makes every compile of a .smith file offer its tokens to the lowering.
#ifdef SMITH_HOOK
extern "C" void smith_hook_install();
#endif

#ifdef SMITH_HOOK
// Whether a command has a source among its inputs (a file of a language clang compiles).
static bool hasSource(int argc, char **argv) {
    static const char *const endings[] = {".c", ".cc", ".cpp", ".cxx", ".m", ".mm", ".smith"};
    for (int i = 1; i < argc; i++) {
        size_t length = std::strlen(argv[i]);
        for (const char *ending : endings) {
            size_t size = std::strlen(ending);
            if (argv[i][0] != '-' && length > size && std::strcmp(argv[i] + length - size, ending) == 0) return true;
        }
    }
    return false;
}
#endif

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
// The folders that differ between machines are written as fixed names in what a compile makes (#3300): smith's cache
// (where the LLVM pack's headers are), smith's data (where clang's own headers are) and the checkout a nested build
// belongs to. A hosted runner, a cloud worker and a laptop spell them differently, and an object that held one could
// not be shared through the step store, whose key leaves them out (runtime/executor_cache.c). They go before the
// user's arguments, so a -ffile-prefix-map of the user's wins where both match.
static void machineMaps(std::vector<std::string> &maps) {
    const char *home = std::getenv("HOME");
    const char *cache = std::getenv("XDG_CACHE_HOME");
    const char *share = std::getenv("XDG_DATA_HOME");
    const char *repo = std::getenv("SMITH_REPO");
    std::string folder;
    if (cache != nullptr && cache[0] == '/')
        folder = std::string(cache) + "/smith";
    else if (home != nullptr && home[0] == '/')
        folder = std::string(home) + "/.cache/smith";
    if (!folder.empty()) maps.push_back("-ffile-prefix-map=" + folder + "=/smith-cache");
    folder.clear();
    if (share != nullptr && share[0] == '/')
        folder = std::string(share) + "/smith";
    else if (home != nullptr && home[0] == '/')
        folder = std::string(home) + "/.local/share/smith";
    if (!folder.empty()) maps.push_back("-ffile-prefix-map=" + folder + "=/smith-data");
    if (repo != nullptr && repo[0] == '/' && repo[1] != '\0')
        maps.push_back(std::string("-ffile-prefix-map=") + repo + "=/smith-repo");
}

extern "C" int smith_clang_main(const char *program, int argc, char **argv, bool lld, const char *resource) {
#ifdef SMITH_HOOK
    smith_hook_install();
#endif
    llvm::SmallVector<char *, 64> arguments;
    arguments.push_back(const_cast<char *>(program));
    bool cc1 = argc > 1 && std::strncmp(argv[1], "-cc1", 4) == 0; // an internal tool, which takes no driver flags
    if (!cc1) arguments.push_back(const_cast<char *>("-no-canonical-prefixes"));
    if (!cc1) { // the programs are in a folder of their own, the files the driver reads in another
        arguments.push_back(const_cast<char *>("-resource-dir"));
        arguments.push_back(const_cast<char *>(resource));
    }
    std::vector<std::string> maps; // kept until clang_main returns: the arguments point into them
    if (!cc1) {
        machineMaps(maps);
        for (std::string &map : maps)
            arguments.push_back(map.data());
    }
    if (!cc1 && lld && smith_platform_linker_version != nullptr)
        arguments.push_back(const_cast<char *>(smith_platform_linker_version));
    // The frame sizes of a compile to an object, for the program's worst-case stack (#207): `-fstack-usage` writes a
    // `.su` that the effects plugin reads and removes (shim/effects_infer.cpp). With `-c` it is beside the object
    // (`SMITH_STACK_USAGE_ADDED=1`: the plugin looks there); one command that compiles and links names every `.su`
    // after the program (`-o p` is `p.su`), the file the driver chose is then given to the plugin, which reads it
    // after each job, in turn.
    bool compiles = false, stack = !cc1, link = false;
    const char *output = nullptr;
    for (int i = 1; i < argc; i++) {
        const char *argument = argv[i];
        if (std::strcmp(argument, "-c") == 0) compiles = true;
        if (std::strcmp(argument, "-o") == 0 && i + 1 < argc) output = argv[i + 1];
        if (std::strcmp(argument, "-E") == 0 || std::strcmp(argument, "-S") == 0 ||
            std::strcmp(argument, "-fsyntax-only") == 0 || std::strcmp(argument, "-###") == 0 ||
            std::strcmp(argument, "-fstack-usage") == 0 || std::strcmp(argument, "-M") == 0 ||
            std::strcmp(argument, "-MM") == 0 || std::strncmp(argument, "-flto", 5) == 0 ||
            std::strcmp(argument, "-emit-llvm") == 0) { // bitcode has no frames yet
            stack = false;
        }
        size_t length = std::strlen(argument);
        // an assembly file has no frames, and the ELF driver warns that -fstack-usage went unused
        if (length > 2 && std::strcmp(argument + length - 2, ".s") == 0) stack = false;
        arguments.push_back(argv[i]);
    }
#ifdef SMITH_HOOK
    link = !compiles && hasSource(argc, argv);
    size_t length = output == nullptr ? 0 : std::strlen(output);
    bool placed = compiles ? (length > 2 && std::strcmp(output + length - 2, ".o") == 0) || output == nullptr
                           : output == nullptr || std::strncmp(output, "/dev/", 5) != 0;
    if (stack && (compiles || link) && placed) {
        std::string file = "1";
        if (link) { // the driver's own rule: the output without its extension, `.su`
            file = output == nullptr ? "a.su" : output;
            size_t dot = file.rfind('.'), slash = file.rfind('/');
            if (output != nullptr && dot != std::string::npos && (slash == std::string::npos || dot > slash))
                file.erase(dot);
            if (output != nullptr) file += ".su";
        }
        arguments.push_back(const_cast<char *>("-fstack-usage"));
        setenv("SMITH_STACK_USAGE_ADDED", file.c_str(), 1);
    }
#else
    (void)stack;
    (void)compiles;
    (void)link;
    (void)output;
#endif
    arguments.push_back(nullptr);
    int count = static_cast<int>(arguments.size()) - 1;
    char **vector = arguments.data();
    llvm::InitLLVM init(count, vector, /*InstallPipeSignalExitHandler=*/true,
                        /*NeedsPOSIXUtilitySignalHandling=*/true);
    return clang_main(count, vector, {program, nullptr, false});
}
