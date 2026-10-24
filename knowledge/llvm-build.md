---
type: architecture
title: smith build llvm
description: smith build llvm builds LLVM 23.1.3 static and pinned into a cache that survives clean, with the libraries smith links.
tags: [llvm, build]
---
# smith build llvm

`smith build llvm` builds LLVM 23.1.3 (clang, clang-tools-extra, lld, compiler-rt; AArch64; static; no dylibs outside
`/usr/lib` and `/System`) from the pinned source, with a pinned CMake and Python, into
`~/.cache/smith/llvm/23.1.3-r8/{build,install}` (`scripts/llvm.sh`; it survives `smith build clean`). `check` and a plain
build never run it. It also keeps clang's own main, with cc1, cc1as and cc1gen-reproducer, as `libclangmain.a` in the
install, and the mains of clang-format, clang-tidy and clangd (patched copies, see `shim/tool.h`) as `libsmithtools.a`,
which smith links. The configuration `23.1.3-r8` adds `patches/clang-constexpr-c.patch` (C function bodies in the constant evaluator for `.smith`, #97) and clang-format's tuple and slice forms (`patches/clang-format-smith.patch`, #96, #82); r6 adds the compile-time failure annotations, the evaluator under `-fmodules` and constexpr pointers into literals (#103); r7 adds `patches/clang-constexpr-fold.patch` (a call the evaluator ran is no GNU folding: no `-Wgnu-folding-constant`, #166) and clang-format's `[const :]` spacing (#130); r8 adds `patches/smith-lang-format-enum.patch` (payload enums laid out like structs, the dot rule's spacing, #215). Its clang needs `SDKROOT` (it does not ask xcrun), and it cannot compile for x86_64.
