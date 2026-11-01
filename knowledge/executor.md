---
type: design
title: The executor
description: The executor reads the subset of ninja's language that smith writes, schedules and decides rebuilds itself, and keeps its state in .smith.
tags: [build, executor]
---
# The executor

`smith build` runs the manifest itself: no ninja is installed or run. The code is `runtime/executor.c` and the five files
it includes (`executor_manifest.c`, `executor_state.c`, `executor_plan.c`, `executor_run.c`, `executor_tools.c`), which
`smith.c` includes; it stands on smith's private io layer, compiled from `runtime/io.smith` and linked (until #888). It uses
that layer (`<runtime/io.h>`, on kqueue on macOS and epoll on Linux): the manifest and the graph live in `io.arena`, the
files are read and written through `io.files`, and one event loop waits for every job's process and pipe.

## Who runs the graph

smith parses the `.smith/build.ninja` that build.c's program wrote; the program does not run the graph.
- **Reuse.** The manifest is already the contract between the program and the runner: the program stays a small
  generator that exits, the runner is one binary for every project, and `SMITH_NINJA` can still run the same file. The
  build program's own compile (`build.c` into `.smith/program`) is a manifest too, so one runner does both; a program that ran
  its own graph could not compile itself.
- **Simplicity.** The program runs `build()` on every `smith build` (globs, options and captures are read fresh), so
  it cannot also be a long-lived scheduler: when a `smith_defer` need changes mid-build, the build has to run the program
  again and read its new manifest. That is one loop with a file between the two; with the graph in the program, it would
  be `build()` run twice in one process, whose arena and tables are file-level statics.
- **Speed.** Reading the manifest costs 4 ms for game's 886 KB (1,378 edges); the no-op build is dominated by running
  `build()` (about 100 ms in game), which a program-side executor would not avoid. The executor is compiled with smith, at
  the flags smith is built with, and not at `-O0 -g` as every project's build program is.

## What is kept between builds

Two files in the manifest's `builddir` (`.smith/`), text, appended to as jobs end and compacted when more than two
thirds of the lines are dead:
- `.smith/.smith_log`: `# smith log 1`, then one line per output and run, `<start ms> <end ms> <mtime ns> <hash> <path>`.
  The hash is FNV-1a 64 of the command. The mtime is the time the command started, on the file system's clock (ninja's own rule:
  an input modified after that, even while the command ran, makes the output out of date); a restat step that changed
  an output records that output's time if it is later. This is `.ninja_log`'s job (a changed command reruns the step). The durations are kept
  for whoever wants them; the jobs start in ninja's order, the longest chain of commands after them first.
- `.smith/.smith_deps`: `# smith deps 1`, then per output `<mtime ns> <count> <path>` and `<count>` lines, one path
  each: what the depfile named. This is `.ninja_deps`'s job. The depfile itself is removed once read.

A line cut short by a crash is not read, and a file of another version is as good as none. **An upgrade from a tree that
ninja built is correct:** the executor never reads `.ninja_log` or `.ninja_deps`, so every output has no log line and the
first build runs every command. After that nothing differs. The other way works too: with `SMITH_NINJA` set, ninja
finds its own logs, which are older than the files smith made since, so it reruns at most what its log says changed; a
tree is never left wrong, at worst built twice.

## The step cache

A compile (an edge with `deps = gcc`, one object and at most its `.fx` effects summary, `-MD` and `-MF` in a command with
no shell feature) is served from `<cache>/smith/steps` when a build has made it before (#621, `runtime/executor_cache.c`).
Written in C, not `.smith`: it works on the executor's own types (`SmithExec`, `SmithEdge`, `SmithJob`), which move to
`.smith` with the executor (#45); its one pure part, SHA-256, is `runtime/package.c`'s.
- **Two levels, as ccache's direct mode.** The inputs of a compile are known only after it ran, so a *manifest*
  `m/<xx>/<key>` (key = SHA-256 of the command with the folder a token, the compiler's build-id or LC_UUID, the
  environment (`SMITH_LANG_FLAGS`, `SMITH_LANG_INPUTS`, `SDKROOT`, `SMITH_CLANG`, `CPATH`, ...) and the content of the
  step's explicit and implicit inputs) keeps up to 8 *variants*: the files a compile read and a spec of each. A *result*
  `r/<xx>/<rkey>/` holds the object, the `.fx` and the depfile of a variant.
- **The compile writes `-MD`**, so the depfile names the system's headers too. The graph tracks what `-MMD` named: after
  the command ends the executor drops the system's files from the list it logs (`.smith_deps`), so a no-op build stats
  no more than before (system = under `/usr`, `/lib`, the macOS SDK and Xcode, smith's embedded runtime, or a folder the
  command names with `-isystem`, `-isysroot`, `--sysroot`, `SDKROOT`; a folder named with `-I`, `-iquote`, `-idirafter`,
  `-F` is the project's even then). A project file is keyed by content (with its folder a token, so the generated module
  map of two folders is one file), a system file by path, size and time.
- **A hit is what a rebuild makes.** A compile that writes debug info (`-g`) is given `-ffile-prefix-map=<project folder>=.`
  by the manifest (build.c's `smith_render_command`, for smith's own build and every project's alike; a command that
  names a prefix map itself keeps its own), so the object holds no folder: a `debug` object is shared by every worktree
  like a `release` or `fast` one (#1351). An output that still holds the folder as bytes (a compile that names it
  some other way) is stored under a key that has the folder in it: only a build in that folder is served it. `smith dbg`
  and `smith mcp` map `.` back to the project (lldb's `target.source-map`) so a source is shown from any folder
  ([debugger](debugger.md)). The `.d` and `.fx` are stored with the
  folder a token and restored with this one. A compile that printed anything (a warning), read a file written after it
  began, or names `__TIME__`, `__DATE__` or `__TIMESTAMP__` is not stored (so `runtime/executor_cache.c`, which smith.c includes, writes the three names in pieces, #1414); nor are compiles with a flag that reads or
  writes more than the depfile names (`@file`, `-fprofile-*`, `--coverage`, `-save-temps`, plugins, ...), nor a
  precompiled header, read or written (`-include-pch`, `-x *-header`, a `.pch` or `.gch`) without
  `-Xclang -fvalidate-ast-input-files-content` (the only `-Xclang` argument accepted): a header records each input's
  time, so one restored after a checkout gave its header a new time is rejected by every compile that includes it
  (#728); with the flag it stores each input's hash and accepts a file of the same text (#1415). A header holds the
  absolute path of the file it was made from, so it is cached across worktrees only when that path is the same in all
  of them (build.c makes it from a copy of `shim/pch.h` in the package cache); one that holds the root is stored for
  that root alone, as any output that does. The cache hashes a `.pch` read by a compile as any input, and does not
  refuse it for naming `__TIME__` (a header's tables do, whatever it was made from).
- **Restoring** clones the files (FICLONE on Linux, `clonefile` on macOS, else a copy), writes the depfile as a compile
  would, and the job ends as one does: the depfile log and command log are written, `-v` shows `(cached)` after the
  step, `-d explain` says it was restored. A hit takes no core of the host's pool. Stores and restores are a temporary
  file and a rename, so builds in other worktrees can share the folder. A restored output gets the time of the restore, not
  the cached file's old one, so it is not older than its inputs; a step that is not restat also re-reads the time of its outputs when it
  ends, so a restat step that reads them records a time that is not older than they are, and a tree just built settles (#816). A header
  step run by a bare-named compiler waits for `.smith/compiler/<hash>` (the compiler's identity), not for its file (the runtime swaps the input).
- **The build program is served too.** `smith build` compiles `build.c`, the embedded runtime and the libraries it links
  into `.smith/program` through the executor, and those compiles are cacheable: `-MD` (not `-MMD`, which the cache
  refuses) and `-fdebug-compilation-dir=.`, so the `-g` objects hold no folder and a fresh project with the same build
  file is served all of them (about 0.6 CPU-s of Linux compile; the link always runs). The tests' runs have the cache
  on, in the run's shared `$SMITH_TEST_CACHE` (`tests/scratch.smith`); `scratch::uncached(&p)` turns it off for a test
  that counts the compiles of its own fixture.
- `SMITH_STEP_CACHE=0` turns it off. An entry unused for 31 days is removed, and the results are kept within `SMITH_CACHE_MAX` (#618), checked at most once an hour (`smith cache`
  shows the folder; `smith cache clean steps` empties it). Like the depfile model it cannot see a header that appears
  earlier on the include path, or (with `-fmodules`) a system header that only a module reads. Module header steps and
  lint are not covered yet.

## Keep writing `build.ninja`

Yes. It is the executor's input, so it is not a fallback format that can be dropped; what stays optional is ninja. Keep
`SMITH_NINJA=<ninja>` for one release cycle: it costs 20 lines in `smith.c`, and it lets anyone check the executor
against ninja on the same file (`tests/build_*_test.smith` and the generated-graph test do). Remove it, and the "ninja language"
claims in `docs/build-c.md`, when the manifest's last producer other than smith is gone.

## Parallelism, and a job that is not a process

`-j` is ninja's default, the cores plus 2 (3 for two cores, 2 for one), and at most 128. Each running job has a slot
with a `SmithJob`, a `SmithJobKind` (`start`, `poll`, `stop`: three functions) and 256 MiB of address space for its
output, reset when it ends. The loop is: start ready jobs while there are slots and the pools allow, wait on the io
loop (50 ms at most, so a signal is seen), poll every running job, finish those that ended. A job that runs in this
process later (a compile with clang inside smith, #1) is another `SmithJobKind`: `start` queues it on a thread or a
worker, `poll` looks at its done flag (a user event on the loop wakes it), `stop` cancels it; the planner,
the pools, the progress line and the log do not change. Threads against worker processes for #1 is not decided here.

## What ninja does, and what smith does with it

| ninja | smith |
|---|---|
| rebuild when an output is missing, older than an input, or its command changed | kept: the same rules, including ninja's for `restat` outputs and for `generator` edges |
| depfiles (`deps = gcc`) | kept: read when the command ends, kept in `.smith_deps`, removed; a missing depfile is no dependencies; one that does not name the first output fails the step |
| `restat` | kept: an unchanged output cleans the edges that depended on it only, and the time kept is the newest input's |
| `generator`, manifest regeneration and reload | kept: the manifest's own edge is built first, and the manifest is read again when that ran (8 times at most) |
| order-only and implicit inputs, multiple outputs, `phony`, `default` | kept |
| pools, `console` | kept; a console job gets the terminal and the build's stdin, and the progress line and other output wait until it ends |
| `-j N`, `-k N`, `-n`, `-v`, `--quiet` | kept; the first failure stops starting jobs, `-k N` stops at the N-th, `-k 0` never |
| the progress line | kept: `[finished/total] description` (the command with `-v`), overwritten in place on a terminal and one line per job on a pipe; the total falls when a restat step cleans edges |
| each job's output printed whole | kept: stdout and stderr in one pipe, printed when the job ends; a job whose output outgrows its 256 MiB is cut, with a note |
| a failing job's command and output | kept: `FAILED: [code=N] <outputs>`, the command, the output; `N` is 128 plus the signal for a killed job |
| Ctrl-C | kept: SIGINT, SIGTERM and SIGHUP stop starting jobs, send the signal to every job's process group (escalating to SIGKILL after 5 s), remove the half-written outputs (as ninja does: those that changed, and everything a depfile step wrote), and exit 128 plus the signal (130 for Ctrl-C) |
| `-t clean [-g] [-r rule] [targets]`, `-t compdb [rules]`, `-t targets [all\|rule R]`, `-t commands <targets>`, `-t deps [targets]`, `-t cleandead` | kept, with ninja's output; `cleandead` leaves a file that the manifest still names |
| `.ninja_log` / `.ninja_deps` (binary deps log) | replaced by `.smith_log` / `.smith_deps` (text), above |
| the default job count | the same: cores + 2 |
| `-d explain` | kept: says on stderr why each edge is dirty, and an output made dirty by an input says `X is dirty (first cause: Y)`, Y being the first output or missing source whose own reason was printed |
| `-l`, `-w`, other `-d` modes | read and ignored |
| `-t graph`, `browse`, `query`, `inputs`, `rules`, `recompact`, `restat`, `msvc`, `multi-inputs`, `-f`, `-C` | dropped: no project here uses them (`-f` and `-C` were already refused) |
| `include`, `subninja`, `rspfile`, `deps = msvc`, `dyndep`, `$in_newline`, one rule's variable naming another's | dropped: smith writes none of them. `SMITH_NINJA` runs a manifest that has them |
| `NINJA_STATUS`, colour | dropped |
| output capture limit | new: 256 MiB per running job, at most 128 jobs |
| a console job's stdin | changed for the better: ninja's console jobs keep it too, and so does smith's (checkers' `smith run` reads it) |

`smith --smith-exec <manifest> [flags and targets]` runs any manifest the executor reads, in the current folder, with no
`build.c`; the tests use it.

## Measured

On macOS with Homebrew's clang 23.1.3, the same smith run both ways, in two trees of each project at one commit (one
tree cannot serve both: each way sees the other's outputs as built behind its back, and rebuilds), the runs interleaved
because other builds ran on the machine too, which makes the long ones noisy. The executor is the second number.

| | ninja | executor |
|---|---|---|
| no-op, game (30 runs, median) | 109 ms | 110 ms |
| no-op, learner | 63 ms | 62 ms |
| no-op, smith | 15 ms | 11 ms |
| no-op, checkers | 15 ms | 12 ms |
| clean build of the default target, smith (3 runs, median) | 672 ms | 662 ms |
| clean, checkers | 109 ms | 107 ms |
| clean, learner | 11.57 s | 11.72 s |
| clean, game (5 runs, fastest, machine busy) | 5.32 s | 5.46 s |
| `check test` from clean, learner (4 runs, fastest, machine busy) | 35.17 s | 35.13 s |
| 1,000 jobs of `true && touch`, `-j8` (median) | 655 ms | 670 ms |

game's no-op is about 100 ms of `build()` and 5 ms of executor (ninja: 8 ms); the others are the same shape. A job costs
the scheduler about 0.65 ms, the process it starts, under ninja and the executor alike. Jobs start in ninja's order (the
longest chain of commands after them first): ordering by the logged durations was tried and was faster in game's clean
build and slower in learner's `check test`, each by under 10 %, so it was dropped. Touching one header gave the same
jobs under both (game's `math.h`: 172 for the default target and 309 for `check`; learner: 7 and 3; smith: 3). Built
twice from clean in one folder, game (548 files), learner (243) and checkers differ only in their linked binaries
(the linker's UUID and the object files' times, 48 bytes each, as between two ninja builds) and learner's metallibs (not
reproducible); every object and generated file is the same.

A no-op build of smith itself (nothing changed, the smith in the tree unchanged) is 0.13-0.27 s wall and 0.08 s CPU
(#639): `build()` and the manifest, and no step. What costs more is the smith that built the tree changing: its link is
newer than every header step's output, so each runs again to find its bytes the same, and a smith process costs about
10 ms to start whatever it does. The headers of the tests are one step, `smith-cc --smith-headers` (see
[smith-module-headers](smith-module-headers.md)): 0.3 s of CPU where 116 steps took 1.9 s.

## How it was checked

- `tests/build_*_test.smith` has 89 checks of the executor (a failed job, `-k`, depfiles, restat, pools, parallel output, Ctrl-C,
  the tools, malformed manifests, the logs and the upgrade from ninja); `tests/io.c` has the process capture.
- `python3 tests/ninja_compare.py <smith>` compares the executor with ninja on random manifests (plain, restat, depfile
  and multi-output steps, implicit and order-only inputs, aliases, defaults), each edited at random between builds (a
  source changed or only touched, an output deleted or touched, a header a depfile named, a depfile's list, a command, a
  single target asked for) and built with both: the commands run and the files made must be the same in every build. It
  needs ninja, so it is run by hand when the executor changes, not by `smith test`.
