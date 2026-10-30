---
type: architecture
title: smith build llvm
description: smith build llvm builds LLVM 23.1.3 static and pinned into a cache that survives clean, with the libraries smith links.
tags: [llvm, build]
---
# smith build llvm

`smith build llvm` builds LLVM 23.1.3 (clang, clang-tools-extra, lld, compiler-rt; AArch64; static; no dylibs outside
`/usr/lib` and `/System`) from the pinned source, with a pinned CMake and Python, into
`~/.cache/smith/llvm/23.1.3-r11/{build,install}` (`scripts/llvm.sh`; it survives `smith build clean`). `check` and a plain
build never run it. **A build that finishes removes the others (issue #618):** each folder is 11 GB, and six piled up, so
`scripts/llvm_prune.c` removes every folder under `~/.cache/smith/llvm` (and its Enzyme, `enzyme/<version>-<name>`) but the
one just built and the one the previous release uses (the one whose `install/lib/smith-clang.id` is the identity that
`smith-cc --smith-which` of the smith running the build prints), so a build of the previous commit still finds its LLVM. A folder
with no id file (a build in progress) is never touched, and when the identity cannot be read nothing is removed. What goes
is built again from the pinned sources. It also keeps clang's own main, with cc1, cc1as and cc1gen-reproducer, as `libclangmain.a` in the
install, and the mains of clang-format, clang-tidy and clangd (patched copies, see `shim/tool.h`) as `libsmithtools.a`,
which smith links. The configuration `23.1.3-r11` is built from these patches: `patches/clang-constexpr-c.patch` (C function bodies in the constant evaluator for `.smith`, #97) and clang-format's tuple and slice forms (`patches/clang-format-smith.patch`, #96, #82); r6 adds the compile-time failure annotations, the evaluator under `-fmodules` and constexpr pointers into literals (#103); r7 adds `patches/clang-constexpr-fold.patch` (a call the evaluator ran is no GNU folding: no `-Wgnu-folding-constant`, #166) and clang-format's `[const :]` spacing (#130); r8 adds `patches/smith-lang-format-enum.patch` (payload enums laid out like structs, the dot rule's spacing, #215); r9 adds `patches/clang-plugin-attr-args.patch` (the parser reads the arguments of a plugin's scoped C23 attribute, identifiers included: `ParsedAttrInfo::HasVariadicIdentifierArg`, #207); r10 adds `patches/clangd-plugin-hover.patch` (clangd's `HoverProviderRegistry`: a plugin in smith's process registers a `HoverProvider` and adds a section to the hover of a declaration, and clangd knows nothing of what it says, #263); r11 adds `patches/smith-lang-format-filescope-if.patch` (clang-format reads the branches of a file-scope `if constexpr` in a `.smith` file as file scope: their imports are import lines and their contents declarations, #335). Its clang needs `SDKROOT` (it does not ask xcrun), and it cannot compile for x86_64.

**On Linux** (issue #305) the same `smith build llvm` runs `scripts/llvm_linux.c`, a plain C23 `.c` program (
so the seed can build it with the system's compiler before there is a smith), with Linux's pinned CMake and Python
and the same patches and folder. It builds for the host's target (X86 or AArch64), with lld as the host linker and no
SDK, libedit or system debugserver, and does `scripts/llvm.sh`'s post-install steps for ELF: `libclangmain.a`,
`libsmithtools.a` and lldb's archives with each object's `.init_array` renamed by `llvm-objcopy --rename-section` to
`__smith_<tool>` (no `defer_init`: `shim/platform_linux.h` runs the section from the linker's `__start_`/`__stop_`
symbols), and archives rewritten whole, since `liblldbHost.a` has two members named `Host.cpp.o`. The identity is
`llvm-23.1.3-linux <hash>`, hashed by `scripts/sha256.h`. On a 4-core machine the first build takes about 2 hours and
12 GB. The prover's libraries and Enzyme are not built on Linux yet.

**The LLVM pack (issue #797).** A hosted CI runner has no LLVM and building it takes hours, so the part of an LLVM build that smith links and embeds is published as a GitHub Release `llvm-<name>-<os>-<arch>` (`llvm-23.1.3-r11-darwin-arm64`, `llvm-23.1.3-r11-linux-x86_64`: the configuration name of `build.c` and `scripts/llvm.sh`, and `uname`'s machine; the identity of `smith-clang.id` is inside and in the release's notes). `scripts/llvm_pack.c` (plain C23, libc, `tar` and `gh`) makes and takes it: the pack is `install/lib/*.a`, `smith-clang.id`, `libLTO.dylib`, `lib/clang/` (the resource directory) and `lib/cmake/`, `install/include/`, `bin/clang` (the compiler `compare` and the shims use), and on macOS the prover's `prove/{lib,include}` and Enzyme's `lib/`; not the 4.7 GB build folder, the sources or the other 2.4 GB of programs. On macOS arm64 that is 811 MB, 130 MB as one xz part (unpacking takes 7 s). It is cut into parts of at most `LLVM_PACK_PART_BYTES` (1.9 GB, under GitHub's 2 GiB per asset), each with its SHA-256 in `<tag>.sha256`, which a fetch checks before it unpacks beside the final folders and renames them in. `smith build llvm` takes the pack when its release exists and builds otherwise (`-Dfetch=auto`; `-Dfetch=0` always builds, `-Dfetch=1` fetches or fails; the pack goes to `~/.cache/smith/llvm/<name>/install`, `prove/` and `~/.cache/smith/enzyme/` where a build puts them, so `smith build` then links against it), and a folder with a build in it is never fetched over. `smith build llvm-pack [-Dout=DIR]` writes the pack of the LLVM built here, `smith build llvm-publish` publishes it. `.github/workflows/llvm.yml` (`workflow_dispatch`, and a push to `main` that changes `patches/`, `scripts/llvm*` or the workflow) builds each platform on a hosted runner (`macos-15` through the stage0 smith, `ubuntu-latest` with `scripts/llvm_linux.c`) and publishes it, and does nothing when the release exists; every job runs only when the repository is public (`github.event.repository.private == false`), because hosted minutes of a private repository are billed. A job has the hosted 6-hour limit and the build has no chaining yet: a measured run that does not fit must split it (the build folder handed on as an artifact).
