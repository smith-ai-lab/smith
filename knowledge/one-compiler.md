---
type: architecture
title: One compiler, in smith's own process
description: smith ships upstream LLVM 23.1.3 linked into itself, compiles C, C++ and Objective-C in its own process, and picks other compilers only through environment variables.
tags: [compiler, llvm]
---
# One compiler, in smith's own process

**One compiler, upstream LLVM: the one smith ships.** It is smith's own static build of LLVM 23.1.3 (`smith build
llvm`), linked into smith, which compiles C, C++ and Objective-C in its own process, with no LLVM install needed at
run time. Homebrew's `llvm` 23.1.3, held with `brew pin`, is what a smith without clang inside (the stage0) runs.
`.metal` goes to Apple's `metal` (`xcrun -sdk macosx metal`, or `$SMITH_METAL`), and `.metal` and other inputs in one
command are refused.

**smith compiles in its own process**, by running clang's own `clang_main` on the arguments (`shim/clang.cpp`), so
every flag, diagnostic, exit status, signal and crash report is clang's, and so are pipes. `SMITH_CLANG` runs an
external clang instead, `execv`ed with its full path as `argv[0]` (gcc finds its files from it), as `.metal` runs
Apple's `metal`.

**A change of compiler rebuilds every object it compiled.** An object depends on a file in `.smith/compiler/` that
names its compiler, rewritten only when that changes. The compiler is its executable (path, size and mtime), but for
smith it is the clang it runs, not smith's own file: the LLVM version and a hash of the archives linked in
(`lib/smith-clang.id` of the install, compiled into smith, printed by `--smith-which`), or for `SMITH_CLANG` that
clang's file. So a smith rebuilt with the same LLVM rebuilds nothing, and a rebuilt LLVM or another clang rebuilds all.

`SMITH_CLANG`, `SMITH_CLANGXX` (an external clang, instead of the one inside smith) and `SMITH_METAL` pick the compilers, `SMITH_VERBOSE=1` prints the command, and
`smith cc --smith-which <args>` (or `smith-cc --smith-which <args>`) prints the choice without running it. `SMITH_NINJA=<ninja>` makes `smith build` run its
manifest with that ninja instead of smith's own executor.
