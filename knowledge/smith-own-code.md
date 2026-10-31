---
type: convention
title: smith's own code
description: smith's own code is .smith modules and C23, libc only, with no malloc; C++ is only the edges to LLVM.
tags: [c, style]
---
# smith's own code

**smith's own code is `.smith` modules and C23 (`-std=gnu23`), libc only, no malloc**: it allocates nothing itself.
New logic is written in `.smith` (`runtime/*.smith`, built by the previous release under the lag rule); the C in
`runtime/` moves to `.smith` file by file (#45). The LLVM linked into smith is C++ and allocates, and `shim/` holds the
only C++ in smith: the edges that must call LLVM's C++ API, behind C interfaces. The logic that grew there moves to
`.smith` (#540; owner, 2026-10-27: "we only have to talk to LLVM at the edges through C++, but we get to write nice
Smith code most of the time").

## Decisions

- No new shell files (owner, 2026-10-25: "I want everything to be in Smith. I don't want any shell files"): a test that starts smith or another process is a `.smith` test through `scratch::` (`tests/scratch.smith`), a download or a tool's build is a task in the build file, and the existing scripts are being ported (see the roadmap).
