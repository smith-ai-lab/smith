---
type: convention
title: smith runs on macOS 26 and newer
description: smith's own binary is built with -mmacosx-version-min=26.0 whatever macOS builds it, a newer system function is a build error, and programs smith builds for users keep the user's target.
tags: [macos, build, release]
---
# smith runs on macOS 26 and newer

**smith is built for macOS 26 and newer, on any newer macOS** (owner, 2026-10-29: "Let's make 26 the floor"; #802).
26 is one release behind the newest and what GitHub's free `macos-26` runners have. Without a deployment target the
binary needs the macOS that built it: the stage0 release made on macOS 27 did not start on an older macOS
(`dyld: Symbol not found: _posix_spawn_file_actions_addchdir` on `macos-15`).

- **`build.c` gives every compile of smith itself and its link `-mmacosx-version-min=26.0`** (`BuildPlatform.deployment`
  and `link_flags`): smith's C and `.smith` objects, the shims, the prover's objects, the tree-sitter parser and its
  library, the runtime pieces. `otool -l build/Darwin-arm64/smith` shows `minos 26.0` (`tests/deployment_test.smith`).
- **A system function newer than 26 is a build error.** The compiles have `-Werror=unguarded-availability-new`
  (whatever `WERROR` says); the fix is the form 26 has, or `__builtin_available` with a fallback.
- **Programs smith builds for users are not changed**: they keep the user's SDK and deployment target
  ([sdk-rule](sdk-rule.md)). Only smith's own binary has the floor.
- **LLVM's objects are built for the macOS that built them** (`scripts/llvm.sh`, the LLVM release asset, #797): the
  pack smith links is built on GitHub's `macos-26` runner, so its objects match the floor. One built on a newer macOS
  makes the link warn `has version 27.0.0, which is newer than target minimum of 26.0.0` for each object.
