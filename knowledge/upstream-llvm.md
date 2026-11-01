---
type: reference
title: Upstream's LLVM 23.1.3 release against smith's own build
description: What llvm/llvm-project's release archives for macOS arm64 and Linux x86_64 ship and how they differ from what scripts/llvm.sh builds, so smith knows which parts it could take from upstream.
tags: [llvm, build, static-linking]
---
# Upstream's LLVM 23.1.3 release against smith's own build

**Upstream's release is static, but built without RTTI and exceptions, so the prover and Enzyme cannot use it as it is** (issue #1376, measured on `LLVM-23.1.3-macOS-ARM64.tar.zst` and `LLVM-23.1.3-Linux-X64.tar.zst`, unpacked with `zstd --long=30`).

| | macOS arm64 | Linux x86_64 |
|---|---|---|
| `libLLVM*.a` / `libclang*.a` | 212 / 109 | 221 / 139 (no clangd XPC libraries) |
| lld: ELF, MachO, COFF, MinGW, Wasm, Common | static | static |
| clangd, clang-tidy, clang-format libraries (`libclangdMain`, `libclangTidyMain`, `libclangFormat`, `libclangDaemon`, `libclangDaemonTweaks`) | static | static |
| lldb | `liblldb.dylib` only, no `liblldb[A-Z]*.a` | `liblldb.so` only |
| compiler-rt | `libclang_rt.*_osx` (asan, tsan, ubsan, rtsan, profile) | 73 files per target |
| `libLLVM` / `libclang-cpp` shared | none / `libclang-cpp` present | none / `libclang-cpp` present |
| `llvm-config` | `--shared-mode` static, `--build-mode` RELEASE | `LLVMConfig.cmake`: `LLVM_ENABLE_SHARED_LIBS OFF` |
| assertions | off | off |
| **RTTI / exceptions** | **off / off** | **off / off** |
| targets | all 20 (AArch64 ... XCore) | the same list |
| system libs | zlib, libc++ (system dylib) | zlib, zstd, libxml2; `libstdc++.so.6` and `libgcc_s.so.1` dynamic, GCC 12.3 on Ubuntu 22.04, needs glibc 2.34 |
| deployment target | `minos 14.0` (our floor is 26.0, so the objects are older, which is fine) | not applicable |

smith's own build (`scripts/llvm.sh`) differs in what it turns on and off: RTTI and EH **on** (Alive2 needs them), only the AArch64 target, no zlib, zstd or libxml2, lldb's static libraries copied out of the build tree, and the four driver objects of `clang_main` packed as `libclangmain.a`.

## Gaps and what each needs
- **RTTI and exceptions off.** Alive2 and Enzyme (the plugin API, `dyn_cast` on their own classes) are built against an LLVM with both on. Mixing them fails at link time (`typeinfo for llvm::...` undefined) or at run time. Needs either a build of LLVM's libraries with RTTI and EH on (what we do today), or a change in Alive2 and Enzyme: not a small one.
- **The token hook and the other patches.** Not in the release. Once the new front end lands (#1112) only the compiler-rt sanitizer patch remains, and compiler-rt is a small build of its own.
- **`libclangmain.a`.** The release has no `clang_main` library: `driver.cpp.o` and the `cc1*` objects are inside the `clang` executable. They are four small sources and could be built from the source tarball with the release's include directory.
- **lldb.** Only a dynamic library, and `smith dbg` and `smith dap` link lldb's plugins statically. A shared lldb is a decision for the lead (smith ships one binary with no LLVM install at run time).
- **clangd, clang-tidy, clang-format.** The libraries are there; `scripts/llvm.sh` also needs the build tree's `ClangFormat.cpp.o` (its `main`). Not in the release.
- **Linux libstdc++.** The archives were compiled against GCC 12's libstdc++, so smith's Linux link must use a compatible one statically.

## Recommendation
Keep building LLVM from source until RTTI and EH can be turned on in a release-shaped build. The release is useful for the parts that need neither (lld, compiler-rt checks, comparing sizes), not as a replacement.
