---
name: smith-run-test
description: "smith run and smith test: running a project's program and its tests, the arguments they take, the test runner's options (--test, --seed, --seeds, --jobs, --shrink, --full, --isolate), 'defines no task or alias named', 'is a verb, not a target of smith build'. Load when running or debugging the program or the tests of a project that has a build.c."
---

# smith run and smith test

This skill ships in the smith plugin, from `.agents/skills/smith-run-test/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Writing tests is the smith-test skill; writing `build.c` is smith-build.

`smith run` and `smith test` act on what the project's `build.c` declares: the program is the default's, the tests are `smith_tests`'. There is no task to write and no list of files or programs anywhere else.

## smith run

`smith run [<arguments>]` builds `smith_default` and runs the one program it names on the terminal (a `smith_program` output; other things in the default, such as links, are built but not run). Its arguments go to the program, so `smith run level2 --fast` runs `<program> level2 --fast`. `-D` options belong to the build (`smith run -DFONT=x`); `--` ends them, so `smith run -- -Dx` gives the program `-Dx`. When the default names no program or more than one, it says so and lists the default's targets: make `smith_default` name the one program (`smith build <other>` builds the rest).

## smith test

`smith test [<options>]` builds the test runner and runs it. The options are the runner's own:
- `--test <substring>` runs one test, or every test whose name contains it; `--seed <n>` runs a property test on that one seed; `--seeds <n>` sets the keys per property test (100).
- `--jobs <n>` (default: the cores; `--jobs 1` is easier to follow), `--timeout <seconds>` (10), `--shrink 0|1`, `--shuffle <seed>`, `--isolate` (a process per test), `--in-process` (no fork at all, for `smith dbg build/tests -- --test name --in-process`), `--full` (heavy checks at full size), `--trace`, `--color`, `--list`, `--verbose`, `--stats`.
- A failure ends with the line to rerun it, such as `rerun: smith test --test name --seed 123`.

Examples: `smith test`, `smith test --test v3`, `smith test --test prop_v3 --seed 8022860619270860748`, `smith test --full`.

## When it goes wrong

- `smith run: the default of build.c names no program` / `more than one program`: `smith_default` must name exactly one `smith_program` output (aliases are opened); it lists what it names.
- `smith test: build.c declares no tests`: the build calls no `smith_tests`; add it (the smith-test skill). A task of your own that should run with `smith test` sets `.test = true`.
- `this verb was given arguments, but build.c does not read them`: `smith test` was given arguments and build.c calls no `smith_tests` (the runner reads them through `smith_args(b)`).
- `build.c: task 'run' is named after a verb`: no task or alias is named `run`, `test`, `check`, `fmt`, `lint`, `docs` or `prove`; the verb does it, or name the task another way and run it with `smith build <name>`.
- `test is a verb, not a target of smith build`: `smith build run|test|check|fmt|lint|prove|docs` are refused. Use the verb.
- `no build.c in this folder`: run it from the project's folder.
- The old `-DTEST=`, `-DSEED=`, `-DJOBS=`, `-DFULL` options are gone; use the runner's options.
