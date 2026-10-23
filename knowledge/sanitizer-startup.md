---
type: design
title: Sanitized processes start as fast as Apple's
description: A two-line compiler-rt patch gives AddressSanitizer's shadow the fast mmap flag on arm64, so a sanitized process starts in 9 ms.
tags: [sanitizers, patch]
---
# Sanitized processes start as fast as Apple's

**Sanitized processes start as fast as Apple's.** Upstream compiler-rt maps AddressSanitizer's 14 TB of shadow
without XNU's fast-mmap flag on arm64 (it passes `VM_FLAGS_4GB_CHUNK` only on x86_64, and its 2 GB threshold
`2 << 30` overflows `int`), so the kernel paid per TB at every start, `fork` and exit: about 38 ms a process (80
under load) and 40 ms a `fork`. `patches/compiler-rt-xnu-fast-mmap.patch` (two lines, what Apple's clang does) fixes
both: an empty sanitized program starts in 9.0 ms against 8.3 ms for Apple's (40 runs, medians), and a full
`smith check` spends about 117 s of kernel time instead of about 400.
