// A C interface to clangd's own main, linked into smith as `smith lsp`.
//
// clangdMain is the function clangd's main calls (clangd/tool/ClangdMain.cpp), so every flag, the protocol and every
// diagnostic are clangd's own. See tool.h for how its initializers and its resource directory are handled.
#include "tool.h"

#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/CreateInvocationFromArgs.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Tool.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/Utils.h"
#include "clang/Options/Options.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

#include <cstdlib>
#include <memory>
#include <vector>

extern "C" {
#include "../runtime/lang.h"
}

// shim/hook.cpp: makes every translation unit with a .smith main file, clangd's included, offer its tokens to the
// lowering.
#ifdef SMITH_HOOK
extern "C" void smith_hook_install();
#endif

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

// The one place a command becomes a compiler invocation in clangd (clangd/Compiler.cpp, for the open file, the preamble
// and the background index alike) is clang::createInvocation. This is that function, from clang/lib/Driver/
// CreateInvocationFromArgs.cpp (Apache-2.0 WITH LLVM-exception, the version of this LLVM), with one addition: the
// command is first made what `smith cc` makes the command of a .smith input (smith_lang_command, the one rule of
// runtime/lang_command.c: `-x c`, and `-std=gnu2y` unless it names a standard), so a .smith file compiles in the editor
// whatever its command is: the fallback of a folder with no compile database, a hand-written database, or a command
// borrowed from a .c file. smith defines the symbol, so the linker takes it and leaves the archive's member out.
namespace clang {
std::unique_ptr<CompilerInvocation> createInvocation(ArrayRef<const char *> ArgList, CreateInvocationOptions Opts) {
    assert(!ArgList.empty());
    std::optional<DiagnosticOptions> LocalDiagOpts;
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags;
    if (Opts.Diags) {
        Diags = std::move(Opts.Diags);
    } else {
        LocalDiagOpts.emplace();
        Diags =
            CompilerInstance::createDiagnostics(Opts.VFS ? *Opts.VFS : *llvm::vfs::getRealFileSystem(), *LocalDiagOpts);
    }

    SmallVector<const char *, 16> Args(ArgList);
    std::vector<char *> lowered(Args.size() + 16);
    int count =
        smith_lang_command((int)Args.size(), const_cast<char **>(Args.data()), 1, lowered.data(), lowered.size() - 1);
    if (count > 0) Args.assign(lowered.begin(), lowered.begin() + count);

    // FIXME: Find a cleaner way to force the driver into restricted modes.
    Args.insert(llvm::find_if(Args, [](const char *Elem) { return llvm::StringRef(Elem) == "--"; }), "-fsyntax-only");

    // FIXME: We shouldn't have to pass in the path info.
    driver::Driver TheDriver(Args[0], llvm::sys::getDefaultTargetTriple(), *Diags, "clang LLVM compiler", Opts.VFS);

    // Don't check that inputs exist, they may have been remapped.
    TheDriver.setCheckInputsExist(false);
    TheDriver.setProbePrecompiled(Opts.ProbePrecompiled);

    std::unique_ptr<driver::Compilation> C(TheDriver.BuildCompilation(Args));
    if (!C) return nullptr;

    if (C->getArgs().hasArg(options::OPT_fdriver_only)) return nullptr;

    // Just print the cc1 options if -### was present.
    if (C->getArgs().hasArg(options::OPT__HASH_HASH_HASH)) {
        C->getJobs().Print(llvm::errs(), "\n", true);
        return nullptr;
    }

    // We expect to get back exactly one command job, if we didn't something failed. Offload compilation is an
    // exception as it creates multiple jobs. If that's the case, we proceed with the first job.
    const driver::JobList &Jobs = C->getJobs();
    bool OffloadCompilation = false;
    if (Jobs.size() > 1) {
        for (auto &A : C->getActions()) {
            // On MacOSX real actions may end up being wrapped in BindArchAction
            if (isa<driver::BindArchAction>(A)) A = *A->input_begin();
            if (isa<driver::OffloadAction>(A)) {
                OffloadCompilation = true;
                break;
            }
        }
    }

    bool PickFirstOfMany = OffloadCompilation || Opts.RecoverOnError;
    if (Jobs.size() == 0 || (Jobs.size() > 1 && !PickFirstOfMany)) {
        SmallString<256> Msg;
        llvm::raw_svector_ostream OS(Msg);
        Jobs.Print(OS, "; ", true);
        Diags->Report(diag::err_fe_expected_compiler_job) << OS.str();
        return nullptr;
    }
    auto Cmd = llvm::find_if(
        Jobs, [](const driver::Command &Cmd) { return StringRef(Cmd.getCreator().getName()) == "clang"; });
    if (Cmd == Jobs.end()) {
        Diags->Report(diag::err_fe_expected_clang_command);
        return nullptr;
    }

    const llvm::opt::ArgStringList &CCArgs = Cmd->getArguments();
    if (Opts.CC1Args) *Opts.CC1Args = {CCArgs.begin(), CCArgs.end()};
    auto CI = std::make_unique<CompilerInvocation>();
    if (!CompilerInvocation::CreateFromArgs(*CI, CCArgs, *Diags, Args[0]) && !Opts.RecoverOnError) return nullptr;
    return CI;
}
} // namespace clang

// Runs clangd on argv[1 .. argc), as argv[0], <folder>/bin/clangd, in clang's folder. Returns its exit
// status.
extern "C" int smith_clangd_main(int argc, char **argv) {
    smith_tool_start("__smith_lsp", argv[0]);
#ifdef SMITH_HOOK
    smith_hook_install();
#endif
    return clang::clangd::clangdMain(argc, argv);
}
