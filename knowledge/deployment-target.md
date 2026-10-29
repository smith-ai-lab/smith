---
type: convention
title: smith runs on macOS 15 and newer
description: smith's own binary is built with -mmacosx-version-min=15.0 whatever macOS builds it, a newer system function is called only where it exists, and programs smith builds for users keep the user's target.
tags: [macos, build, release]
---
# smith runs on macOS 15 and newer

**smith is built for macOS 15 and newer, on any newer macOS** (owner, 2026-10-29; #802). Without a deployment target the
binary needs the macOS that built it: the stage0 release made on macOS 27 did not even start on GitHub's `macos-15`
runners (`dyld: Symbol not found: _posix_spawn_file_actions_addchdir`).

- **`build.c` gives every compile of smith itself and its link `-mmacosx-version-min=15.0`** (`BuildPlatform.deployment`
  and `link_flags`): smith's C and `.smith` objects, the shims, the prover's objects, the tree-sitter parser and its
  library, the runtime pieces. `otool -l build/Darwin-arm64/smith` shows `minos 15.0` (`tests/deployment_test.smith`).
- **A newer system function is called only where it exists.** The compiles also have
  `-Werror=unguarded-availability-new` (whatever `WERROR` says), so a call to something newer than macOS 15 is a build
  error. The fix is the older form that 15 has, or `__builtin_available(macOS 26.0, *)` with a fallback:
  `smith_platform_spawn_chdir` (`shim/platform_macos.h`) calls `posix_spawn_file_actions_addchdir` on 26 and the
  `_np` form before.
- **Programs smith builds for users are not changed**: they keep the user's SDK and deployment target
  ([sdk-rule](sdk-rule.md)). Only smith's own binary has the floor.
- **LLVM's objects are built for the macOS that built them** (`scripts/llvm.sh`, the LLVM release asset, #797): until
  that build has the same floor, the link says `ld64.lld: warning: ...(x.o) has version 27.0.0, which is newer than
  target minimum of 15.0.0` for each such object. The binary's `minos` is 15.0 all the same, but it is only safe on 15
  once LLVM, Z3, Alive2 and Enzyme are built with `-mmacosx-version-min=15.0` too.
