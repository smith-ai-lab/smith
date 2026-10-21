# smith build

Status: v1, v1.5 (`smith_defer`, `smith_lines`), C-function steps (`.run`, `.args`, `smith_run_lines`,
`smith_run_status`) and the changes found by porting game (below) are implemented. checkers and smith itself build with
it. `.content_hash` is declared but not implemented. The
23 cases that shaped it, each in make, ninja, Zig 0.17.0 and a smith `build.c`, are in
[build-c-cases.md](build-c-cases.md).

## What smith build is

- `build.c` describes the build. It is a C program, so loops, functions and `if` are the language, not a macro system.
- `smith build` compiles `build.c` with one fixed command of its own, runs it, and the program writes `build.ninja`.
- smith then runs that file itself, with its own executor ([The executor](#the-executor)): the scheduling, the rebuild
  decisions and the parallelism. No ninja is needed; `SMITH_NINJA=<ninja>` runs the same file with ninja instead.
- It replaces the Makefile. It is step 2 of the three steps in [CLAUDE.md](../CLAUDE.md): it only describes the build, so
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

## How smith build runs

The driver, in order:
1. `build.c` must be in the current folder.
2. With `$SMITH_NINJA` set, find that ninja (the fallback, below). Otherwise nothing is looked for: the executor is
   part of smith.
3. Split the arguments. Those that start with `-D` go to the build program, and everything else is the executor's:
   `-j`, `-k`, `-n`, `-v`, `--quiet`, `-t <tool>` and the targets. `-f` and `-C` are refused, because `smith build`
   names the manifest itself.
4. Write `.smith/include/smith/build.h`, `.smith/runtime.c`, `.smith/bootstrap/build.ninja` and `.smith/.gitignore`.
   Each is written only when its bytes changed, so its mtime changes only then.
5. Run the bootstrap manifest, `.smith/bootstrap/build.ninja`, with the executor (quietly). It compiles `build.c`
   into `.smith/program`, and rebuilds the program when `build.c`, a header it includes, or smith's runtime changed (a
   depfile).
6. Run the program with the `-D` arguments. `build()` calls the `smith_*` functions, and the program writes
   `.smith/build.ninja` only when its bytes changed.
7. Run `.smith/build.ninja` with the executor and the other arguments; its exit status is `smith build`'s: 0, 1 for
   a failed build, 2 for a bad command line, and 128 plus the signal for an interrupt.

The header and the runtime are embedded in smith with C23 `#embed`, so `smith build` needs nothing installed beside it.
The compile command is `<compiler> -std=gnu23 -g -O0 -Wall -Wextra -I .smith/include`, where the compiler is smith itself
(it compiles in its own process, or runs `$SMITH_CLANG`) or, for a stage0 smith, the external clang it would pick.
Bootstrapping still works with one `cc` command: that builds the stage0.

smith's own files all go in `.smith/`: the manifests, the logs (`builddir`), the build program, stamps and task
outputs. The bootstrap manifest has its own `builddir`, `.smith/bootstrap`, so its logs are separate.

### A change of compiler rebuilds everything

ninja decides that an object is out of date from its command, its inputs and their mtimes. A new smith, or another
`SMITH_CLANG`, changes none of them when the command says `smith`, so the objects would look up to date though another
compiler built them, and sanitized objects from two compilers do not link together (their ASan ABIs differ). So every
`smith_object` and `smith_program` depends on a file `.smith/compiler/<hash of the compiler word>` that says which
compiler it is:
- a compiler that is not smith: its executable's real path, size and mtime (a copy of the same bytes is a new compiler);
- smith (a program named `smith...`) is the clang it runs, not its own file, which changes with every build of smith and
  would rebuild every project after each install. `<smith> --smith-which -c a.c` says which: with clang linked in,
  `clang in-process <clang>` and `identity <llvm version> <hash>`, where `scripts/llvm.sh` hashes the archives that get
  linked (and the four objects of `libclangmain.a`) into `lib/smith-clang.id` of the install, `smith build` compiles it
  into smith (`-DSMITH_CLANG_ID=`), and so a smith rebuilt against the same LLVM is the same compiler and a rebuilt LLVM
  is another; with an external clang (`$SMITH_CLANG`, or a stage0's), `clang <path>`, and that clang's real path, size
  and mtime.

The program writes the file on every run, and `smith build` runs the program before ninja every time, but it rewrites the
file only when the text changes. ninja then sees a newer input only after a change of compiler, and a build with the same
compiler stays "no work to do". Going back to the old compiler is a change as well. `tests/build.sh` covers it: a stand-in
compiler replaced, `COMPILER` switched, smith routing to another `SMITH_CLANG`, a smith replaced by a copy (nothing
rebuilds), smith switched to an external clang and back, and a stand-in smith whose identity line changes.

They are not in `build/`, because a project's `clean` task usually runs `rm -rf build`, and that would delete the logs
while the build is still running from them (ninja then exited 1 with "Error writing to build log", measured; the
executor would lose what it just recorded). smith writes `.smith/.gitignore` containing `*`, so git ignores the folder
with no change to the project, as pytest, ruff and mypy do for their caches. smith does not create `build/`.

An unchanged manifest keeps its mtime, so a `smith build` with nothing to do does not rebuild the manifest's own
edge.

Built in, the tools of ninja's that this build uses (the full list and what is dropped is in [The executor](#the-executor)):
- `smith build -t clean` removes what the build made, the manifest's own edge aside.
- `smith build -t compdb` prints the compile commands as JSON (`compile_commands.json` is written by `build.c`).
- `smith build -t targets`, `-t commands`, `-t deps` and `-t cleandead`.
- `-j`, `-k`, `-n` and `-v` work as in ninja.
- A project's own `clean` task is just a task.

Generator rules:
- Only the whole words `$in` and `$out` expand. smith expands them itself. ninja evaluates an edge variable such as
  `cmd` when it reads the line, before the edge's `$in` and `$out` exist, so `$in` there would be empty. smith writes
  the step's input and output paths into `cmd`, each shell-quoted like a literal word. Every `$` in `cmd` is doubled.
- Literal words in a command are shell-quoted. Paths in `build` lines are escaped (`$ `, `$:`, `$$`).
- A newline stays an error in a path, `description`, `depfile` and a pool name, and in a `-D` argument, because ninja
  cannot hold one there. A command word may hold newlines (a shell script). ninja's `cmd` is one line, so when any word
  of a step's command holds a newline, smith writes the whole command line to `.smith/command/<hash>`: the text it
  would have put in `cmd`, with the stamp's `&& touch` included and single quotes keeping the newlines, and the
  hash is FNV-1a 64 of that text as 16 hex digits. The file ends with a newline, is written through a temporary file
  and a rename only when its bytes differ, and has no `$` doubled, because `sh` reads it and ninja does not. The edge
  becomes `cmd = sh .smith/command/<hash>`. The name changes with the text, so ninja's command comparison reruns the
  step when the script changes. A command with no newline is written as before. Old command files stay (like old
  stamps); `rm -rf .smith` removes them.
- `smith_write` runs when `build.c` runs, not as a step: it writes the file through a temporary file and a rename, only
  when the bytes differ, so a step that lists the file as an input reruns only when the text changed. No step makes the
  file, so `-t clean` leaves it. A path must be relative, with no `..` component and nothing under `.smith/`.
- A step declared again is accepted when every field is equal (command, the three input lists, outputs, depfile,
  description, pool, restat, always) and `*step` becomes the earlier copy. A step that makes an earlier step's output
  with any field different is an error that names the output and the first field that differs. A step without outputs
  is compared by its stamp, which hashes the command and the input lists. The same holds for a task declared twice
  under one name and an alias declared twice (its targets must be equal).
- A task or alias name is a path: `A-Za-z0-9_./-`, not starting with `/` or `-`, not ending with `/`, with no empty,
  `.` or `..` component. The task's output `.smith/task/<name>` then has folders, and is never created. Pool names keep
  `A-Za-z0-9_.-`.
- `smith_option` and `smith_option_value` called again with a declared name and the same kind return what the first
  call returned (the fallback of the first call included), and the description may then be `nullptr`. The other kind is
  an error. A `smith_defer` callback reads the options this way.
- `smith_capture_or` starts the program with its stderr on `/dev/null` and returns the fallback when it cannot start,
  exits non-zero or is killed. Output over 1 MiB is still an error.
- `deps = gcc` is emitted whenever `.depfile` is set, so the executor folds the `.d` file into `.smith/.smith_deps` and deletes it.
- `.always` adds an input on one shared phony target that has no inputs (`build smith_always: phony`).
- A step with no outputs gets a stamp output, `.smith/stamp/<hash>`, and `&& touch <stamp>` ends its command. The
  stamp is touched only when the command succeeds, so the step reruns only when its inputs change (case `nofileout`).
  The hash is FNV-1a 64 of the command and every input, so two steps with one command and different inputs get two
  stamps.
- A task runs every time it is asked for, like make's `.PHONY`. It is an edge whose output is
  `.smith/task/<name>`, which is never created, plus `build <name>: phony .smith/task/<name>`. A task cannot
  have outputs, `.restat` or `.always`; setting one is an error.
- `smith_default` is required. Without it ninja would build every target, tasks such as `clean` included, so a
  `build.c` without a default is an error. An empty default, such as a glob that matched nothing, builds nothing: smith
  writes `build smith_nothing: phony` and `default smith_nothing`.
- A `-D` value lasts for one `smith build` call, as a make variable on the command line does. The build program gets it
  as an argument and writes the manifest for it (case `options`). A `-D` that `build.c` never declared is an error that
  lists the declared options.
- There is no automatic `cleandead`. make never deletes leftover outputs, so smith does not either.
  `smith build -t cleandead` is there when wanted (case `noleftover`).
- `smith_defer` records each call (needs copied, `more` kept) in a table of 256. After `build()` returns, the program
  walks the table in order from 0 up to its current count, which grows when a `more` defers again. An entry whose
  needs all exist (`stat`) has its `more` called once; an entry with a missing need is pending. Nothing builds while
  the program runs, so one pass is enough: an entry that `more` adds is reached later in the same pass. Callbacks run
  from this loop, never from inside each other.
- When the table is not empty, the manifest gains a regeneration rule and edge. Their inputs are every entry's needs,
  pending or not, so a need that changes during the build regenerates the manifest:
  ```
  rule smith_regenerate
    command = .smith/program --regenerate .smith/build.ninja <the -D arguments of this call>
    description = smith: regenerate .smith/build.ninja
    generator = 1
  build .smith/build.ninja: smith_regenerate <every need> | .smith/program
  ```
  `generator = 1` keeps the manifest through `-t clean` and keeps a changed command from forcing a regeneration.
- The program's arguments are `[--regenerate] <manifest> [-D...]`; the driver passes no `--regenerate`, the executor does.
  Without it the manifest is written only when its bytes changed. With it the manifest is always written, even with the
  same bytes, so its mtime moves past the needs and the executor reads it again once and settles (it decides regeneration by
  mtime only).
- `smith_default` may be missing only while some entry is pending, because the pending `more` will set it. Then the
  manifest has `build smith_nothing: phony` and `default smith_nothing`. With nothing pending, a missing default is
  still the error.
- Measured with ninja 1.13.2, and the executor does the same. A fresh tree: the run before the executor writes stage 1 (the steps that make the needs, plus the
  regeneration edge); the executor builds the needs, regenerates, reloads, then builds what was asked, so a stage 2 target
  can be named on the command line. Needs present but stale: `more` runs with the old contents, the executor rebuilds the
  needs and regenerates before it builds any target, so stale steps never run. No-op: the needs are older than the
  manifest and the executor prints `no work to do`.
- `smith_lines` reads the file into the arena on every `smith build` (limit 16 MiB). A file a step makes is read inside
  `more`; read in `build()` it is the error `cannot read <path>; if a step makes it, read it inside smith_defer`.

## The executor

`smith build` runs the manifest itself: no ninja is installed or run. The code is `runtime/executor.c` and the five files
it includes (`executor_manifest.c`, `executor_state.c`, `executor_plan.c`, `executor_run.c`, `executor_tools.c`), which
`smith.c` includes after `lib/io.c`, so `cc -std=gnu23 smith.c -o smith` still builds all of smith. It uses
`<smith/io.h>` (so it is macOS only until io has another backend): the manifest and the graph live in `io.arena`, the
files are read and written through `io.files`, and one kqueue loop waits for every job's process and pipe.

### Who runs the graph

smith parses the `.smith/build.ninja` that build.c's program wrote; the program does not run the graph.
- **Reuse.** The manifest is already the contract between the program and the runner: the program stays a small
  generator that exits, the runner is one binary for every project, and `SMITH_NINJA` can still run the same file. The
  bootstrap (compiling `build.c` into `.smith/program`) is a manifest too, so one runner does both; a program that ran
  its own graph could not compile itself.
- **Simplicity.** The program runs `build()` on every `smith build` (globs, options and captures are read fresh), so
  it cannot also be a long-lived scheduler: when a `smith_defer` need changes mid-build, the build has to run the program
  again and read its new manifest. That is one loop with a file between the two; with the graph in the program, it would
  be `build()` run twice in one process, whose arena and tables are file-level statics.
- **Speed.** Reading the manifest costs 4 ms for game's 886 KB (1,378 edges); the no-op build is dominated by running
  `build()` (about 100 ms in game), which a program-side executor would not avoid. The executor is compiled with smith, at
  the flags smith is built with, and not at `-O0 -g` as every project's build program is.

### What is kept between builds

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

### Keep writing `build.ninja`

Yes. It is the executor's input, so it is not a fallback format that can be dropped; what stays optional is ninja. Keep
`SMITH_NINJA=<ninja>` for one release cycle: it costs 20 lines in `smith.c`, and it lets anyone check the executor
against ninja on the same file (`tests/build.sh` and the generated-graph test do). Remove it, and the "ninja language"
claims in `docs/build-c.md`, when the manifest's last producer other than smith is gone.

### Parallelism, and a job that is not a process

`-j` is ninja's default, the cores plus 2 (3 for two cores, 2 for one), and at most 128. Each running job has a slot
with a `SmithJob`, a `SmithJobKind` (`start`, `poll`, `stop`: three functions) and 256 MiB of address space for its
output, reset when it ends. The loop is: start ready jobs while there are slots and the pools allow, wait on the io
loop (50 ms at most, so a signal is seen), poll every running job, finish those that ended. A job that runs in this
process later (a compile with clang inside smith, #1) is another `SmithJobKind`: `start` queues it on a thread or a
worker, `poll` looks at its done flag (a user event on the kqueue wakes the loop), `stop` cancels it; the planner,
the pools, the progress line and the log do not change. Threads against worker processes for #1 is not decided here.

### What ninja does, and what smith does with it

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
| `-d explain` | kept: says on stderr why each edge is dirty |
| `-l`, `-w`, other `-d` modes | read and ignored |
| `-t graph`, `browse`, `query`, `inputs`, `rules`, `recompact`, `restat`, `msvc`, `multi-inputs`, `-f`, `-C` | dropped: no project here uses them (`-f` and `-C` were already refused) |
| `include`, `subninja`, `rspfile`, `deps = msvc`, `dyndep`, `$in_newline`, one rule's variable naming another's | dropped: smith writes none of them. `SMITH_NINJA` runs a manifest that has them |
| `NINJA_STATUS`, colour | dropped |
| output capture limit | new: 256 MiB per running job, at most 128 jobs |
| a console job's stdin | changed for the better: ninja's console jobs keep it too, and so does smith's (checkers' `smith build run` reads it) |

`smith --smith-exec <manifest> [flags and targets]` runs any manifest the executor reads, in the current folder, with no
`build.c`; the tests use it.

### Measured

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

### How it was checked

- `tests/build.sh` has 89 checks of the executor (a failed job, `-k`, depfiles, restat, pools, parallel output, Ctrl-C,
  the tools, malformed manifests, the logs and the upgrade from ninja); `tests/io.c` has the process capture.
- `python3 tests/ninja_compare.py <smith>` compares the executor with ninja on random manifests (plain, restat, depfile
  and multi-output steps, implicit and order-only inputs, aliases, defaults), each edited at random between builds (a
  source changed or only touched, an output deleted or touched, a header a depfile named, a depfile's list, a command, a
  single target asked for) and built with both: the commands run and the files made must be the same in every build. It
  needs ninja, so it is run by hand when the executor changes, not by `smith build test`.

## Two layers

build.c has two layers, and a project may use either or both.

- **The raw layer** (`smith_step`, `smith_task`) is the make replacement: you own every command, and smith knows
  nothing about what a step does. It cannot tell a compile from an `rm`.
- **The high level** (`smith_object`, `smith_program`, `smith_compile_command`, `smith_compile_commands`) is for letting smith know what a
  compile and a link are. Each call emits ordinary steps, the same as `smith_step`, and its comment in the header states
  the exact command. `smith build -v` shows it. Nothing is hidden; the gain is that smith can write a correct
  `compile_commands.json`, which clang's `-MJ` cannot (it records clang's internal command, `-cc1`, which clangd
  rejects), and which has an entry for build.c itself so an editor finds `<smith/build.h>`.

`compile_commands.json` rules:
- One entry per `smith_object`, in call order: `directory` is the project root, `file` the source, `output` the object,
  and `arguments` the exact argv of the step, `-MMD -MF` included (clangd tolerates them, and the file tells the truth).
  An object declared again with the same output has one entry.
- Then one entry per `smith_compile_command(b, compiler, source, flags)`, in call order: `file` the source, no `output`,
  and `arguments` the compiler, the flags, `-c` and the source. It is for files the build never compiles on their own,
  such as sources reached only through a unity build, so the editor still knows their flags. A source that already has
  a `smith_object` entry or an earlier `smith_compile_command` is skipped (the first entry for a file wins), and
  calling it after `smith_compile_commands` is an error.
- Then one entry for build.c: `file` "build.c" and `arguments` `smith -std=gnu23 -g -O0 -Wall -Wextra -I .smith/include
  -c build.c`, the flags `smith build` compiles build.c with (a test checks they match smith.c's bootstrap).
- The JSON is escaped (`"`, `\`, control characters as `\u00XX`), indented two spaces, ends with a newline, and is
  written only when its bytes change, through `smith_write`'s rules. A `.run` step does not run build(), so it writes nothing there.
- Calling `smith_compile_commands` twice is an error, and so is a path under `.smith/`. Call it last.
- A missing required field is an error naming it; so is an object in `.objects` that was never passed to `smith_object`.
  A program's step lists the objects' outputs as inputs and the libraries that are files (a path with `/`, or ending in
  `.a`, `.dylib` or `.so`, and not starting with `-`) as implicit inputs; `-framework X` is not a file.

## Packages: `smith_package`

A package is an archive named in build.c by a URL and the SHA-256 of its bytes, as in Zig. There is no registry, no
version solving and no lock file: the hash is the lock, so the same hash always gives the same bytes.

```c
const char *cmake = smith_package(b, &(SmithPackage){
    .url = "https://github.com/Kitware/CMake/releases/download/v4.1.2/cmake-4.1.2-macos-universal.tar.gz",
    .hash = "sha256:...", // `smith fetch <url>` prints this line
});
// cmake is "<cache>/smith/packages/<hex>"; bin/cmake is under it
```

- **Cache:** `$XDG_CACHE_HOME/smith/packages/<hex>/`, or `~/.cache/smith/packages/<hex>/`, keyed by the archive's hash.
  `smith_package` fetches, verifies and extracts on the first use and afterwards only looks the folder up, so a build
  works offline once it has fetched. It returns the absolute path, which is stable and can go in steps, `-I` flags and
  objects. An archive with one top-level folder is returned as the contents of that folder.
- **Safe to interrupt:** the archive goes to a temporary file, its SHA-256 is checked, it is extracted into a temporary
  folder, and only then is the folder renamed to its hash. A folder named by a hash is therefore always complete; a
  killed fetch leaves only `.tmp-*` names, which nothing reads.
- **Errors:** a wrong hash names the URL, the expected hash and the actual one. `-DOFFLINE` makes a package missing from
  the cache an error that names its URL, with no fetch. build.c runs on every `smith build`, so a package is fetched
  when build.c asks for it, before the executor runs.
- **`smith fetch <url>`** downloads the archive, prints `.hash = "sha256:...",` to paste into build.c, and leaves the
  package in the cache.
- **Download** is smith's own HTTPS client (`lib/http.c` over `io.net`, Network.framework with the system's trust;
  `file://` copies a local file, for tests). It follows redirects, https only (a redirect to another scheme is an
  error), streams the body to the temporary file a block at a time, fails a server that sends nothing for 60 s with
  `ETIMEDOUT`, and reports statuses, redirect loops, short bodies and TLS failures as messages that name the URL.
  smith.c and the build runtime include the client with the libraries it needs (`lib/arena.c`, `random.c`, `io.c`,
  `http.c`; the runtime gets them from `.smith/lib/` and `.smith/internal/http.c`, and `smith_library` does not offer
  http until a second project needs it), so `cc -std=gnu23 smith.c` is still the whole build; Network.framework is
  loaded with `dlopen` on the first download, so nothing else pays for it. SHA-256 is in smith, checked against the NIST
  test vectors in `tests/build.sh`. The network cases are tested in `tests/package.sh` (a real cache, a scripted
  network: redirects, a refused cross-scheme redirect, 404, a short body, a broken connection, a wrong hash, a TLS
  failure, leftovers of a killed fetch). macOS only for now.
- **Extraction** is smith's own code and runs no process: see [Archives](#archives) below.

## Archives

`smith_package` reads `.tar`, `.tar.gz` (or `.tgz`) and `.tar.xz`, told apart by their first bytes, never by the name.
These are the formats today's packages use: CMake's `.tar.gz`, Python's standalone build as `.tar.gz`, and LLVM's source
as `.tar.xz`. A zip is refused by name (`zip archives are not supported`), as are bzip2 and zstd; nothing needs them.

- **The code is smith's own**, not a vendored library: `runtime/gzip.c` (gzip, DEFLATE, CRC-32; about 480 lines),
  `runtime/xz.c` (the xz container, LZMA2, CRC-64 and SHA-256 checks; about 670) and `runtime/untar.c` (tar and the safe
  extraction; about 720). They are written in the rules of the rest of smith: no malloc (the LZMA2 dictionary is one
  anonymous `mmap` of the size the block asks for, at most 1 GiB, and the rest is static), every loop bounded by an input
  size that was checked, errors as values. Reasons: the formats are small and fully specified; the smaller vendored
  decoders (a 0BSD xz decoder such as xz-embedded, roughly 2,000 lines in its own style, or a public-domain inflate such
  as puff, roughly 800 lines that decode a bit at a time and run several times slower) would still need adapting to the
  rules above and to a streaming sink, and a vendored tree is a second style, license and update to keep; and every
  field the formats let a reader verify is verified, which a general-purpose library does not promise. The cost is the
  code itself, which the tests below hold to account.
- **What is verified:** gzip's header CRC, CRC-32 and size of every member (members may follow one another, and zero
  padding may follow the last); xz's header, block header and index CRC-32s, the sizes a block header states, each
  block's check (none, CRC-32, CRC-64 or SHA-256), the index against the blocks and the footer against the header (several
  streams with the padding xz allows between them are read); tar's header checksums and its two closing zero blocks.
  An archive that is cut off anywhere, in a header, a name, a file, a block or a trailer, is an error that says so.
  Filters other than LZMA2 (BCJ, delta) and other checks are refused by name.
- **Where it writes:** every entry's name is made relative and must not be absolute or contain `..`; every folder on the
  way is opened with `O_NOFOLLOW` from a descriptor of the extraction folder, so a symlink made earlier in the same
  archive cannot send a later file elsewhere; a symlink's target must be relative, may have `..` only at its start and no
  more of them than its folder is deep, so it cannot leave the package, whatever other symlinks exist; when the package
  turns out to be one top-level folder, a symlink must also stay inside that folder. A hard link is made with `linkat`
  to an entry already extracted. A device, a FIFO, a sparse file or any other type is refused. Each refusal names the
  entry and the reason, and a package that fails is never put in the cache.
- **What is kept:** file modes (owner, group and other bits, with the process's umask, as `tar` does for an ordinary
  user; no setuid or sticky bit) and file modification times (pax times to the nanosecond). Folders are always made
  owner-readable, -writable and -searchable, so that a package can be removed even if the archive says otherwise, and
  folder times are not restored. Owners are not kept. Pax and GNU long names, the ustar prefix field and non-ASCII names
  work; the other pax records (extended attributes, ACLs) are skipped.
- **Speed**, the best of three runs on an Apple M-series Mac, extracting into a fresh folder, smith (built as smith
  builds itself, `-O1`) against `/usr/bin/tar -xf` (bsdtar with zlib and liblzma): LLVM 23.1.3's source (180,598 files,
  2.3 GB of tar in 96 xz blocks) 24.3 s against 27.5 s; CMake 4.4.4 (260 MB, `.tar.gz`) 2.2 s against 1.3 s; Python
  3.13's standalone build (67 MB, `.tar.gz`) 0.38 s against 0.24 s. Decoding alone, to a sink that counts, runs at
  about 240 MB/s for xz and 270 MB/s for gzip, single-threaded; bsdtar's gzip is about 1.4 times faster per core (zlib
  is hand-tuned) and its xz the same. Files take most of the time, so the difference shows only on gzip.
  The timings vary by a factor of two with what else the machine is doing.
- **Not done:** `._name` AppleDouble companions that macOS tar writes (they begin with the AppleDouble magic number) are
  dropped, as bsdtar turns them into attributes and smith keeps none. Extended attributes, ACLs and file flags are not
  kept. There is no limit on how much an archive may write, as with `tar`: the SHA-256 in build.c is what vouches for it.
- **Tests:** `tests/untar.sh`. Archives made with the system tar, gzip and xz in every format, check and block layout
  above, each extracted and compared with its source (every path, byte, mode and link); hostile archives made with
  `tar -P -s` (traversal, absolute names, escaping and absolute symlinks, a file written through a symlink, a FIFO and
  a device) and damaged ones (truncated, wrong gzip CRC-32 and size, a changed byte under each xz check, damaged xz
  headers and index, a damaged tar header), each refused with its reason and leaving the cache empty;
  `tests/untar.c` under AddressSanitizer and UBSan, with known answers (including fixed-Huffman gzip), every shorter
  prefix of an archive refused, and thousands of randomly damaged archives that must not crash, hang, write outside
  the folder or leave a symlink that points out (for a plain tar the damage rewrites names, targets and types and then
  fixes the checksum, so the name checks are what stop it); and the same extractions with `tar`, `gzip` and `xz`
  forbidden to run, through `sandbox-exec`.

## The caches and `smith cache`

smith keeps two caches under `$XDG_CACHE_HOME/smith/` (or `~/.cache/smith/`), each keyed by hash, so anything removed
from either is simply fetched or built again:

- `packages/<hex>/`: what `smith_package` extracted (above).
- `modules/`: clang's module cache for `smith_modules` (below). It is one cache for every project and worktree, not one
  per project, so Apple's SDK and framework modules are built once per machine and a new clone or worktree starts warm.
  clang keys a module file by the module, its path and the hash of the compile's flags, so two worktrees whose headers
  differ (different paths) never share or overwrite one another's module files, and a sanitized compile never reuses a
  plain one's. A module file is checked against its headers when it is used and rebuilt if one changed, so a warm cache
  can only be stale in a way clang detects.

**One rule for both: an entry unused for 31 days is removed, checked at most once a week.** For modules smith states it
in every compile, as `-fmodules-prune-interval=604800 -fmodules-prune-after=2678400`, and clang applies it (named
constants in `runtime/package.c`). For packages smith applies it:

- Each `smith_package` (and `smith fetch`) touches `<hex>.used` beside the package folder: that is its last use.
- The weekly check is due when `packages/prune.timestamp` is missing or older than a week; when it is not due, the cost
  is one `stat`. When due, the build touches the timestamp first (so a build starting meanwhile skips the check), then
  removes every package whose `<hex>.used` is older than 31 days. A package with no stamp (cached before pruning
  existed) is stamped, not removed. A `.tmp-*` name (a fetch in progress) is removed only when it is itself older than
  31 days. A stale stamp with no package is removed.
- **Safe beside a build that uses a package:** a build touches the stamp before it looks for the folder, and the prune
  renames a stale package out of the way (`.tmp-prune-*`) and reads the stamp again before it deletes anything. If the
  build's touch is there, the package is renamed back and kept; if the rename came first, the build finds no folder and
  fetches it again. So a build always gets a complete folder, and a package used in the last 31 days is never removed.
- Tests fake the clock through file times (`touch -t`) in `tests/cache.sh`, with caches of their own in a temporary
  `XDG_CACHE_HOME`.

`smith cache` prints each cache's path, number of entries and size on disk; `smith cache -v` also lists each entry and
its last use (a package's stamp; a module folder's newest file). `smith cache clean` removes both caches whole, and
`smith cache clean modules` or `smith cache clean packages` one of them. Like `smith build`, `smith new` and `smith
fetch`, `cache` is recognised only as the exact first argument.

**A build holds the caches.** `smith build` takes a shared `flock` on `<cache>/smith/lock` and keeps the descriptor open
in smith, which every job inherits, so the lock lasts as long as the build's processes (the system
drops it if smith is killed). `smith cache clean` takes the lock exclusively without waiting, and refuses with exit
status 2 while any build, in any project or worktree, holds it. That costs one `open` and one `flock` per build.
Showing the caches never takes the lock, and pruning does not need it: it is safe as above.

## Clang modules: `smith_modules`

Clang modules parse a group of headers once, cache the result, and reuse it wherever the headers are included again. The
source stays plain C: every `#include` of a header in a module becomes an import. Apple's SDK ships module maps for libc
and every framework, so `#include <stdio.h>` and `#import <AppKit/AppKit.h>` already have modules; `smith_modules` adds
the project's own headers and smith's.

```c
void build(SmithBuild *b) {
    smith_modules(b, &(SmithModules){.folders = (const char *[]){"src", "tests", nullptr}});
    // ... every smith_object after this one is compiled with modules
}
```

- **Off until build.c says so.** smith never changes a default. `smith new`'s template turns it on (`smith_modules(b,
  &(SmithModules){})`, with no folders, which still covers system headers and smith's); `tests/new.sh` checks that.
- **Which compiles.** Every `smith_object` after the call whose source ends in `.c` or `.m`: the flags are added before
  the object's `.flags`, so a later `-fno-modules` wins. `.metal` and C++ sources never get them. Calling it after a
  `smith_object`, or twice, is an error.
- **`.folders`** lists header folders below the project (not `.`). Every `.h` under a folder, subfolders included, is a
  module of its own, named from its path (`src/base/math.h` is `src_base_math`). The map for a folder is
  `.smith/modules/<folder with / as _>.modulemap`. Folders may not overlap. smith's own headers
  (`.smith/include/smith/*.h`) always get a map, `.smith/modules/smith.modulemap`, as modules `smith_arena`,
  `smith_io`, and so on.
- **One module per header, not one per folder.** clang's `umbrella` directory makes the whole folder a single module,
  and the `-MD` depfile of every source then lists every header of that folder, so editing one header rebuilds every
  source that includes any header there. With one module per header (build.c's `smith_glob` finds them, so nobody lists
  them) the depfile lists exactly the headers a source imports, directly and through other headers, and a rebuild is
  what the build already does without modules. Each module is `module <name> { header "<absolute path>" export * }`; `export
  *` makes a header's own includes visible to its users, as textual inclusion did.
- **The flags:** `-fmodules -fmodules-cache-path=<cache>/smith/modules -fmodules-prune-interval=604800
  -fmodules-prune-after=2678400` and one `-fmodule-map-file=<map>` per map. `<cache>` is `$XDG_CACHE_HOME`, or
  `~/.cache`, as for packages. The cache is clang's own (implicit modules), so clang does the locking when many compiles
  fill it at once. A sanitized compile and a plain one hash differently, so each has its own folder inside the cache.
  See "The caches" below for why it is shared and for the pruning flags.
- **Rebuilds.** The depfile lists the module maps and the headers imported, so editing a header rebuilds the sources that
  import it; editing or adding a header in a folder rewrites its map, which rebuilds the sources that load it. A flag
  that changes a module's hash (a `-D`, the standard, a sanitizer) changes the command, so the objects rebuild and
  clang builds the module again under the new hash.
- **compile_commands.json keeps the module flags**, so the editor sees what the build does. Use `smith lsp` (clangd) with no
  `--query-driver`: with it, clangd cannot build the SDK's modules (not even `#include <stdint.h>`: the SDK's map
  requires a feature that the extracted `-isystem` paths clear). Measured on game's `src/platform/macos.m` with its real
  module flags: clangd without `--query-driver` reports 0 errors, with it 1 (`module
  '_c_standard_library_obsolete' requires feature 'found_incompatible_headers__check_search_paths'`).
- **Isolation, and `.textual`.** A header is compiled alone when it is first imported, so one that works only after
  another include, or after a `#define` before it, fails there:

  ```
  While building module 'inc_needs_stddef' imported from bad.c:2:
  inc/needs_stddef.h:2:1: error: unknown type name 'size_t'
  ```

  The fix is for the header to include what it uses. A header that stays textual (it is included several times with
  different macros, or is not valid alone) is listed in `.textual`, spelled as `smith_glob` prints it
  (`"src/legacy.h"`): it is then not a module and every `#include` of it is textual, as before. A name that no folder
  holds is an error.
- **Measured** on a busy machine: Objective-C that parses AppKit, Metal and
  AVFoundation (game's `src/platform/macos.m`) compiles in 0.19 s instead of 0.67 s with a warm cache; filling the cache
  the first time costs 2 to 4 s and about 70 MB per configuration. Unity builds with a few translation units gain little,
  because the parse is not their critical path.
- **Explicit modules** (smith builds each `.pcm` as a step and passes `-fmodule-file=`) are not implemented.
  They would make the module graph part of the build graph: no cache locking, exact rebuilds of the module files, a
  reproducible build, and a depfile that needs no help. They cost build.c a model of the module dependencies (which
  module imports which), which clang's scanner (`clang-scan-deps`) can supply.

## Tests: `smith_tests` and `<smith/test.h>`

Tests are written once and done well, in smith: a plain function per test, checks that print both values with their
types, workers that run the tests in parallel, and a runner that is the library's `main`. `smith_tests` (the call is in the API below,
with the exact commands) builds that runner from the project's test files and adds the task `smith build test`.

```c
// tests/test_random.c
#include <smith/test.h>

void test_addition(void) {                      // a test: found by name, no registration
    SMITH_CHECK(2 + 2, ==, 4);
}

void test_uniform_stays_in_range(SmithKey key) { // a property test: runs on 100 keys
    SmithUniformInt64 range = {.low = 0, .high = 6};
    int64_t x = smith_uniform_int64_sample(key, range);
    SMITH_CHECK(x, <=, range.high);
}
```

```c
// build.c
SmithTests tests = {
    .sources = smith_glob(b, smith_list(b, "tests/test_*.c", nullptr)),
    .objects = (const SmithObject *[]){&arena, &random, &code_under_test, nullptr},
    .flags = flags,                                  // for the test files and smith's test library
    .link_flags = link_flags,
};
smith_tests(b, &tests);                              // tests.step is the task "test"
```

- **Macros only for checks.** Only a macro can capture an expression's text, its file and its line, and evaluate an
  expression where the check can catch its end. A test is a plain function; there is no `TEST()` macro and no registration.
- **Found by name, in the files build.c names.** build.c says which files are tests and how they compile and link,
  because only build.c knows that. A step scans them for lines that are exactly `void test_<name>(void) {` or
  `void test_<name>(SmithKey key) {` (`<name>` in `[a-z0-9_]`, the parameter's name in `[a-z_]`) and writes
  `tests_list.h` (prototypes) and `tests_list.c` (the table) beside the runner. Each test file compiles with
  `-include <tests_list.h> -Werror=missing-prototypes`, so a `test_` function the scan missed because it is formatted
  another way (`void test_x (void) {`) is a compile error. A `static` helper is not a test. A name defined twice is an
  error that names both files; a file with no tests is fine.
- **`SMITH_CHECK(condition)`**, **`SMITH_CHECK(a, op, b)`**, **`SMITH_CHECK_NEAR(a, b, tolerance)`** and
  **`SMITH_CHECK_ABORTS(expression)`** are the checks. A comparison evaluates each argument once and converts `b` to
  `a`'s type as an assignment would, so `-Wconversion` flags a lossy comparison (`SMITH_CHECK(int_value, ==, 3.5)` is a
  compile error) and both values print in `a`'s type. `a`'s type is its type as a value: an array becomes a pointer and
  `const` drops; every pointer compares, and prints, as an address. Printed types: `bool`, `char`, every standard
  signed and unsigned integer type, `float` and `double` (shortest round trip plus `%a`, so a difference in the last
  bit shows) and pointers. A failed check ends the test.
- **`SMITH_CHECK_SAME_BITS(a, b)`** passes when two floats or two doubles are bitwise identical: `-0.0` is not `0.0`,
  and a NaN is itself (two NaNs with different payloads differ). `b` converts to `a`'s type; any other type is a
  compile error. It is the check for determinism. A failure prints both values, shortest and in hex, and a NaN or an
  infinity also shows its raw bits (`nan [0x7ff8000000000001]`).
- **`SMITH_CHECK_ABORTS`** runs the expression in the worker's own process, with its output going to a temporary file.
  It checks that the program stops on purpose: the check passes when the expression raises `SIGABRT` or `SIGTRAP` (an
  `assert`, `abort()`, `__builtin_trap`, the arena's capacity stop), which the worker catches and jumps back from with
  `siglongjmp`, and its output holds no sanitizer report. It fails, with the expression's output shown, when it
  returns ("it returned"), exits ("it exited with status N"; the exit ends the worker), crashes with another signal
  such as `SIGSEGV` ("it crashed"; the worker ends), or is stopped by a sanitizer report ("AddressSanitizer",
  "UndefinedBehaviorSanitizer", "LeakSanitizer", "ThreadSanitizer" or "runtime error:" in its output: a bug, not the
  stop the test expected; the worker is retired). So a memory bug before the assertion cannot make the test pass. The
  output is discarded on a pass. A hang is the test's timeout, which ends the worker. With `--isolate` the expression
  runs in a forked child and the parent judges how it ended, as before the workers. In a worker the expression's
  effects (writes to variables) stay, as with any `longjmp`; a variable it changes that the test reads afterwards
  should be `volatile`.
- **`smith_test_note(format, ...)`** prints under a failure, whether a failed check, a crash or a timeout; each note
  replaces the last. **`smith_test_arena(capacity)`** is the test's memory, from a block the runner reserved once with
  `mmap`, given back before each test and each key.
- **The runner** forks `--jobs` workers (the cores by default), once each and when one is needed, and sends each the
  tests to run, one after another, in its own process. Before each test and each key of a property test the worker gives
  the arenas back, clears the note and sets `alarm(timeout)`; it clears the alarm when the test ends. A failed check
  unwinds with `siglongjmp` to the worker's loop and the result goes back through shared memory, so the worker lives
  on. A crash, a timeout, an exit, output past 16 MiB or a sanitizer's report (in the output of a failed or a passing
  test) ends the worker: the runner records the failure as it always has, forks a fresh worker and goes on, so a
  crash fails only its own test. A worker also ends after any failed test, because a failed check unwinds out of
  arbitrary code and can leave state half-done, and after any test that ran a `SMITH_CHECK_ABORTS`, passed or failed,
  because a death check stands for the program ending: a worker is reused only after a clean pass with no death
  check (the shrinking worker is reused across its failing attempts, so the runner reruns the final shrunk case once in a fresh worker: if that does not fail the same way, the failure is shown unshrunk with a line saying state leaked between attempts). What a test leaves in global variables, files
  and the process stays for the next test on that worker after such a pass, and which tests share a worker depends on timing unless `--jobs 1`; `--shuffle <seed>` runs the
  tests in a shuffled order to find such a dependence (it replays with `--jobs 1`), and `--isolate` keeps the old
  process per test (and per `SMITH_CHECK_ABORTS`), exactly. `--stats` prints how many forks the run made. Results are
  printed in table order, in either mode. Output is captured and
  shown only for a failing test (the first 64 KiB; a worker that writes 16 MiB in one test is killed). The options come from
  `-DTEST=<substring>` (a test named exactly that, else every test whose name contains it), `-DSEED=<n>` (each
  property test on that one seed), `-DSEEDS=<n>` (keys per property test, 100 by default), `-DJOBS=<n>` (default: the
  cores), `-DSHRINK=0|1`, `-DSHUFFLE=<seed>`, `-DISOLATE` and `-DFULL`; the binary takes `--test`, `--seed`, `--seeds`, `--jobs`, `--timeout <seconds>` (10, per
  test and per key), `--shrink 0|1`, `--shrink-seconds <seconds>` (10, the time one shrink may take), `--shuffle <seed>`, `--isolate`, `--stats`, `--full`, `--list` and `--verbose`. These are read in `build()` with `smith_option_value`,
  so a project that reads the same name gets the same value. The run ends with `N passed, M failed in T s` and the five
  slowest tests; `--verbose` lists every test.
- **Full size: `smith_test_full()`.** It is false by default and true when the run was given `-DFULL` (the runner's
  `--full`). A test with a heavy check picks its size from it, such as 200 meshes by default and 20,000 with `-DFULL`,
  and checks the same properties either way, so `smith build test` stays fast and `smith build test -DFULL` is the
  slow pass. It replaces a second task such as game's `test-slow`: that becomes `smith build test -DFULL`.
- **`.inputs`: files the tests read when they run**, such as compiled shaders, baked meshes and fixtures. They are the
  test task's inputs after the runner, so they are built first and a change to one reruns the tests. The tests read
  them by their path, relative to the project's root, where `smith build` runs them.
- **A property test's keys** are `smith_key_make(seed)`, with the seeds drawn from a fixed root, so a printed seed
  replays exactly.
- **A failure prints** the test, the seed of a property test, the check's file and line, the line as written, the
  values with their types, the note, the test's output, and the command that reruns it:

  ```
  FAIL uniform_stays_in_range with seed 6387777158891554393, shrunk in 41 runs
    tests/test_random.c:12
        SMITH_CHECK(x, <=, range.high);
      x          = 7   (long long)
      range.high = 6   (long long)
      note: <the note, if any>
      <the test's output, indented, if any>
      rerun: smith build test -DTEST=uniform_stays_in_range -DSEED=6387777158891554393
  ```

  The check's source line is read from the file when the failure is reported, and left out when the file cannot be read.
  A crash is `FAIL name: killed by SIGSEGV` with the captured stderr (a sanitizer's report), a timeout is
  `FAIL name: timed out after 10.0 s` (the time the test ran: a test that calls `alarm` itself replaces the runner's timer, and ends as a timeout after its own time). Color goes on only when stdout is a terminal and `NO_COLOR` is unset.
- **A failing property test is shrunk.** The runner reruns the failing key in a worker of its own that has `smith_random_hook`
  (in `<smith/random.h>`) set, which records each `(key, n)` the test asks `smith_key_bits` for, in first-use order, in
  shared memory (at most 65,536 words; past that it stops recording). Every distribution maps word 0 to its simplest
  value (0 or the bound nearest it for an integer, about the mean for a normal, false, the identity permutation). For
  each recorded word in order the runner tries 0; if the test then stops failing it binary-searches between 0 and the
  word for the smallest one that still fails. Each try is a run in that worker, which reads an override table from shared memory; a try that kills the worker costs a fork, and nothing else does.
  A try is kept only when the failure is the same: the same check (file and line), or the same signal or exit status;
  a failure that moves to another check is not kept. Passes repeat until none improves, within 1,000 runs or 10
  seconds, whichever comes first, and the same seed gives the same shrunk case (when the runs limit ends it; the
  seconds limit depends on the machine). The report is the shrunk failure, with its values, its note and its output,
  and `shrunk in k runs`; the rerun line names the original seed, and rerunning it shrinks to the same case again.
  `-DSHRINK=0` shows the unshrunk failure. Shrinking starts only when every other test has finished (a shrinkable
  failure and the results after it are held until then, and printed in order), so a result never depends on another
  test: nothing stops reading the other workers' pipes or judging their timeouts. A timeout is not shrunk (each try would take the whole timeout), and a failure
  that does not reproduce when its key is run alone (it depended on an earlier key, or on chance) is shown as it was.
- **`.objects` must include smith's arena and random.** The test library uses both, and they are compiled once, by the
  project, with its flags: `smith_tests` fails and says which `smith_library` to add when one is missing.
- The test files get `compile_commands.json` entries through `smith_object`, so clangd reads them with the project's
  flags (`smith lsp --check` on a test file reports no errors).

## The API

Version 1 is enough for checkers and learner. Version 1.5 (`smith_defer`, `smith_lines`) adds what game needs for its
generated mesh list. Everything else is later. The text below is [include/smith/build.h](../include/smith/build.h)
without `#pragma once`. Setting `.content_hash` is the error `smith: build.c: .content_hash is not implemented yet`.

```c
// smith/build.h: the API of a project's build.c, which `smith build` compiles and runs to write build.ninja.
// Design: docs/build-c.md. Lists end in nullptr and are never nullptr (an optional field left unset reads as empty).
#pragma once

typedef struct SmithBuild {
    // `uname -s` exactly: "Darwin", "Linux"; nothing renamed, so it matches what a Makefile sees
    const char *os;
    const char *arch; // `uname -m` exactly: "arm64", "x86_64", "aarch64"
    const char *root; // the project's absolute folder, where build.c is
    int cpus;         // the number of cores
} SmithBuild;

typedef struct SmithRun SmithRun;

typedef struct {
    // argv. Only the whole words $in and $out expand; every other $ is literal. A word may hold newlines
    const char *const *command;
    const char *const *inputs;            // $in; a change reruns the step
    const char *const *implicit_inputs;   // a change reruns the step; not on the command line
    const char *const *order_only_inputs; // built first; a change does not rerun the step
    const char *const *outputs;           // $out
    const char *depfile;                  // a file the command writes in -MMD form
    const char *stamp;         // the step's only output, which smith touches on success; .depfile may name it
    const char *description;   // printed instead of the command
    const char *pool;          // a name from smith_pool, or "console" for the terminal
    bool restat;               // an output the command left unchanged does not rerun what depends on it
    bool always;               // run on every build
    int (*run)(SmithRun *run); // a C function in build.c instead of .command; 0 succeeds
    const char *const *args;   // given to .run as run->args
    bool content_hash;         // later: rerun only when the inputs' bytes changed
} SmithStep;

void build(SmithBuild *b); // what the project writes

// one ninja edge. Afterwards every list and string in *step points at the arena copies; with no outputs, .outputs is
// the stamp. A step declared again with every field equal (command, the input lists, outputs, depfile, description,
// pool, restat, always) changes nothing and *step becomes the earlier copy; a step that makes an earlier step's output
// with any field different is an error that names the output and the first field that differs. A command word may
// hold newlines (a shell script): smith writes the whole command line to .smith/command/<hash of its text> and the
// edge runs it with sh, so editing the script reruns the step. A newline in a path, .description, .depfile or .pool is
// an error. With .stamp the step's output is that file, $out is it, and afterwards .outputs is {stamp}; .stamp with
// .outputs (other than the {stamp} a returned step holds) is an error, and so is .stamp on a task. Declared again,
// .stamp must be equal like every other field.
void smith_step(SmithBuild *b, SmithStep *step);
// `smith build <name>`: runs every time it is asked for, like a make .PHONY target
// afterwards .outputs is {name}, so other steps can depend on the task. A name holds A-Z a-z 0-9 _ . / -, may not
// start with '/' or '-', end with '/', or hold an empty, '.' or '..' component (check-alone/src/a.c is a name). A task
// declared again under one name with every field equal changes nothing; with any field different it is an error
void smith_task(SmithBuild *b, const char *name, SmithStep *step);
// a group of targets, named by the same rules as a task; declared again with equal targets it changes nothing, with
// different targets it is an error
void smith_alias(SmithBuild *b, const char *name, const char *const *targets);
void smith_default(SmithBuild *b, const char *const *targets);
void smith_pool(SmithBuild *b, const char *name, int depth);

// -Dname. An option can be read again: the same name and kind returns the same value and the description may then be
// nullptr, so a smith_defer callback can read what build() read. The other kind is an error
bool smith_option(SmithBuild *b, const char *name, const char *description);
// -Dname=value; fallback may be nullptr
const char *smith_option_value(SmithBuild *b, const char *name, const char *fallback, const char *description);
// an environment variable, or fallback (may be nullptr)
const char *smith_env(SmithBuild *b, const char *name, const char *fallback);
// the union of the patterns' matches, sorted by strcmp, each name once: make's $(sort $(wildcard a b c)).
// A `**` component matches zero or more folders, so src/**/*.c matches src/a.c and src/x/y/b.c (symbolic links are
// not followed, and more than 64 folders deep is an error). Every other component goes through fnmatch, the last one
// included, and a hidden entry matches only a component that starts with '.', as in the shell
const char *const *smith_glob(SmithBuild *b, const char *const *patterns);
// a command's stdout without its last newline; no shell. A program that cannot start, exits non-zero or is killed is an
// error
const char *smith_capture(SmithBuild *b, const char *const *argv);
// like smith_capture, but returns fallback (may be nullptr) when the program cannot start, exits non-zero or is
// killed; its stderr goes to /dev/null
const char *smith_capture_or(SmithBuild *b, const char *const *argv, const char *fallback);
// Writes a file when build.c runs, through a temporary file and a rename, and only when its bytes differ, so its mtime
// moves only when its content does. It creates missing parent folders. The path must be relative, with no '..'
// component and nothing under .smith/. For files whose content build.c computes; a step that reads one lists it as an
// input. `-t clean` leaves it, since no step makes it
void smith_write(SmithBuild *b, const char *path, const char *text);

// The path of a smith library's source, such as ".smith/lib/arena.c", which smith writes on every `smith build`
// together with the library's header in .smith/include/smith/. The project compiles the source itself, with its own
// flags (smith_object), and adds -I .smith/include to its compiles to find <smith/arena.h>; nothing is linked unless
// build.c asks. An unknown name is an error that lists the known names ("arena", "io", "random", "test", "text"; random and io use the arena,
// and io uses random too, so a project that compiles one compiles what it uses; smith_tests compiles "test").
const char *smith_library(SmithBuild *b, const char *name);

// A package is an archive named by a URL and the hash of its bytes, both written in build.c. There is no registry, no
// version solving and no lock file: the hash is the lock, and the same hash always gives the same bytes.
typedef struct {
    const char *url;  // https:// (file:// for tests)
    const char *hash; // "sha256:<64 hex digits>" of the archive's bytes; `smith fetch <url>` prints the line to paste
} SmithPackage;
// The absolute path of the package's extracted contents, in smith's cache: $XDG_CACHE_HOME/smith/packages/<hex>/, or
// ~/.cache/smith/packages/<hex>/ when that is unset, keyed by the archive's hash. Fetched, verified and extracted on
// the first use (smith's own HTTPS client; smith itself reads .tar, .tar.gz and .tar.xz);
// afterwards it is only looked up, so a build works offline once it has fetched. When the archive holds one top-level
// folder, the path is that folder's contents. The package appears in the cache only when it is complete: a fetch that
// is interrupted leaves nothing under the hash. A wrong hash is an error that names the URL and both hashes, and
// -DOFFLINE makes a package missing from the cache an error that names its URL instead of a fetch. The path is stable,
// so it can go in steps, -I flags and objects. macOS only for now.
const char *smith_package(SmithBuild *b, const SmithPackage *package);

// Helpers
[[gnu::format(printf, 2, 3)]] const char *smith_fmt(SmithBuild *b, const char *format, ...);
// joins lists; the last argument is nullptr
const char *const *smith_cat(SmithBuild *b, const char *const *first, ...);
// format applied to each item ("out/%s")
const char *const *smith_map(SmithBuild *b, const char *const *items, const char *format);
// items, or an empty list
const char *const *smith_when(SmithBuild *b, bool condition, const char *const *items);
// the items with the separator between them, in the arena; the empty list gives "". For text such as a unity file:
// smith_join(b, smith_map(b, files, "#include \"%s\"\n"), "")
const char *smith_join(SmithBuild *b, const char *const *items, const char *separator);
// the strings up to a final nullptr, as a list in the arena. Unlike a compound literal it never dangles, so it is safe
// to keep and to return from a function; smith_list(b, nullptr) is the empty list
const char *const *smith_list(SmithBuild *b, const char *first, ...);
// ("src/a.c", ".o") gives "src/a.o"
const char *smith_replace_extension(SmithBuild *b, const char *path, const char *extension);

// Steps that exist only once a generated file is read.
// `needs` are paths: outputs of steps already declared, or source files. Once they all exist, `more` is called, and it
// may call any function here, smith_defer included. The program calls `more` itself while it runs, for each call whose
// needs exist, in order (limit 256 calls). A call whose needs do not exist yet is pending: the manifest gets a
// regeneration edge on every call's needs, so the executor builds the needs, runs the program again with them in place,
// and reloads. A pending `more` may be the one that calls smith_default.
void smith_defer(SmithBuild *b, const char *const *needs, void (*more)(SmithBuild *b));
// One entry per line of the file, without the newline; a final newline adds no entry and an empty line is "". The file
// is read on every `smith build`. A file that a step makes is read inside smith_defer's `more`, never in build(). A
// missing file or one over 16 MiB is an error.
const char *const *smith_lines(SmithBuild *b, const char *path);

// Steps written as C functions. A step or task with .run (and no .command) calls it when the executor runs the step.
// The step's command carries everything the function gets: `.smith/program --run <function> --input <path>... --output
// <path>... --arg <word>...`, where <function> is the function's address minus the address of a fixed function in the
// program, in hex (the program is each .run edge's implicit input, so the offset is valid for the run that uses it).
// With --run the program does not call build() or any smith_defer callback: it makes a SmithBuild (os, arch, root,
// cpus, a fresh arena) and a SmithRun from those words, calls the function and exits with its result. So a function
// sees no state build() set, no -D option and no earlier capture: its data is its step's .inputs, .outputs and
// .args, and nothing else. Pass an option's value in .args. The offset is checked to land in this executable.
// The return value is the exit status. Like a command, the function writes the outputs it declares; one with none gets
// smith's stamp. run->b works with every helper that needs no build() state (smith_fmt, smith_cat, smith_join,
// smith_list, smith_lines, smith_glob, smith_capture, smith_write). Editing build.c reruns every .run step, since the
// function's code is part of the program. .content_hash is not implemented yet and exits 2.
struct SmithRun {
    SmithBuild *b;
    const char *const *inputs;  // the step's .inputs
    const char *const *outputs; // its outputs, the stamp included; empty for a task
    const char *const *args;    // its .args
};
// Runs a program, no shell, and returns its stdout split into lines as smith_lines does, with its stderr passed
// through. A program that cannot start, exits non-zero or is killed prints `smith: <argv[0]> failed (status N)` and
// gives nullptr. More than 1 MiB of stdout is an error.
const char *const *smith_run_lines(SmithRun *run, const char *const *argv);
// Like smith_run_lines, but the exit status is the answer: the program's exit status, 128 plus the signal that killed
// it, or 127 when it cannot start. *lines (may be nullptr) receives its stdout lines, or nullptr when it did not start.
// It prints no message.
int smith_run_status(SmithRun *run, const char *const *argv, const char *const **lines);
// Like smith_run_status, but the program's stderr goes into the same pipe as its stdout, so *lines holds both in the
// order the program wrote them, as 2>&1 does. For comparing a program's whole output with an expected text. The 1 MiB
// limit covers both streams together.
int smith_run_combined(SmithRun *run, const char *const *argv, const char *const **lines);
// Records that this run read path, so a change to it reruns the step: the program writes the step's .depfile from
// these paths when the function returns. A .run step that records a path must have a .depfile.
void smith_run_depend(SmithRun *run, const char *path);
// Runs a program with no shell and with its stdout and stderr going straight to the terminal (inherited, not
// captured). `environment` (NAME=value entries, may be nullptr) is added to the current environment, and an entry
// replaces a variable of the same name. Returns the exit status, 128 plus the signal that killed it, or 127 when it
// cannot start. stdout and stderr are flushed first, so the function's own printing comes before the program's. The
// same flush happens before smith_run_lines and smith_run_status spawn.
int smith_run_program(SmithRun *run, const char *const *argv, const char *const *environment);

// High level: compiles and links that smith knows are compiles and links, so it can write compile_commands.json.
// Each emits ordinary steps, the same as calling smith_step yourself; the command is written out in its comment.

// One source compiled to one object:
//   <compiler> <flags...> -MMD -MF <output with .d for its extension> -c <source> -o <output>
// with that .d file as the step's depfile.
typedef struct {
    const char *compiler;               // the program, such as "smith" or "smith++"; required
    const char *source;                 // required
    const char *output;                 // the object, such as "build/main.o"; required
    const char *const *flags;           // exactly these, in this order
    const char *const *implicit_inputs; // files the compile reads that are not in its depfile on a clean build, such as
                                        // a generated #include or #embed file
    SmithStep step;                     // set by smith_object: the step it emitted
} SmithObject;

// Objects linked into a program:
//   <linker> <flags...> <each object's output...> <libraries...> -o <output>
typedef struct {
    const char *linker;                // the program, such as "smith" or "smith++"; required
    const char *output;                // the program file; required
    const char *const *flags;          // before the objects, such as -mmacosx-version-min=26.0
    const SmithObject *const *objects; // the objects to link, each already passed to smith_object
    const char *const *libraries;      // after the objects: -framework Metal, -lm, a .a path
    SmithStep step;                    // set by smith_program: the step it emitted
} SmithProgram;

void smith_object(SmithBuild *b, SmithObject *object);
void smith_program(SmithBuild *b, SmithProgram *program);
// Records a compile_commands.json entry for source with no step: {directory: the root, file: source, arguments:
// [compiler, flags..., "-c", source]} and no "output". It is for files the build never compiles on their own, such as
// sources reached only through a unity build, so the editor still knows their flags. compiler and source are required.
// Entries follow the smith_object entries, in call order, before build.c's. A source that already has a smith_object
// entry or an earlier smith_compile_command is skipped: the first entry for a file wins. Call it before
// smith_compile_commands, which writes the file; after it, it is an error.
void smith_compile_command(SmithBuild *b, const char *compiler, const char *source, const char *const *flags);
// Clang modules, off until build.c calls this, and then for every C and Objective-C source passed to smith_object
// afterwards (never .metal, never C++; call it before the first smith_object, or it is an error). Each header under
// the folders becomes its own module, so a source depends on exactly the headers it includes; smith's own headers
// (.smith/include/smith/) are modules too. smith writes the module maps to .smith/modules/ and clang keeps its cache in
// $XDG_CACHE_HOME/smith/modules (or ~/.cache/smith/modules), shared by every project and worktree and keyed by the
// compile's flags and the header's path, so the SDK's modules are built once per machine; `smith cache` shows it and
// `smith cache clean` empties it. Every covered compile gets `-fmodules -fmodules-cache-path=<that folder>`,
// `-fmodules-prune-interval=604800 -fmodules-prune-after=2678400` (a module unused for 31 days is removed, checked at
// most once a week) and one
// `-fmodule-map-file=` per map, before the .flags; the -MD depfile then lists the maps and the headers imported, so
// editing a header rebuilds exactly the sources that import it. compile_commands.json keeps these flags, so the
// editor sees exactly what the build does (`smith lsp`, without --query-driver, builds the modules).
// A header that cannot be compiled alone, or that clang must include more than once with different macros, is listed
// in .textual (paths as smith_glob prints them, such as "src/legacy.h"): it stays a plain #include everywhere.
typedef struct {
    const char *const *folders; // header folders below the project, such as "src"; a folder covers its subfolders
    const char *const *textual; // headers that stay textual
} SmithModules;
void smith_modules(SmithBuild *b, const SmithModules *modules);
// Tests: smith's test library (<smith/test.h>, docs/build-c.md) built into a runner with the project's test files.
typedef struct {
    const char *compiler;       // compiles and links the tests, by default "smith"
    const char *const *sources; // the test files; every void test_<name>(void) and test_<name>(SmithKey key) in them
    const SmithObject *const *objects; // the code under test, linked in; it must include smith's arena and random
    const char *const *flags;          // for the test files and smith's test library
    const char *const *link_flags;
    const char *const *inputs; // files the tests read when they run, such as compiled shaders; built first
    const char *output; // the runner, by default build/tests
    const char *name;   // the task, by default "test"
    SmithStep step;     // set by smith_tests: the task
} SmithTests;

// Emits ordinary steps, the same as calling smith_step yourself; the commands are written out here.
//   find the tests (a .run step, restat): reads .sources and writes <folder of .output>/tests_list.h, the prototypes
//     of the tests, and tests_list.c, the table the runner reads. A test is a line that is exactly
//     `void test_<name>(void) {` or `void test_<name>(SmithKey <key>) {` (<name> in [a-z0-9_], <key> in [a-z_]), the
//     pattern ^void (test_[a-z0-9_]+)\((void|SmithKey [a-z_]+)\) \{$. A name defined twice is an error that names
//     both files.
//   each test file, through smith_object:
//     <compiler> <flags...> -include <tests_list.h> -Werror=missing-prototypes -MMD -MF <d> -c <file> -o <object>
//     so a test_ function the scan missed, because it is formatted another way, is a compile error.
//   tests_list.c and smith_library(b, "test"), compiled with <compiler> <flags...>.
//   the runner: <compiler> <link_flags...> <test objects, tests_list.o, the test library, .objects...> -o <output>
//     The objects are in <output>_objects/. .objects must include smith's arena and random, which are not compiled a
//     second time; the call fails and says which smith_library to add when one is missing.
//   the task <name> (a .run task, on the console): runs <output> with the options -DTEST (--test), -DSEED (--seed),
//     -DSEEDS (--seeds), -DJOBS (--jobs), -DSHRINK (--shrink) and the boolean -DFULL (--full), each read with
//     smith_option_value or smith_option, so a project that reads the same name gets the same value. .inputs are
//     the task's inputs after the runner, so they are built before the tests run and a change to one reruns them.
void smith_tests(SmithBuild *b, SmithTests *tests);
// Writes compile_commands.json at path when build.c runs, only when its bytes change, from every smith_object so far,
// then every smith_compile_command, plus one entry for build.c itself with the command smith compiles it with. Call it
// last.
void smith_compile_commands(SmithBuild *b, const char *path);
```

Rules:
- A list ends in `nullptr`, and a list is never itself `nullptr`. An empty list is `(const char *[]){nullptr}`;
  `smith_glob` and `smith_when` return one when there is nothing. A loop starts from one:
  `objects = smith_cat(b, objects, cc.outputs, nullptr)`. The `nullptr` that ends `smith_cat`'s arguments is the only
  one.
- A compound literal lives only until the end of its block. Every smith call copies what it is given, so a literal
  passed straight to a call is safe, and what a call returns lives until the build ends. A literal returned from a
  function, or stored in a struct that outlives its block, dangles: the game port's raw translation crashed on one, and
  ASan did not catch it.
- `smith_step` and `smith_task` take the step by pointer and update it in place. They copy every list and string into
  the arena, so a block-scope compound literal is safe, and afterwards the step's lists point at the copies:
  `.inputs = compile.outputs` is valid after the block ends. Declare the step first, then pass its address:
  clang-format indents a call that holds the whole `(SmithStep){...}` very deeply, and this form formats cleanly.
- A step with no outputs gets a stamp list in `.outputs`, and a task gets `{name}`.
- A task has no outputs, `.restat` or `.always`; setting one is an error.
- `smith_default` is required.
- `.command` and `.run` are exclusive: setting both is an error, and so is `.args` without `.run`.

### C functions as steps (`.run`)

A recipe that is a program, such as game's `check-symbols` or `check-libc`, is a C function in build.c. It reads build
outputs (objects, programs), so it runs during the build, after they exist; the executor runs it as a step.

- **The generator rule.** The edge's command is
  `.smith/program --run <function> [--input <path>]... [--output <path>]... [--arg <word>]...`, followed by the stamp's
  `&& touch` for a step with no outputs. `<function>` is the function's address minus the address of a fixed function in
  the runtime, in hex. Both are in the same executable, and the executor reruns every `.run` step whenever that executable
  changes (it is each `.run` edge's implicit input, so editing build.c reruns every `.run` step: conservative and
  correct, not minimal), so the offset is valid for the run that uses it. Words are quoted as in any command. A task
  has no `--output`. Because the command carries the step's whole data, a change in `.inputs` or `.args` reruns the
  step.
- **No replay.** `--run` does not call `build()` or any `smith_defer` callback. It makes a `SmithBuild` (os, arch,
  root, cpus, a fresh arena) and a `SmithRun` from the words, checks with `dladdr` that the address is a function of
  the executable itself (`SMITH_ASSERT`; a failure is a bug), calls the function and exits with its result, so the executor
  fails the step and nothing after it runs. An earlier design replayed `build()` for every `.run` step, which cost 79
  ms per C step in game (`build()` ran brew, git twice and xcrun each time) and let a function depend on state `build()`
  happened to set, or see a different build if `git status` changed in between.
- **The rule: a function's data is its step's lists.** The `-D` options are not passed to the function. Give it what it
  needs in `.inputs`, `.outputs` and `.args`; an option's value goes in `.args`
  (`.args = (const char *[]){smith_option_value(b, "who", "nobody", "a name"), nullptr}`). A `.run` step added inside a
  `smith_defer` callback works the same way. `run->b` works with every helper that needs no `build()` state.
  Measured (`tests/build.sh`): a `.run` task that does nothing costs about the same as a `true` task, 10 ms against 9 ms
  per `smith build`, which is the cost of starting a process.
- **The function.** `int f(SmithRun *run)` returns 0 for success. `run->inputs`, `run->outputs` (the stamp included;
  empty for a task) and `run->args` are the step's own lists, and `run->b` works with every helper. Like a command, it
  writes the outputs it declares; a step with none gets smith's stamp, touched after a zero return. With `.restat`, a function that
  writes its output with `smith_write` gives early cutoff: unchanged bytes keep the mtime and nothing after it reruns.
- **Two functions with the same inputs and args and no outputs** get the same stamp name, which is an error that names
  `.run`; give one of them an output or different `.args`.
- `smith_run_lines(run, argv)` runs a program with no shell and returns its stdout as lines (stderr passes through); a
  program that cannot start, exits non-zero or is killed prints `smith: <argv[0]> failed (status N)` and gives `nullptr`.
  `smith_run_status(run, argv, &lines)` returns the status instead: the exit status, 128 plus the signal, or 127 when
  the program cannot start, and prints nothing. Both limit stdout to 1 MiB.
- `smith_run_program(run, argv, environment)` runs a program with no shell and its stdout and stderr inherited, not
  captured, with `environment` (`NAME=value` entries, may be `nullptr`) added to the current environment; it uses
  `posix_spawnp` with a built `envp`. It returns the exit status, 128 plus the signal, or 127 when the program cannot
  start. It flushes stdout and stderr first, as `smith_run_lines` and `smith_run_status` do, so the function's own
  printing comes first. game's `clip`, which plays a session through `./build/game` with `GAME_*` variables, needs it.
- `smith_run_combined(run, argv, &lines)` is `smith_run_status` with the program's stderr in the same pipe as its stdout
  (`2>&1`), so `lines` holds both in the order the program wrote them; the 1 MiB limit covers both together. For
  comparing a program's whole output with an expected text.
- **A failed `.run` step names itself.** The command also carries `--description <text>` (the step's `.description`, or
  its first output, or a task's name) and, for a step with a `.depfile`, `--depfile <path>`. When the function returns
  non-zero, after it has printed, the program prints `smith: <description>: failed (status N)` to stderr as its last
  line. the executor itself prints the description as the progress line (`[1/1] <description>`), then on failure `FAILED:
  [code=N] <outputs>`, the command (`.smith/program --run ...`) and the step's output, in a smart terminal, `TERM=dumb`
  and a pipe alike (a smart terminal overwrites the progress line); it does not repeat the description, so the line
  smith adds is the one that says which check failed and with what status.
- **What a `.run` step read.** `smith_run_depend(run, path)` records a path; when the function returns, the program
  writes the step's `.depfile` as `<first output>: <paths>` (a space escaped as `\ `, `#` as `\#`, `$` as `$$`), always,
  even with no paths, so a stale one is never read. A path recorded by a step with no `.depfile` is an error, and
  so is a `.depfile` on a `.run` task (it has no output to name). The step then reruns when a recorded file changes,
  for files the function learned at run time (named inside one of its inputs).
- **`.stamp`.** A step whose result is only that it passed names its stamp: `.stamp` is the step's one output (`$out`,
  `.outputs` afterwards), smith touches it when the command succeeds or the function returns 0, and `.depfile` may name
  it as its target (`-MT`), so a syntax check needs no `sh -c '"$@" && touch "$0"'`. `.stamp` with `.outputs` is an
  error (a returned step holds `.outputs = {stamp}`, which is accepted), and so is `.stamp` on a task. A failing step
  leaves no stamp.
- `.pool = "console"` gives the step the terminal. the executor allows one console step at a time, and it keeps the build's own stdin.
- The calls that read the outside world are read again on every `smith build`, because the program reruns each time.
- `smith_glob` takes a list of patterns, so one pattern is a list of one: `smith_glob(b, (const char *[]){"src/*.c",
  nullptr})`. The result is the union, sorted by `strcmp` with each name once, like make's `$(sort $(wildcard a b c))`.
  A `**` component matches zero or more folders (`src/**/*.c` matches `src/a.c` and `src/x/y/b.c`); it is a bounded
  iterative walk (an explicit stack, no recursion, 64 folders deep at most, symbolic links not followed). Every other
  component goes through `fnmatch`, and a hidden entry matches only a component that starts with `.`, as in the shell.
- `smith_join(b, items, separator)` and `smith_list(b, first, ..., nullptr)` build text and lists in the arena, for
  example a unity file: `smith_join(b, smith_map(b, files, "#include \"%s\"\n"), "")`. `smith_list` never dangles, so
  a function can return its list; `smith_list(b, nullptr)` is the empty list.

Which tier each project needs:
- checkers: v1.
- smith: v1. Its `build.c` is at the repository root and replaced its Makefile.
- learner: v1.
- game: v1.5, for the mesh list that a program prints and that the rest of the build depends on (`smith_defer`,
  `smith_lines`). Implemented.

Dropped from the proposals of the case agents:
- `smith_list`: dropped at first, since a compound literal does it, then added once the game port crashed twice on
  literals kept past their block.
- `smith_in`: membership is a loop. The `function` case writes `in_list` in six lines.
- `smith_read`: `smith_lines` covers the cases. A whole-text read can come back if a case needs it.
- `smith_run_temp` and `smith_run_status`: no case needs them yet.

## Found by porting game

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
        .command = (const char *[]){"smith", "-std=c23", "-MJ", "checkers.json", "-g", "-O0", "checkers.c", "-o",
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

    SmithStep run = {
        .command = (const char *[]){"./checkers", nullptr},
        .inputs = (const char *[]){"checkers", nullptr},
        .pool = "console",
    };
    smith_task(b, "run", &run);

    SmithStep clean = {
        .command = (const char *[]){"rm", "-f", "checkers", "checkers.json", "compile_commands.json", nullptr},
    };
    smith_task(b, "clean", &clean);

    smith_default(b, smith_cat(b, checkers.outputs, compile_commands.outputs, nullptr));
}
```

- `smith build` builds `checkers` and `compile_commands.json`; `smith build run` builds what is needed, then runs the
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
- Every smith snippet is a complete `build.c` that passes `smith -std=gnu23 -fsyntax-only -Wall -Wextra -Werror` against
  `smith/build.h`. That checks the API compiles, not that it works.
- Full cases: [build-c-cases.md](build-c-cases.md).

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

- smith's own C stays plain C23 with libc only, and allocates nothing, so a fresh clone builds a stage0 with one command
  and any C23 compiler: `cc -std=gnu23 smith.c -o smith` (checked with Apple clang 21). The stage0 has no C++ and no
  LLVM, and runs an external clang. `smith build llvm` then builds our static LLVM (once, about 9 minutes), and
  `smith build` links the full smith: `smith.c` with `-DSMITH_IN_PROCESS`, `shim/clang.cpp` (the only C++) and the
  archives of the LLVM install, with `-Wl,-dead_strip`. The stage0 then runs smith's own `build.c`, which is
  the second project built by `build.c`, after checkers: `smith build`, `smith build test`, `smith build check`,
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
  executor](#the-executor) says how a job can later run in this process.
