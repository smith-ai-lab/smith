---
name: smith-run-test
description: "smith run and smith test: running a project's program and its tests, the arguments they take, the test runner's options (--test, --seed, --seeds, --jobs, --shrink, --full, --isolate), 'defines no task or alias named', 'is a verb, not a target of smith build'. Load when running or debugging the program or the tests of a project that has a build.c."
---

# smith run and smith test

This skill ships in the smith plugin, from `.agents/skills/smith-run-test/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Writing tests is the smith-test skill; writing `build.c` is smith-build.

A project verb builds and runs the task or alias of its name in the project's `build.c`. There is no list of files or programs anywhere else.

## smith run

`smith run [<arguments>]` builds the program the `run` task names and runs it on the terminal. Its arguments go to the program, so `smith run level2 --fast` runs `<program> level2 --fast`. `-D` options belong to the build (`smith run -DFONT=x`); `--` ends them, so `smith run -- -Dx` gives the program `-Dx`.

## smith test

`smith test [<options>]` builds the test runner and runs it. The options are the runner's own:
- `--test <substring>` runs one test, or every test whose name contains it; `--seed <n>` runs a property test on that one seed; `--seeds <n>` sets the keys per property test (100).
- `--jobs <n>` (default: the cores; `--jobs 1` is easier to follow), `--timeout <seconds>` (10), `--shrink 0|1`, `--shuffle <seed>`, `--isolate` (a process per test), `--in-process` (no fork at all, for `smith dbg build/tests -- --test name --in-process`), `--full` (heavy checks at full size), `--trace`, `--color`, `--list`, `--verbose`, `--stats`.
- A failure ends with the line to rerun it, such as `rerun: smith test --test name --seed 123`.

Examples: `smith test`, `smith test --test v3`, `smith test --test prop_v3 --seed 8022860619270860748`, `smith test --full`.

## When it goes wrong

- `smith run: build.c defines no task or alias named run`: the project has no such target; add `smith_task(b, "run", ...)` (the smith-build skill). The same for `test`, `prove`, `check`, `fmt`, `lint`. A project made by `smith new` has all of them; one that predates `test` and `prove` in the template adds `smith_tests` and a `prove` task (copy them from `smith new`'s build.c).
- `this verb was given arguments, but build.c does not read them`: the task does not call `smith_args(b)`; give the arguments to its command.
- `test is a verb, not a target of smith build`: `smith build run|test|check|fmt|lint` are refused. Use the verb.
- `no build.c in this folder`: run it from the project's folder.
- The old `-DTEST=`, `-DSEED=`, `-DJOBS=`, `-DFULL` options are gone; use the runner's options.
