// The only other C++ in smith: lld's Mach-O linker, linked into smith so that it links in its own process.
//
// clang's driver finds its linker as `ld` beside the program it runs as (the folder of shim/clang.cpp's `bin/clang`),
// before PATH, and smith keeps a hard link to itself there named `ld`. So a link the driver starts is smith run as
// `ld`, which calls lld::macho::link, the function the ld64.lld binary's main reaches, on the arguments the driver
// built. No flag is added to anything the user types.
#include "lld/Common/Driver.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/raw_ostream.h"
#ifdef SMITH_ENZYME
#include <limits.h>
#include <mach-o/dyld.h>
#include <string>
#include "llvm/Plugins/PassPlugin.h"
#endif

LLD_HAS_DRIVER(macho)

#ifdef SMITH_ENZYME
// Enzyme (automatic differentiation of LLVM IR, scripts/enzyme.sh) is linked into smith and runs inside the LTO
// pipeline of this link. lld's LTO config has no hook for a PassBuilder, only `--load-pass-plugin=<file>`, which
// dlopens the file and asks it for llvmGetPassPluginInfo. The file is smith itself: dlopen of an executable's own path
// answers its handle, and Enzyme's llvmGetPassPluginInfo is exported from it (build.c, -exported_symbol; the pointer
// below keeps it from being stripped). Nothing is loaded from disk, and a link with no bitcode never reads the option.
extern "C" llvm::PassPluginLibraryInfo llvmGetPassPluginInfo();
[[gnu::used]] static llvm::PassPluginLibraryInfo (*const smith_enzyme_entry)() = llvmGetPassPluginInfo;
#endif

// Links as ld does: argv[1 .. argc) are ld's arguments, argv[0] is ignored. Returns the exit status. lld exits the
// process itself when it is done, without freeing what it allocated (exitEarly), as ld64.lld does.
extern "C" int smith_lld_main(int argc, char **argv) {
    llvm::InitLLVM init(argc, argv, /*InstallPipeSignalExitHandler=*/true, /*NeedsPOSIXUtilitySignalHandling=*/true);
    llvm::SmallVector<const char *, 64> arguments;
    arguments.push_back("ld64.lld");
#ifdef SMITH_ENZYME
    char self[PATH_MAX];
    uint32_t size = sizeof self;
    std::string plugin;
    if (_NSGetExecutablePath(self, &size) == 0) {
        plugin = std::string("--load-pass-plugin=") + self;
        arguments.push_back(plugin.c_str());
    }
#endif
    for (int i = 1; i < argc; i++)
        arguments.push_back(argv[i]);
    bool linked = lld::macho::link(arguments, llvm::outs(), llvm::errs(), /*exitEarly=*/true, /*disableOutput=*/false);
    return linked ? 0 : 1;
}
