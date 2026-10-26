// What the C++ shims do differently on Linux, one function each; shim/platform_macos.h has the same functions for
// macOS, shim/platform.h includes the one for the platform, and knowledge/platform-layer.md has the table.
#pragma once

#include "lld/Common/Driver.h"
#include "llvm/Support/raw_ostream.h"

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

// The sections the LLVM build's post-install step (scripts/llvm_linux.c) moved the initializers of a tool into: an
// object's .init_array becomes `__smith_<tool>`, and the linker gives each such section, its name being a C
// identifier, the symbols __start_<name> and __stop_<name>. They are weak, so a smith without a tool (no lldb, no
// prover) links, and its table is empty.
#define SMITH_PLATFORM_SECTION(name) \
    extern "C" [[gnu::weak]] void (*__start_##name[])(); \
    extern "C" [[gnu::weak]] void (*__stop_##name[])();
SMITH_PLATFORM_SECTION(__smith_fmt)
SMITH_PLATFORM_SECTION(__smith_tidy)
SMITH_PLATFORM_SECTION(__smith_lsp)
SMITH_PLATFORM_SECTION(__smith_lldb)
SMITH_PLATFORM_SECTION(__smith_prove)
#undef SMITH_PLATFORM_SECTION

// The table of initializers moved into the section `name` (such as __smith_fmt), and its count.
inline void (**smith_platform_section(const char *name, size_t *count))() {
    struct Section {
        const char *name;
        void (**start)();
        void (**stop)();
    };
    const Section sections[] = {
        {"__smith_fmt", __start___smith_fmt, __stop___smith_fmt},
        {"__smith_tidy", __start___smith_tidy, __stop___smith_tidy},
        {"__smith_lsp", __start___smith_lsp, __stop___smith_lsp},
        {"__smith_lldb", __start___smith_lldb, __stop___smith_lldb},
        {"__smith_prove", __start___smith_prove, __stop___smith_prove},
    };
    *count = 0;
    for (const Section &section : sections) {
        if (std::strcmp(section.name, name) != 0 || section.start == nullptr || section.stop == nullptr) continue;
        *count = static_cast<size_t>(section.stop - section.start);
        return section.start;
    }
    return nullptr;
}

// The path of the running executable, in out; false when it does not fit.
inline bool smith_platform_self_path(char *out, size_t size) {
    ssize_t length = readlink("/proc/self/exe", out, size - 1);
    if (length <= 0 || static_cast<size_t>(length) >= size - 1) return false;
    out[length] = '\0';
    return true;
}

LLD_HAS_DRIVER(elf)

// The linker smith runs as `ld`: lld's ELF flavor, as the ld.lld binary.
constexpr const char *smith_platform_lld_name = "ld.lld";
inline bool smith_platform_lld_link(llvm::ArrayRef<const char *> arguments) {
    return lld::elf::link(arguments, llvm::outs(), llvm::errs(), /*exitEarly=*/true, /*disableOutput=*/false);
}

// No -mlinker-version= on Linux: the flag is Darwin's, and the ELF driver does not read it.
constexpr const char *smith_platform_linker_version = nullptr;

// Adds a change of folder to the actions of a posix_spawn (glibc 2.29 names it _np).
inline int smith_platform_spawn_chdir(posix_spawn_file_actions_t *actions, const char *folder) {
    return posix_spawn_file_actions_addchdir_np(actions, folder);
}

// Starts `path` on `args` with `environment`, stopped (a debugger attaches, then sends SIGCONT). Linux's posix_spawn
// has no start-suspended flag, so the child stops itself before it runs the program, which it then does under the
// debugger: it calls only raise and execve, which are safe in a child of a process with threads. Returns 0, or an
// errno value.
inline int smith_platform_spawn_suspended(pid_t *pid, const char *path, char *const *args, char *const *environment) {
    pid_t child = fork();
    if (child < 0) return errno;
    if (child == 0) {
        raise(SIGSTOP);
        execve(path, args, environment);
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, WUNTRACED) != child || !WIFSTOPPED(status)) return ECHILD;
    *pid = child;
    return 0;
}
