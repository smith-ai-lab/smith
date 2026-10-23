---
type: architecture
title: smith build llvm
description: smith build llvm builds LLVM 23.1.3 static and pinned into a cache that survives clean, with the libraries smith links.
tags: [llvm, build]
---
# smith build llvm

`smith build llvm` builds LLVM 23.1.3 (clang, clang-tools-extra, lld, compiler-rt; AArch64; static; no dylibs outside
`/usr/lib` and `/System`) from the pinned source, with a pinned CMake and Python, into
`~/.cache/smith/llvm/23.1.3-r4/{build,install}` (`scripts/llvm.sh`; it survives `smith build clean`). `check` and a plain
build never run it. It also keeps clang's own main, with cc1, cc1as and cc1gen-reproducer, as `libclangmain.a` in the
install, and the mains of clang-format, clang-tidy and clangd (patched copies, see `shim/tool.h`) as `libsmithtools.a`,
which smith links. Its clang needs `SDKROOT` (it does not ask xcrun), and it cannot compile for x86_64.
