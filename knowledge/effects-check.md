---
type: contract
title: smith-effects, a program touches the world only through capabilities
description: A clang-tidy check decided function by function from the source: raw effects live only in a capability's module, and io::make is called only in main.
tags: [lint, effects, io]
---
# smith-effects, a program touches the world only through capabilities

**`smith-effects` is a clang-tidy check that a program touches the world only through capabilities passed down from
`main`** (`shim/effects.h`, registered in `shim/tidy.cpp` beside `SmithNaming`, so `smith lint` and `smith lsp` both
run it, `.smith` names as written; skill smith-io). `main` makes `io::Io io = io::make(argc, argv)` (`SmithIo io = smith_io_make(argc, argv)` in C), or in a `.smith` file is `i32 main(io::Io *io)`, which the lowering turns into a `static` function and a C `main` that makes, passes and closes the io (issue #52; `io::make` in it is refused), and a function
takes the part it needs, so its signature says what it can do. Decided function by function from the source, with no
lists of files:
- **A raw effect is a call into a system header that is not on the pure allow list** (in `shim/effects.h`, each
  entry with its reason: `<string.h>`, `<math.h>`, `<ctype.h>`, the `strto*` family, `snprintf`, `qsort`...), or an
  Objective-C message to a system framework's class or object. `abort`, `assert`, `__builtin_trap` and smith's failure
  path are not effects.
- **A capability is a record marked `[[clang::annotate("smith::capability")]]`** (`io::Io`, `io::Console`, `io::Files`,
  `io::Clock`, `io::Processes`, `io::Net`, `io::Args` (the live environment), and the handles `io::Op`, `io::Capture`,
  `io::Child` and `io::Connection`, C's SmithIo and the like; not `random::Key` or `arena::Arena`, which are values). Raw effects are allowed in its implementation: the `.smith`
  module that defines it, or the `.c` beside its header (`io.c` with `io.h`; `lib/io.c` with `include/.../io.h`); `smith::io` is itself such a `.smith` module. An
  exported function there that reaches a raw effect must take or return a capability. `io::make` (`smith_io_make`) is marked
  `[[clang::annotate("smith::main_only")]]` and may be called only in `main`; no capability in static storage.
- **Default on, `smith-effects.CFiles` true;** smith's own `.clang-tidy` sets it false, because smith's C is being
  ported to `.smith` (issue #45) and each ported module must be clean from the day it lands. The editor runs it
  because `patches/smith-editor-fast-check.patch` makes clangd treat `smith-*` checks as fast (LLVM `23.1.3-r4`).
- **The stack is solved at the link, from the same summaries.** Each compile to an object asks the backend for frame sizes (`-fstack-usage`, added by `smith_clang_main`, so no LLVM change; for one command that compiles and links the `.su` the driver names after the program is read after each job); the plugin copies them into the object's `.fx` (`S` lines, an `H` line with the opt level and sanitizers; a frame is `dynamic` when the body has a variable-length array or `alloca`, not by the backend's word, which a sanitizer's realigned arrays also earn) and removes the `.su`. The link solves `stack(f) = frame(f) + max stack(callee)` over the merged graph and writes `<program>.stack`: musttail back edges add nothing, other cycles and dynamic frames are `unbounded`, and code with no source is `at least N + unknown (calls: ...)`, never a guessed number (the task runtime, #241, decides what an unknown is). The file records the build's flags because a frame at `-O0` with sanitizers is larger than at `-O2`.
