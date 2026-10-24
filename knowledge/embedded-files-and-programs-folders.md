---
type: architecture
title: The embedded files and the programs folders
description: The files clang looks up beside itself are embedded in smith and written once to a runtime folder that smith never removes, and the programs it runs are clones of smith in a per-binary folder.
tags: [compiler, cache]
---
# The embedded files and the programs folders

**The files the driver looks up beside itself are embedded** (`scripts/pack.c`, `#embed`, LZFSE; about 20 MB in
smith): the builtin headers, the sanitizers' ignore lists, the macOS compiler-rt runtimes (not iOS's), and
`libLTO.dylib`, which the system `ld` loads for `-flto` (lld has LLVM inside and needs none). On first use (about 1 s, then a `stat`) smith writes them
to `$XDG_DATA_HOME/smith/clang/llvm-<version>-<key>` (else `~/.local/share/...`); the key is the pack's contents
and the identity of the compiler and linker. The programs the driver runs (`bin/clang`, `bin/clang++`, `bin/ld`,
clones of smith itself; it runs `clang` again with `-cc1` for any compile with more than one step) are not in
it: they are in `$XDG_DATA_HOME/smith/programs/<LC_UUID of the running smith>/`, whose `lib` links to that
folder's, and the shim passes `-resource-dir` for it. So the installed smith, a dev build and another agent's
never share or remake each other's links, and a smith without lld has no `ld`. When a new programs folder is made, smith removes the folders
that may go (issue #109; every worktree's build made one, about 220 MB once its binary was deleted): one whose binary
is gone or was rebuilt in place (the folder's `binary` file records the LC_UUID and path), and one idle for 31 days
(using one touches it once a day). Never one a running smith holds: every smith that uses its folder takes a shared
`flock` on `<folder>/.in-use` for its life (children inherit it, the kernel drops it on death; about 11 microseconds), and the
prune takes the exclusive lock without waiting, removing the folder while it holds it; and never one younger than an
hour (the gap between a start and its lock). So a long-running `smith lsp` of a deleted worktree keeps its folder, and
an old folder with no process goes. Only the runtime folder lives forever.

**That folder is outside the caches smith prunes and cleans, and smith never removes it**, because a sanitized
binary records an absolute `LC_RPATH` to its `lib/clang/23/lib/darwin`: `smith cache clean`, the 31-day pruning and
an emptied `~/.cache` leave every built binary running. Old folders stay for the binaries built against them (about
70 MB each); delete one by hand to reclaim it, and its binaries then fail at load, naming the missing library.

**The programs are clones (`clonefile`, APFS copy on write: a file of their own, no bytes copied), never hard links**
(issue #48; a hard link, then a symbolic link, is the fallback on a volume that cannot clone). macOS names a running
program by the last name of its file that was looked up, and `syspolicyd` (Gatekeeper and XProtect) reads that name
to assess it, seconds after the exec under load (`ASP: Unable to apply provenance sandbox` in the kernel log marks the
wait). When the file is gone it kills the process: `Terminating process due to Malware rejection ... Unable (errno: 2)
to read file` (`Killed: 9`, exit 137), and a file that vanishes while it is validated crashes `syspolicyd` itself
(`Security::MachORep::signingData`, a null `Universal`), which fails every assessment pending until it restarts. Every
test's scratch data folder held a hard-linked name of the smith under test and was removed when the test ended, so
the other processes of that smith, started from `build/`, were killed. With a clone nothing but the program's own
name can vanish under it. A test that makes a copy of smith for a project uses `cp -c`, and `ln -s` where a name will do.
