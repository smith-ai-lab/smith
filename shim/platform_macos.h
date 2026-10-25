// What the C++ shims do differently on macOS, one function each; shim/platform_linux.h has the same functions for
// Linux, shim/platform.h includes the one for the platform, and knowledge/platform-layer.md has the table.
#pragma once

#include "lld/Common/Driver.h"
#include "llvm/Support/raw_ostream.h"

#include <cstddef>
#include <cstdint>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include <spawn.h>

// The table of initializers scripts/llvm.sh moved into __DATA,<name> (a name such as __smith_fmt), and its count.
inline void (**smith_platform_section(const char *name, size_t *count))() {
    unsigned long size = 0;
    auto *table = reinterpret_cast<void (**)()>(getsectiondata(&_mh_execute_header, "__DATA", name, &size));
    *count = table == nullptr ? 0 : size / sizeof(void (*)());
    return table;
}

// The path of the running executable, in out; false when it does not fit.
inline bool smith_platform_self_path(char *out, size_t size) {
    uint32_t capacity = static_cast<uint32_t>(size);
    return _NSGetExecutablePath(out, &capacity) == 0;
}

LLD_HAS_DRIVER(macho)

// The linker smith runs as `ld`: lld's Mach-O flavor, as the ld64.lld binary.
constexpr const char *smith_platform_lld_name = "ld64.lld";
inline bool smith_platform_lld_link(llvm::ArrayRef<const char *> arguments) {
    return lld::macho::link(arguments, llvm::outs(), llvm::errs(), /*exitEarly=*/true, /*disableOutput=*/false);
}

// The -mlinker-version= the driver is given when its ld is lld (shim/clang.cpp says why), or nullptr.
constexpr const char *smith_platform_linker_version = "-mlinker-version=1115.7.3";

// Adds a change of folder to the actions of a posix_spawn.
inline int smith_platform_spawn_chdir(posix_spawn_file_actions_t *actions, const char *folder) {
    return posix_spawn_file_actions_addchdir(actions, folder);
}

// Starts `path` on `args` with `environment`, stopped before its first instruction (a debugger attaches, then sends
// SIGCONT). Returns 0, or an errno value.
inline int smith_platform_spawn_suspended(pid_t *pid, const char *path, char *const *args, char *const *environment) {
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_START_SUSPENDED);
    int spawned = posix_spawn(pid, path, nullptr, &attributes, args, environment);
    posix_spawnattr_destroy(&attributes);
    return spawned;
}
