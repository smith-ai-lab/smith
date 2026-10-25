---
type: architecture
title: The platform layer
description: What smith does differently on macOS and Linux lives in one file per platform, runtime/platform_macos.c and runtime/platform_linux.c, with the same functions, and this table says what differs.
tags: [platform, linux, macos]
---
# The platform layer

smith is built and tested on macOS on Apple Silicon. Linux (x86_64 and arm64, glibc) is being brought up in issue #305.
Where the two differ, smith's C does not grow `#ifdef` mazes. Instead there is one file per platform with the same
functions:

- `runtime/platform_macos.c`
- `runtime/platform_linux.c`

`smith.c` includes exactly one of them, chosen by the one `#ifdef __APPLE__` beside the include. Everything it includes
after that calls the functions by name. In `.smith` code the platform is `target::os`, never the preprocessor
([smith-target-facts](smith-target-facts.md)).

| Function | macOS | Linux |
|---|---|---|
| `smith_platform_self_id`, `smith_platform_file_id`: the name of a smith's programs folder ([embedded-files-and-programs-folders](embedded-files-and-programs-folders.md)) | the Mach-O `LC_UUID` | the ELF `NT_GNU_BUILD_ID`, linked with `--build-id=md5`: 16 bytes, like a UUID |
| `smith_platform_clone`: a program of the programs folder | `clonefile`, else a hard link, else a symbolic link | `ioctl(FICLONE)` (btrfs, XFS), else a hard link, else a symbolic link |
| `smith_platform_mtime`: a file's time (the executor, the symbolizer) | `st_mtimespec` | `st_mtim` |
| `smith_platform_process_stopped`: whether a test that `smith --smith-explain` read is still stopped | `proc_pidinfo` | the state letter in `/proc/<pid>/stat` |
| `smith_platform_decompress`: a file of clang's resource pack | LZFSE (libcompression; `scripts/pack.c` compresses) | nothing: the pack is stored uncompressed (`scripts/pack.c` keeps every file as it is) |

The C++ shims have the same split in `shim/platform_macos.h` and `shim/platform_linux.h`, which `shim/platform.h`
includes:

| Function | macOS | Linux |
|---|---|---|
| `smith_platform_section`: the deferred initializers of a tool (`shim/tool.h`, `shim/prove.cpp`) | `getsectiondata` on `__DATA,__smith_<tool>` | the linker's `__start___smith_<tool>` and `__stop___smith_<tool>`, of the section `.init_array` was renamed to |
| `smith_platform_self_path`: the running executable | `_NSGetExecutablePath` | `/proc/self/exe` |
| `smith_platform_lld_link`: `ld` in smith's process (`shim/lld.cpp`) | `lld::macho::link`, as `ld64.lld` | `lld::elf::link`, as `ld.lld` |
| `smith_platform_linker_version`: what the driver is told of its ld (`shim/clang.cpp`) | `-mlinker-version=1115.7.3` | nothing (the flag is Darwin's) |
| `smith_platform_spawn_chdir` (`shim/lsp.cpp`) | `posix_spawn_file_actions_addchdir` | `posix_spawn_file_actions_addchdir_np` |
| `smith_platform_spawn_suspended`: a program a debugger attaches to (`shim/lldb_explain.cpp`) | `POSIX_SPAWN_START_SUSPENDED` | a child that stops itself with SIGSTOP before it runs the program |

On Linux `smith.c` also defines `_GNU_SOURCE` before its first include, because glibc hides `nftw`'s `FTW_PHYS` and
`wait4` from plain C23 and macOS does not.

lldb's debug server differs too: macOS's lldb runs the system's debugserver, which Apple signs; on Linux smith is
lldb-server itself, a clone in the programs folder ([debugger](debugger.md)). The LLVM build's post-install steps are
`scripts/llvm_linux.smith` and smith's own link is `build.c`'s `BuildPlatform` table. What still has no Linux side is
listed in issue #305: the symbolizer, the prover's process accounting and `smith::io`.
