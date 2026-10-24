---
type: design
title: Found by porting game, the checkers example, what was measured and the open decisions
description: What porting game and checkers changed in the build API, the 23 cases summarized, measurements, pitfalls and the open decisions.
tags: [build, history]
---
# Found by porting game, the checkers example, what was measured and the open decisions

game's Makefile was ported to a `build.c` of about 1,000 lines. The port worked around these gaps, and each is now in
the API:
- **Multi-line command words.** The Makefile's recipes are shell scripts, and the port joined their lines with a
  heuristic. A word may now hold newlines; smith moves such a command into `.smith/command/<hash>`.
- **`smith_write`.** The unity files and the test list are computed by `build.c`, and the Makefile wrote them with a
  temporary file and `cmp`. The mtime now moves only when the content does, with no `.always` step that prints on every
  no-op build.
- **`smith_capture_or`.** `$(shell git ... 2>/dev/null)` yields a fallback outside a repository, but `smith_capture`
  is fatal when the command fails.
- **An identical step declared twice is accepted.** game's bakers share object files such as `sim/noise`, and the port
  kept a list of what it had declared. The same holds for a task or an alias.
- **`/` in task and alias names.** The check-alone tasks are named after files, such as `check-alone/src/base/arena.c`.
- **Options can be read again.** A `smith_defer` callback has no context, so it could not read the options that
  `build()` read.
- **`smith_glob` takes a list of patterns.** `$(sort $(wildcard a b c))` needed a sorting, de-duplicating helper.

Not changed: lazy values such as `$(xcrun --show-sdk-path)` (the port wraps the command in `sh -c`), quiet `.always`
steps and `.content_hash`.

## Example: checkers

checkers builds with this `build.c`, which replaced its Makefile command for command:
```c
// The build of checkers, run with `smith build`. The commands are the ones the Makefile ran.
#include <smith/build.h>

void build(SmithBuild *b) {
    SmithStep checkers = {
        .command = (const char *[]){"smith-cc", "-std=c23", "-MJ", "checkers.json", "-g", "-O0", "checkers.c", "-o",
                                    "checkers", nullptr},
        .inputs = (const char *[]){"checkers.c", nullptr},
        .outputs = (const char *[]){"checkers", "checkers.json", nullptr},
    };
    smith_step(b, &checkers);

    // clangd reads compile_commands.json: the -MJ fragment inside brackets, without its trailing comma.
    SmithStep compile_commands = {
        .command = (const char *[]){"sh", "-c",
                                    "echo '[' > compile_commands.json && "
                                    "sed '$ s/,$//' checkers.json >> compile_commands.json && "
                                    "echo ']' >> compile_commands.json",
                                    nullptr},
        .inputs = (const char *[]){"checkers.json", nullptr},
        .outputs = (const char *[]){"compile_commands.json", nullptr},
    };
    smith_step(b, &compile_commands);

    SmithStep play = {
        .command = (const char *[]){"./checkers", nullptr},
        .inputs = (const char *[]){"checkers", nullptr},
        .pool = "console",
    };
    smith_task(b, "play", &play);

    SmithStep clean = {
        .command = (const char *[]){"rm", "-f", "checkers", "checkers.json", "compile_commands.json", nullptr},
    };
    smith_task(b, "clean", &clean);

    smith_default(b, smith_cat(b, checkers.outputs, compile_commands.outputs, nullptr));
}
```

- `smith build` builds `checkers` and `compile_commands.json`; `smith build play` builds what is needed, then runs the
  game on the terminal every time; `smith build clean` removes what the Makefile's `clean` removed. `smith build -t
  clean` removes the same three files, because each is a declared output.
- Compared in one scratch folder, make against `smith build`: the machine code is identical, and `checkers.json` and
  `compile_commands.json` are identical once the temporary object's name is set aside. clang compiles and links in one
  step through a randomly named temporary object, so the binary's UUID, its debug map (that object's name and mtime) and
  its signature differ between two make builds as well, in the same places.

## The 23 cases

Cells come from each case's "How each handles it" line. "native" is built in, "workaround" is possible with extra
machinery written by the user, "partly" is some of the behaviour, "no" is not possible on its own. The smith column is
what a `build.c` author writes, so `system1` is a workaround in ninja but native in smith, through `.always`.

| case | what it tests | make | ninja | Zig 0.17.0 | smith | smith API |
|---|---|---|---|---|---|---|
| basic | copy a file; rerun if the input changed or the output is missing | native | native | native | native | nothing new |
| parallel | two independent outputs at the same time | native (needs `-j`) | native | native | native | nothing new |
| include | rebuild when a transitively included header changes | workaround | native | native | native | `.depfile` (v1) |
| wildcard | build `X.out` from `X.in` for any `X` | native | no | workaround | native | `smith_glob`, `smith_replace_extension` (v1) |
| spaces | file names with spaces | workaround | native | native | native | nothing new |
| monad1 | depend on the files named in another file | native | no | workaround | native | `smith_lines` (v1.5) |
| monad2 | a generated list of inputs that already exist | native | native | native | native | nothing new |
| monad3 | a generated list names files built by steps that exist only if the list says so | native | workaround | workaround | native | `smith_defer`, `smith_lines` (v1.5), `smith_map` (v1) |
| unchanged | a generator reruns but writes the same bytes | partly | native | partly | native | `.restat` (v1) |
| multiple | one step writes two files, only one changes | workaround | native | no | native | `.restat` (v1) |
| system1 | a step that runs every build; dependents rerun only if its result changed | native | workaround | native | native | `.always`, `.restat` (v1) |
| system2 | rerun when the output is missing or an environment variable changed | workaround | workaround | workaround | native | `smith_env` (v1) |
| pool | limit one stage to 2 concurrent runs | workaround | native | workaround | native | `smith_pool`, `.pool` (v1) |
| digest | do not rerun when the input is rewritten with the same bytes | workaround | workaround | workaround | later | `.content_hash` (later) |
| nofileout | rerun if and only if the input changed, with no output file | workaround | workaround | native | native | `smith_step` (v1) |
| noleftover | delete outputs that are no longer produced | no | workaround | workaround | workaround | nothing new; the user runs `smith build -t cleandead` |
| secondary | keep an auxiliary file out of the rebuild decision | native | workaround | partly | workaround | `.implicit_inputs`, `.order_only_inputs` (v1) |
| intermediate | an auxiliary file deleted at the end, not recreated | native | workaround | native | workaround | nothing new |
| variants | one program built twice (plain and sanitized) | workaround | native | native | native | `smith_cat`, `smith_fmt` (v1) |
| tool | build a program, run it, `#embed` its output | native | native | workaround | native | nothing new |
| options | a task with values from the command line | native | no | native | native | `smith_option_value`, `smith_task`, `smith_when` (v1) |
| function | a step written as a C function | workaround | workaround | workaround | native | `.run`, `.args`, `smith_run_lines`, `smith_run_status` (v2) |
| platform | a source file and a flag that differ by platform | native | no | native | native | nothing new |

- make and ninja: measured, except make in `digest` and `noleftover` (not run; they have the same shape as the standard
  workarounds) and Linux in `platform`.
- Zig: run on 0.17.0.
- smith: checked by running the hand-written `build.ninja` that the `build.c` would generate. `build.c` itself
  has not run in these cases; the v1 implementation has its own tests. In `function` the ninja file ran against a stand-in for the build program, and the replay step is not verified.
  In `monad3` the two manifests were written by hand, and the regeneration was done by a script standing in for the
  build program.
- Every smith snippet is a complete `build.c` that passes `smith cc -std=gnu23 -fsyntax-only -Wall -Wextra -Werror` against
  `smith/build.h`. That checks the API compiles, not that it works.
- Full cases: [the 23 cases](build-cases.md).

## What we measured

Measured on macOS with Apple clang 21, GNU Make 3.81 and ninja 1.13.2.
- A no-op `make` in checkers, learner and game takes 0.05 to 0.11 s.
- game's clean `make check` takes 18.5 s, of which the static analyzer is 14.5 s.
- So ninja alone would not make these projects faster. The real wins would be early cutoff (do not rerun what depends on
  an output that did not change) and a content cache (do not rerun a step whose inputs have bytes seen before).

The Zig 0.17.0 findings that shaped the design, from the cases:
- Configure caching. `build.zig` is not rerun on every `zig build`. A read of a directory, a file or the environment is
  missed unless declared. smith v1 reruns `build.c` on every `smith build` instead, so no read can be missed.
- Arguments are compared by size, inode and mtime. A same-size in-place edit that keeps the mtime is missed, and a
  same-bytes rewrite reruns the step. Files named in a depfile are compared by content. This is why `digest` works only
  through a depfile in Zig.
- `multiple` fails. Both outputs of one step share a cache folder named by a hash of the step's inputs, so any input
  change reruns both consumers. ninja with `restat` and make with a pattern rule rerun only one.
- There are no custom steps (`function`). A check written in the build language has to run as a shell script or a
  program.
- A deleted cache file is not recreated (`secondary`, `system2`). The build fails until the inputs change.

## Pitfalls found

- **Manifest regeneration is decided by mtime only.** If smith rewrites a stage 1 manifest over an up-to-date tree, the
  new manifest is newer than `list`, ninja sees nothing to do, and `output` never appears. Verified, even with a
  different command string. The fix, verified: set the stage 1 manifest's mtime to 1970 after writing it, or call `more`
  only when its needs already exist.
- **`-n` on a fresh tree shows only stage 1.** A dry run cannot reload the manifest. The `-t` tools and `-n`
  see whichever manifest is on disk (the executor and ninja alike).
- **A generated `#embed` or `#include` file must be an implicit input**, not left to the depfile. LLVM 23 clang with
  `-MMD -MF` and a missing `#embed` file exits 0, writes a 512-byte object and no `.d` file. Without `-MMD` it is a
  fatal error, and Apple clang 21 fails correctly. With only the depfile, a clean build "succeeded" with 2 edges and
  bake never ran.
- **`smith build -t cleandead` only knows files in the log** (`.smith/.smith_log`). If it is deleted, old outputs stay.
  A file that smith used to build and the project now keeps as a hand-written source at the same path stays, since the
  manifest names it (ninja's tool deleted such a file). smith never runs it by itself.
- The generator edge needs `generator = 1`, so that `-t clean` keeps the manifest.
- A task named `test` next to a folder named `test` still ran in ninja 1.13.2, but this is not established to be safe.
  So a task is an edge with output `.smith/task/test` plus `build test: phony .smith/task/test`.
- A value read with `smith_env` ends up in `build.ninja` and `.smith/.smith_log`. That matters if it is a secret.

## Bootstrapping

- (History: until issue #112 a fresh clone built a stage0 with one command, `cc -std=gnu23 smith.c -o smith`, and the
  libraries were compiled from a committed lowering; smith is now built by its previous release, see
  [smith-bootstrap](smith-bootstrap.md).) smith's own C stays plain C23 with libc only, and allocates nothing. The stage0
  had no C++ and no LLVM, and ran an external clang. `smith build llvm` then builds our static LLVM (once, about 9 minutes), and
  `smith build` links the full smith: `smith.c` with `-DSMITH_IN_PROCESS`, `shim/clang.cpp` (the only C++) and the
  archives of the LLVM install, with `-Wl,-dead_strip`. The stage0 then runs smith's own `build.c`, which is
  the second project built by `build.c`, after checkers: `smith build`, `smith test`, `smith check`,
  `smith build install` and the rest replaced the Makefile's targets, with the same compile command and the same
  machine code.
- The same command is the way back when an installed smith is broken and `smith build` cannot run, and the first build
  of a fresh clone: `mkdir -p build && cc -std=gnu23 smith.c -o build/stage0 && build/stage0 build install -DCC=cc`. It
  needs only a C23 compiler.
- Other projects need smith installed to run their `build.c`, as a Makefile needs make. So does smith: the installed
  smith must have `smith build`.

## Open decisions

- **`b->os` is the host.** Cross-compiling needs a target too, next to `os` (case `platform`). Zig has a separate target
  and host; make's `uname` has the same fault.
- **`smith_capture` reruns its command on every `smith build`**, as the whole program does. It costs about 0.04 s for
  `brew --prefix llvm`. make ran it only when a target needed it. Options: a cache by command, or let `build.c` skip the
  call unless a target needs it.
- **A cache of the build program's result.** v1 reruns it every time. A cache would track the `smith_*` reads and rerun
  `build.c` only when one changed.
- **Whether to enforce reads through the `smith_*` calls with a lint rule on `build.c`.** It would flag `getenv`,
  `fopen` and `popen`.
- **`content_hash` versus `restat`.** `restat` cuts off what comes after a step that already ran. `content_hash` would
  stop the step itself, with hash edges generated by smith (case `digest`). Neither covers files found through a
  depfile.
- **Compiles in the executor's own process (#1).** Today a compile is a process like any job; [The
  executor](executor.md) says how a job can later run in this process.
