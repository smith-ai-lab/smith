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

LLD_HAS_DRIVER(macho)

// Links as ld does: argv[1 .. argc) are ld's arguments, argv[0] is ignored. Returns the exit status. lld exits the
// process itself when it is done, without freeing what it allocated (exitEarly), as ld64.lld does.
extern "C" int smith_lld_main(int argc, char **argv) {
    llvm::InitLLVM init(argc, argv, /*InstallPipeSignalExitHandler=*/true, /*NeedsPOSIXUtilitySignalHandling=*/true);
    llvm::SmallVector<const char *, 64> arguments;
    arguments.push_back("ld64.lld");
    for (int i = 1; i < argc; i++)
        arguments.push_back(argv[i]);
    bool linked = lld::macho::link(arguments, llvm::outs(), llvm::errs(), /*exitEarly=*/true, /*disableOutput=*/false);
    return linked ? 0 : 1;
}
