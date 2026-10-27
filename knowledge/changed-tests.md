---
type: design
title: smith test --changed runs the tests a change can reach
description: The tests to run for a diff are the ones the reverse dependency graph reaches from the changed files, no test declares anything, and a path the graph does not know runs everything.
tags: [test, ci, machine-load]
---
# smith test --changed runs the tests a change can reach

Every red batch on the train was a fault in a test the author did not know to run. `smith test --changed [--base <ref>] [--dry-run]` (`runtime/changed.c`, issue #441) names the tests a diff can reach by walking the **reverse dependency graph** from the changed files. No test declares anything: what a test depends on is found from the sources.

**The diff.** The files that differ from `<ref>` (default: where the branch left `origin/main`, else `main`), the working tree and untracked files included. `--dry-run` prints each selected test file with the reason and the `--test` list it would run, and runs nothing. `--list [<base> [<head>]]` (issue #506) prints the selection alone on stdout, one entry per line (a test name, `module::`, or `*` for every test), for a script that asks what a pull request would select; with a `<head>` the changed paths are those of `git diff base head`, untracked files left out, while the sources and records are still read from the working tree. A run passes `--test a::,b::` to the runner, which takes comma-separated substrings (`lib/smith/test.smith`); smith's own `test-units` forwards the arguments of `smith test` to its runner (`build.c`, `smith_args`). It prints the tests it selected, so a miss (a test the train rejected that `--changed` left out) can be counted.

**The graph.** Every `.smith`, `.c`, `.h`, `.cpp`, `.hpp` and `.m` file is scanned for `module`, `import` and `#include` lines; each names a project file by the end of its path (`a::b` is `a/b.smith`, `<smith/x.h>` is `smith/x.h`), and the files that import a changed file, transitively, are changed too. No build is needed, so a replay over old commits needs none. A `*_test.smith` or `*_test.c` file is selected when it is changed or reached this way, or when it runs smith (`scratch::`, `SMITH_SELF`) and a source of smith changed (smith.c, `runtime/`, `lib/`, `include/`, `templates/`, `scripts/`, ...): a test that starts smith depends on all that is compiled into it, the flags of module files and the build conventions included.

**Unknown means run it.** A changed path the graph does not know (not a test, not part of smith, and no file imports or includes it: a new `.c` nothing includes, a fixture read as data, a text file) selects every test, so a change the graph cannot follow is never skipped. The files that select every test too: `build.c`, `smith.c`, `runtime/lang*`, `patches/`, `.github/`, `shim/`, `tests/units/`. Conservative is fine.

**Cheapest first (#506).** `--cheapest-first` writes `.smith/costs.tsv` from the `cpu` line of each record and passes it as the runner's `--costs`: the selected tests start unknown first, then by cpu seconds, so a wide selection under `--fail-fast` and a time limit still meets the cheap breakage first.

**Markdown is placed.** A changed `.md` file (a skill, a concept, the README) is not unknown: it selects the test files whose source names `.md` (skills, readme and docs tests, goldens of Markdown), "it reads Markdown", and no more (#506).

**Records first.** When per-test records exist (`.smith/records`), a test that runs smith is selected when its record is stale instead of by the graph's "all of smith" edge, and the graph decides whatever no record names: [coverage-selection](coverage-selection.md).

**What it weighs.** The runner's `--stats` ends with a line `cpu<TAB>seconds<TAB>name` for each test, its CPU time with the processes it waited for, then `wall<TAB>start<TAB>seconds<TAB>name` (when it started and its wall time, for the busy workers over time); the train's full run can publish them, so the share of CPU a selection runs is measured, not guessed. Recording what tests read at run time is not built; it becomes an issue if misses are more than rare.
