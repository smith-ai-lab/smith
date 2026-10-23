---
type: convention
title: smith's own code
description: smith's own code is C23, libc only, with no malloc, and the only C++ is the thin shims to LLVM.
tags: [c, style]
---
# smith's own code

**smith's own code is C23 (`-std=gnu23`), libc only, no malloc**: it allocates nothing itself. The LLVM linked into
it is C++ and does, and the files in `shim/` (`clang.cpp`, `lld.cpp`, `symbolize.cpp`), thin C interfaces to it, are
the only C++ in smith.
