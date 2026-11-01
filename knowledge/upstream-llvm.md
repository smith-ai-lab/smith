---
type: design
title: upstream's LLVM 23.1.3 release against what smith links
description: What the llvm-project 23.1.3 release archives (macOS arm64, Linux x86_64) ship, and the gaps against scripts/llvm.sh; recommendation use it plus a small build.
tags: [llvm, build, release]
---
# upstream's LLVM 23.1.3 release against what smith links

Measured on `LLVM-23.1.3-macOS-ARM64.tar.zst` (871 MB) and `LLVM-23.1.3-Linux-X64.tar.zst` (1.2 GB) from the
`llvmorg-23.1.3` release (#1376). Unpack with `zstd -d --long=31`. The Linux archive was listed and its CMake files and
two binaries read, not run (the measuring machine is a Mac).

| | macOS arm64 | Linux x86_64 |
|---|---|---|
| Static `lib*.a` | 760 (213 LLVM, 95 clang) | 824 (222 LLVM) |
| clang, lld (ELF, MachO, COFF, Wasm, MinGW, Common), `clangdMain`, `clangTidyMain`, clang-format, `clangDriver`, `clangFrontend` | all static | all static |
| lldb | **`liblldb.dylib` only**, no `liblldb*.a` | **`liblldb.so` only** |
| `clang_main` / `cc1_main` | inside the `clang` binary; **no archive** | same |
| Build | Release, assertions off, **RTTI off, EH off** | same |
| Targets | all 20 (smith: AArch64, or X86) | all 20 |
| System libs | zlib and zstd on (`llvm-config.h`) | zlib and zstd on; `clang` needs libz, libstdc++, libgcc_s |
| Deployment | `minos 14.0` (smith's floor is 26: fine) | glibc and libstdc++ of the build host (not measured) |
| C++ library | system `libc++.1.dylib` | `libstdc++.so.6`, dynamic |
| compiler-rt | `lib/clang/23/lib/darwin`: ASan and UBSan **dynamic** only, `ubsan_minimal` static | `lib/x86_64-unknown-linux-gnu`: full static and shared set |
| Alive2 / Enzyme | **blocked**: need RTTI and EH | blocked |

## Gaps against `scripts/llvm.sh`
- **RTTI and EH off** (smith: on, "Alive2 needs them"). The prover and Enzyme cannot link these libraries. A rebuild of
  `LLVMCore`, Support and what they use with RTTI on is the smallest fix, or the prover stops needing RTTI.
- **lldb**: no static libraries (smith links lldb's core and plugins into its process). Needs a small build of lldb alone.
- **`libclangmain.a`** (`driver.cpp.o`, `cc1_main.cpp.o`, ...): not shipped. Built from source with the right flags, small.
- **Patches**: all but compiler-rt's two-line one die with #1112. The release has none, so the token hook, `clang-format`
  and clangd patches must be gone first. The compiler-rt patch needs a small build of compiler-rt.
- **zlib, zstd on**: smith builds with them off. macOS has `/usr/lib/libz`; zstd is a dylib outside /usr/lib on a Mac.
- Static ASan on macOS is not shipped (smith links `asan_osx_dynamic`, as today).

## Recommendation
**Use the release plus a small build**, not as is and not the whole build: take LLVM, clang, lld, clangd, clang-tidy
and clang-format from the release; build only lldb (static, #7 options), `libclangmain.a`, compiler-rt's patch and, while
the prover needs it, LLVM with RTTI and EH. Until RTTI is settled the saving is only on the macOS libraries the
prover does not touch. No plan here ships a shared LLVM library, so nothing goes to the lead as an escalation.
