---
type: design
title: How smith build runs
description: What smith build does from compiling build.c to running the manifest, and what it records in .smith.
tags: [build]
---
# How smith build runs

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

## A change of compiler rebuilds everything

ninja decides that an object is out of date from its command, its inputs and their mtimes. A new smith, or another
`SMITH_CLANG`, changes none of them when the command says `smith-cc`, so the objects would look up to date though another
compiler built them, and sanitized objects from two compilers do not link together (their ASan ABIs differ). So every
`smith_object` and `smith_program` depends on a file `.smith/compiler/<hash of the compiler word>` that says which
compiler it is:
- a compiler that is not smith: its executable's real path, size and mtime (a copy of the same bytes is a new compiler);
- smith (a program named `smith...`: `smith-cc`, `smith-c++`) is the clang it runs, not its own file, which changes with
  every build of smith and would rebuild every project after each install. `<smith-cc> --smith-which -c a.c` says which: with clang linked in,
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

Built in, the tools of ninja's that this build uses (the full list and what is dropped is in [The executor](executor.md)):
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
