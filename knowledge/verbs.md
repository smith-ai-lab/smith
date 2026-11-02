---
type: convention
title: smith's top level is only verbs, with fixed meanings
description: The verbs cc, c++, build, new, fetch, cache, docs, version, fmt, lint, lsp, run, test, check, prove and dbg each have one fixed meaning read from what the project declares.
tags: [verbs, cli]
---
# smith's top level is only verbs, with fixed meanings

**smith's top level is only verbs**, as zig's is: `smith cc` and `smith c++` compile (clang's arguments, unchanged);
`build`, `new`, `fetch`, `cache`, `docs`, `version`, `fmt`, `lint`, `lsp`, `run`, `test` and `check` do the rest. The bare form `smith
foo.c -o foo` is an error that says to run `smith cc foo.c -o foo`, so nothing is ambiguous. For what needs one
program path (`CC=` in a Makefile, CMake's `CMAKE_C_COMPILER`, the first word of a `compile_commands.json` command,
which clangd reads as a compiler), `smith build install` also installs `smith-cc` and `smith-c++`, links to smith that
act as `smith cc` and `smith c++` by their name, as `smith++` did. `smith++` is gone. The program a `build.c` names as
a compiler is `smith-cc` or `smith-c++`; `"smith"` is refused with that hint.

The one exception at the top level (owner, 2026-10-31, #1138): `smith --version` prints what `smith version` prints, and `smith --help`, `-h` and `help` list the verbs, `smith help <verb>` prints that verb's page, and bare `smith` prints the list too (exit 0); all help is printed from the verb table in `smith.c`, and `smith <verb> --help` is smith's page except for the tool verbs (`cc`, `c++`, `lsp`, `dbg`, `dap`, `fmt`, `lint`), where it is the tool's own; any other flag stays the error above, and `smith cc --version` is clang's.

**`smith docs` checks a project's `knowledge/` (OKF) and `.agents/skills/`, and `smith new` starts every
project with them** (`runtime/docs.c`; skill smith-docs); the concept types and required fields are `types:` and
`requires:` in the frontmatter of `knowledge/index.md`, and the command line's `--type` and `--require` replace them.

**The verbs have fixed meanings, and a project's own tasks run with `smith build <task>`** (issue #40;
`runtime/verbs.c`, knowledge/build-file.md). Each verb acts on what the project already declares, and smith keeps no list of
its own: `smith fmt` formats every C, C++, Objective-C and `.smith` file git sees to `.clang-format` (what
`.clang-format-ignore` names is left alone); `smith lint` is clang-tidy on the project's own sources among the build's
compiles, in parallel; `smith docs` checks `knowledge/` and `.agents/skills/` (the concept types and
required fields are declared in `knowledge/index.md`'s frontmatter); `smith prove` proves the contracts of the
C and `.smith` sources of each program the build links; `smith test` runs what `smith_tests` declares (its arguments go to the runner); `smith run` runs the program
that `smith_default` names, when it names exactly one (its arguments go to the program); `smith check` runs fmt (the dry
run), docs, lint, test and prove, reports every part and fails if any did. With arguments `fmt`, `lint` and `prove`
are clang-format, clang-tidy and the prover, as before. `smith_task` and `smith_alias` refuse a name that is a verb. A
run of build.c writes `.smith/build.info` and `.smith/compile_commands.json`, which the verbs read, and the manifest's
targets `smith_needs` and `smith_tests`. `smith build <verb>` is refused with a pointer to the verb. `smith prove` is
the prover with arguments; `smith dbg` is lldb.

`smith check` is the standard one here too: `smith fmt` (the dry run) over every file git sees, `smith docs`, `smith
lint` (clang-tidy over the compiles of `build.c`, which also declares the libraries' and tests' sources with
`smith_compile_command`; `shim/.clang-tidy` is the C++ glue's own rules), `smith test` (the `test-<name>` tasks, `tests/compare_test.smith`
among them) and `smith prove`; it must pass before a change is done. `smith fmt` (no arguments) rewrites the sources.

smith's top level is only verbs (`smith cc`, `smith c++`, `smith build`, `smith new`, ...), so what a project does is
`smith <verb>` and what builds a target is `smith build <target>`. The verbs a person types every day mean the same
thing in every project, and each takes what it needs from what the project already declares; smith keeps no list of its
own (issue #40):

| verb | with no arguments | with arguments |
| --- | --- | --- |
| `smith fmt` | formats every C, C++, Objective-C and `.smith` file git sees (tracked, and untracked but not ignored; `.c .h .m .mm .cpp .cc .cxx .hpp .hh .smith .metal`) to `.clang-format` (smith's own, embedded, where the file is not found up the tree; `--dump-config` prints the effective one); what `.clang-format-ignore` names is left alone | clang-format, with its arguments unchanged, except that file names with no option that chooses another mode (`-n`, `--dry-run`, `--lines`, `--assume-filename`, ...) are rewritten in place as with `-i` (clang-format prints them, and an agent lost its context to a dumped file); standard input with `--assume-filename` prints; options alone (`--dry-run`, `-n`, `--Werror`, `-i`) are the project verb, never standard input |
| `smith lint` | builds what the compiles read (generated headers), then runs clang-tidy, with `.clang-tidy` (smith's own where none is found; `--dump-config` prints it), on the project's own sources among the build's compiles, side by side | clang-tidy, with its arguments unchanged; `--quiet` alone is the project verb |
| `smith docs` | checks `knowledge/` and `.agents/skills/` of the nearest folder at or above the current one that has `knowledge/` or `.agents/skills/` (up to the repository's root; else the current folder), with the types and fields `knowledge/index.md` declares | the check's options (`--type`, `--require`, `--depfile`, a root) |
| `smith prove` | proves the contracts of the build's C and `.smith` sources, with the flags of their compiles: every function with a contract, minus the trusted ones (`smith_trusted()`) | the prover, with its arguments; `-v`, `--budget=`, `--emit-ir=` and `--no-cache` alone are the project verb |
| `smith test` | builds and runs the tests `smith_tests` declares | the arguments are the test runner's options |
| `smith run` | builds `smith_default`, then runs the one program it names | the arguments go to the program |
| `smith check` | fmt (clang-format's dry run), docs, lint, test and prove, each reported | refused |
| `smith dbg` | not available yet (issue #7) | |

**A project's tasks run with `smith build <task>`, and none is named after a verb.** `smith_task` and `smith_alias`
refuse `fmt`, `lint`, `docs`, `prove`, `check`, `run` and `test` (`smith: build.c: task 'run' is named after a verb;
`smith run` does the same thing in every project, so use it, or name task another way and run it with `smith build
<name>``). The reasons, in the order they were weighed:
- Each project used to carry about sixty lines that said the obvious (a list of sources "add a file when you add one", the
  clang-format and clang-tidy commands, the docs step), the verbs meant whatever each wrote, and a project without the task
  got an error (smith's own repository had no `docs` task, so `smith docs` failed there). What a verb does is now the same
  everywhere, and a project that builds by convention (what `smith new` writes) has no verb tasks to keep up.
- The project already says what the verbs need: `smith_object` and `smith_program` say what is compiled and with which
  flags, `smith_tests` says what the tests are, `smith_default` says what `run` runs, and git says which files exist.
  smith writes what it learned from the run of build.c in `.smith/` for the verbs (below).
- The one thing the verbs need that build.c had no way to say is the arguments: `smith run` and `smith test` take them.
  The test runner's arrive in build.c as **`smith_args(b)`**, a list that is empty for none, which `smith_tests` appends
  to the runner's command, so `smith test --test name --seed 7 --jobs 1` is the runner's own command line. They travel as
  the one option `-DSMITH_ARGS`, each after a ";" (with "%", ";" and newline escaped); a build.c that never calls
  `smith_args` and is given arguments gets an error that says so. The program's arguments are not build.c's: `smith run`
  starts the program itself, with them.
- `-D` options still work on a verb (`smith test -DFONT=...`), since they are build.c's, not the verb's; `--` ends them,
  so that a program can be given one (`smith run -- -Dx`).

**What a verb reads from the build.** Every run of build.c writes `.smith/build.info`, one line per fact with tabs
between the fields, and `.smith/compile_commands.json`, and the manifest has two targets of smith's own:
- `compile<TAB>source<TAB>flag...`: a source of the project's own that the build compiles, with the words of its first
  compile after the compiler (`-MMD -MF`, `-c` and `-o` among them). Not a generated file, nothing under `.smith/` and no
  absolute path (a package's). Sources that `smith_compile_command` declares (never compiled, but read with flags) are
  here too, and build.c itself, with the flags smith compiles it with.
- `test<TAB>task`: a task with `.test` (what `smith_tests` makes its task; smith's own `test-<name>` tasks set it too), or a step with `.test` (its outputs).
- `program<TAB>path`: the output of a `smith_program`; `unit<TAB>source...`: the own sources of a program's objects (not
  the runner `smith_tests` makes); `runner<TAB>path`: the program `smith_tests` makes, the tests' runner (also a
  `program` line), which a debugger launches with `--test <name> --in-process`; `default<TAB>target`: a target of
  `smith_default`, aliases opened.
- `need<TAB>source<TAB>file...`: the generated files one compile of an own source waits for (no line for a source the
  build only declares); `smith lint --changed` builds those of the files it chooses, after the build wrote this file
  with the empty target `smith_info`.
- the target `smith_needs` is the generated files those compiles read (their implicit and order-only inputs that a step
  makes: the tests' list header, a module's header), and `smith_tests` is the test tasks. `smith lint` and `smith prove`
  build `smith_needs`, `smith test` builds `smith_tests`; both exist in every manifest, empty for none.
- `.smith/compile_commands.json` lists each source once, by its first compile, for clang-tidy (`-p .smith`); the
  project's own `smith_compile_commands` file is the editor's and lists every output.

How each verb uses it:
- **fmt** needs no build: `git ls-files -z --cached --others --exclude-standard`, through smith's io, the wanted
  extensions, the files that are there (a file removed and not yet staged is still listed). Outside a git repository it
  says to name the files (`smith fmt -i <files>`). clang-format reads `.clang-format-ignore` itself when it is given files
  (the generated tree-sitter parser, the broken inputs of a test).
- **lint** runs one clang-tidy per source, side by side (one per core), each with its output kept together, so the
  findings of different files do not interleave. `.smith` sources are linted as written (shim/tidy.cpp); a smith whose clang has no token hook (the stage0) leaves them out and says so once. It
  lints the first compile of a source, so a program built in three variants is checked once, with the first's flags. The checks are the project's `.clang-tidy`, or smith's defaults.
- **prove** proves each program's C and `.smith` sources together, as the program links them (`unit<TAB>source...` in the info; the
  tests' runner is left out, and so is a source the build compiles into no program), with the flags of their first
  compiles less what only instruments or caches (`-fsanitize`, modules, frame pointers, pattern initialization, the
  depfile): sources of a program with the same flags go in one `smith prove a.c b.c -- flags`, and programs with the same
  sources (debug, release and fast) are proved once. A program with no contract has nothing to prove, which is quick.
- **test** builds `smith_tests` and so runs the runner (the task, on the console); a build that declares none says so.
- **run** builds the default, then starts the program, replacing smith, so it has the terminal. The default must name
  exactly one program (the others in it, such as links, are built but not run); otherwise `smith run` says how many it
  names and lists them.
- **check** runs fmt, docs, lint, test and prove in that order, cheapest first, and does not stop at a failure: each part
  prints `smith check: <part> ok: <detail> (<n> s wall, <n> s cpu)` or `FAILED (status N)` and the excerpt of its log (Quiet output, below), and the last line is `smith check: ok` or
  `smith check: failed: <parts>`. The exit status is 1 if any part failed. prove is skipped (and says so) by a smith
  without a prover, and lint, test and prove are skipped (and say so) in a folder with no build.c, main.smith or main.c.

**Quiet output** (issue #78): agents pay for every byte a tool prints, and again on every later turn, so `smith check`,
`smith test`, `smith lint` and a project's `smith prove` print one line per stage and write the rest to a log:
`smith test: ok (48 tasks, 1530 passed, 83.8 s)`, and in `smith check` `smith check: test ok: 48 tasks, 1530 passed
(83.8 s wall, 619.1 s cpu)`. A passing `smith check` of smith prints about 8 lines (191,753 bytes before, about 500 now).
The stage runs as a child with SMITH_OUTPUT=full, its output (build progress, per-test lines, the prover's
`trusted` lines) in **`.smith/log/<verb>.log`** (`test.log`, `prove.log`, `lint.log`; every run writes it whole, and
`.smith/` is smith's own folder that git ignores). A failure prints its line, a bounded excerpt of the log (at most 30
lines of 200 columns, from the first line that reports a failure: `FAILED`, `FAIL `, `error:`, `not ok`; for a step
that failed, its executor header and command, then what the step reported) and `N more lines; the full output is in
<log>`. `-v` prints as before (for `smith test`, it is the runner's `--verbose`; `smith prove -v` is the prover's cost
report; `smith check -v` streams every part and lists every task). SMITH_OUTPUT=full is also the way nested runs
stay whole: the quiet run exports it, so a test of smith that runs `smith test` or `smith build` reads what it always
read, and a script of `tests/*.sh` prints its `ok` lines into the log. **`smith build`** (and any build) bounds a failed
step the same way, unless `-v` or SMITH_OUTPUT=full: the excerpt, and the step's whole output in
`.smith/log/<its first output, with / as _>.log`. The code is `runtime/quiet.c` (the excerpt, the log path, the detail
read from the log) and `runtime/verbs.c` (`smith_quiet_verb`, `smith_check_run`). `smith prove` with files (the
prover itself) is not quiet.

**The verbs are not targets of `smith build`.** `smith build run`, `test`, `check`, `fmt`, `lint`, `docs` and `prove` are
refused with a pointer to the verb (`smith: test is a verb, not a target of smith build; run `smith test``). The
alternative, both working, leaves two ways to say the same thing in every document, script and CI file, and a
person who learns `smith build test` is never told that `smith test --seed 7` exists. A refusal costs one message once;
every other target (`release`, `fast`, `clean`, a project's own) is still `smith build <target>`. The dispatch is one
table, `smith_verbs` in smith.c, so a new verb is a row (the actions are in `runtime/verbs.c`).

For a program that needs one path to the compiler (`CC=` in a Makefile, CMake, the first word of a
`compile_commands.json` command, which clangd reads as a compiler), `smith build install` also installs the links
`smith-cc` and `smith-c++`, which act as `smith cc` and `smith c++` by their name. A `build.c` names them as its
compilers (`.compiler = "smith-cc"`); `"smith"` alone, or a path that ends in `/smith` (an environment's `CC=$HOME/.local/bin/smith`), is an error that says so before anything runs, since `smith foo.c` has no meaning. A smith that runs a build puts its own folder first on `PATH` when it holds `smith-cc`, so the compilers a build file names are that smith's links, never the installed ones (a `CC` path wins).
