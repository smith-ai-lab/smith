---
type: architecture
title: smith build and build.c
description: smith build runs a project's build.c, which writes a ninja manifest that smith's own executor runs, with everything explicit and no macros.
tags: [build, build-c]
---
# smith build and build.c

**`smith build` runs a project's `build.c`, which writes a ninja manifest, then runs that manifest itself, with no
ninja.** The executor (`runtime/executor*.c`, on smith's private runtime layer `runtime/io.smith`) reads the subset of ninja's language that smith
writes and keeps its own state in `.smith/`; `SMITH_NINJA=<ninja>` runs the manifest with ninja instead. It is
recognised only as the exact first argument. The header and runtime are embedded in smith with `#embed`, so the stage0 (smith without
clang inside: no C++, no LLVM, it runs an external clang) still builds with one `cc` command. Design and API: [the build file](build-file.md).

Status: v1, v1.5 (`smith_defer`, `smith_lines`), C-function steps (`.run`, `.args`, `smith_run_lines`,
`smith_run_status`) and the changes found by porting game (below) are implemented. checkers and smith itself build with
it. `.content_hash` is declared but not implemented. The
23 cases that shaped it, each in make, ninja, Zig 0.17.0 and a smith `build.c`, are in
[the 23 cases](build-cases.md).

## What smith build is

- `build.c` describes the build. It is a C program, so loops, functions and `if` are the language, not a macro system.
- `smith build` compiles `build.c` with one fixed command of its own, runs it, and the program writes `build.ninja`.
- smith then runs that file itself, with its own executor ([The executor](executor.md)): the scheduling, the rebuild
  decisions and the parallelism. No ninja is needed; `SMITH_NINJA=<ninja>` runs the same file with ninja instead.
- It replaces the Makefile. It is step 2 of [adopting smith](adoption-path.md): it only describes the build, so
  it changes nothing the build does.

## Principles

- **Everything is explicit and there are no defaults.** Commands are written out in full. Replacing a Makefile with a
  `build.c` changes nothing. In particular, object paths stay the same as the Makefile's, because the linked binary
  records them.
- **No macros.** A list is a C23 compound literal that ends in `nullptr`. A loop or a function builds the rest.
- **C23, libc only, no malloc** in the build program. Strings and lists live in smith's arena until `build` returns.
- **build.c reads the outside world through the `smith_*` calls**: `smith_option`, `smith_option_value`, `smith_env`,
  `smith_glob`, `smith_capture`, `smith_capture_or` and `smith_lines`. These are the calls a later cache would track, and the checks on
  `build.c` (a lint rule, the option listing) know about them. In v1 `getenv` and `fopen` would also work, because the
  program reruns on every `smith build`. Use the calls anyway, so the build keeps working when a cache arrives.
- **The build program runs on every `smith build`.** Globs, environment variables, captured commands and options are
  always current, and nothing is recorded or compared.
- Zig 0.17.0 shows the failure of the alternative. It caches the result of running `build.zig`, and it silently missed a
  new file, a changed list and a changed environment variable whenever the read was not declared (measured: `wildcard`,
  `monad1`, `system2`). That is why v1 reruns instead of caching.

## Decisions

- `smith build` runs build.c, then its own executor, with no ninja (`SMITH_NINJA` is the way out).
