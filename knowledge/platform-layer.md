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

On Linux `smith.c` also defines `_GNU_SOURCE` before its first include, because glibc hides `nftw`'s `FTW_PHYS` and
`wait4` from plain C23 and macOS does not.

What still differs and has no Linux side yet is listed in issue #305's spike report: the resource pack's codec, the
in-process linker, the deferred initializers, the symbolizer, `smith::io`, and lldb.
