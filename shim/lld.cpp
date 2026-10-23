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
#include "llvm/IR/Module.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#endif

LLD_HAS_DRIVER(macho)

#ifdef SMITH_ENZYME
// Enzyme (automatic differentiation of LLVM IR, scripts/enzyme.sh) is linked into smith and runs inside the LTO
// pipeline of this link. lld's LTO config has no hook for a PassBuilder, only `--load-pass-plugin=<file>`, which
// dlopens the file and asks it for llvmGetPassPluginInfo. The file is smith itself: dlopen of an executable's own path
// answers its handle, and Enzyme's llvmGetPassPluginInfo is exported from it (build.c, -exported_symbol; the pointer
// below keeps it from being stripped). Nothing is loaded from disk, and a link with no bitcode never reads the option.
//
// The entry is smith's own (Enzyme's is weak, so this one wins): it registers Enzyme, then AddressSanitizer at the same
// extension point, so that ASan runs right after Enzyme and before the optimizer. An autodiff program is compiled with
// `-fsanitize=address -Xclang -disable-llvm-passes`: its functions carry `sanitize_address` and are not yet
// instrumented, which instrumenting before Enzyme would break (the `enzyme_*` marker loads are hidden, and the
// gradients are left unchecked). The pass does nothing for a module with no such function, so a link of another
// program is unchanged, and it cannot run twice: a module that clang already instrumented has `asan.module_ctor`,
// and the pass leaves it alone.
extern "C" void registerEnzyme(llvm::PassBuilder &PB);

namespace {
// What clang's debug profile gives ASan (-fno-sanitize-recover=all, use-after-scope on, stack use after return decided
// by the runtime, globals dead stripping on, no ODR indicator, destructors global).
struct SmithAsanAfterEnzyme : llvm::PassInfoMixin<SmithAsanAfterEnzyme> {
    llvm::PreservedAnalyses run(llvm::Module &module, llvm::ModuleAnalysisManager &manager) {
        bool wanted = false;
        for (llvm::Function &function : module)
            wanted |= function.hasFnAttribute(llvm::Attribute::SanitizeAddress);
        if (module.getFunction("asan.module_ctor") != nullptr) wanted = false;
        if (!wanted) return llvm::PreservedAnalyses::all();
        llvm::AddressSanitizerOptions options;
        options.CompileKernel = false;
        options.Recover = false;
        options.UseAfterScope = true;
        options.UseAfterReturn = llvm::AsanDetectStackUseAfterReturnMode::Runtime;
        return llvm::AddressSanitizerPass(options, /*UseGlobalGC=*/true, /*UseOdrIndicator=*/false,
                                          llvm::AsanDtorKind::Global)
            .run(module, manager);
    }
};

void smith_register_enzyme_and_asan(llvm::PassBuilder &builder) {
    registerEnzyme(builder);
    builder.registerFullLinkTimeOptimizationEarlyEPCallback(
        [](llvm::ModulePassManager &passes, llvm::OptimizationLevel) { passes.addPass(SmithAsanAfterEnzyme()); });
}
} // namespace

extern "C" llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "SmithEnzymeAsan", "v0.1", smith_register_enzyme_and_asan};
}
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
