---
type: design
title: smith test --changed runs the tests a change can reach
description: The tests to run for a diff are the ones the reverse dependency graph reaches from the changed files, no test declares anything, and the train's full run is the net.
tags: [test, ci, machine-load]
---
# smith test --changed runs the tests a change can reach

Every red batch on the train was a fault in a test the author did not know to run. `smith test --changed [--base <ref>] [--dry-run]` (`runtime/changed.c`, issue #441) names the tests a diff can reach by walking the **reverse dependency graph** from the changed files. No test declares anything: what a test depends on is found from the sources.

**The diff.** The files that differ from `<ref>` (default: where the branch left `origin/main`, else `main`), the working tree and untracked files included. `--dry-run` prints each selected test file with the reason and the `--test` list it would run, and runs nothing. A run passes `--test a::,b::` to the runner, which takes comma-separated substrings (`lib/smith/test.smith`); smith's own `test-units` forwards the arguments of `smith test` to its runner (`build.c`, `smith_args`). It prints the tests it selected, so a miss (a test the train rejected that `--changed` left out) can be counted.

**The graph.** Every `.smith`, `.c`, `.h`, `.cpp`, `.hpp` and `.m` file is scanned for `module`, `import` and `#include` lines; each names a project file by the end of its path (`a::b` is `a/b.smith`, `<smith/x.h>` is `smith/x.h`), and the files that import a changed file, transitively, are changed too. No build is needed, so a replay over old commits needs none. A `*_test.smith` or `*_test.c` file is selected when it is changed or reached this way, or when it runs smith (`scratch::`, `SMITH_SELF`) and a source of smith changed (smith.c, `runtime/`, `lib/`, `include/`, `templates/`, `scripts/`, ...): a test that starts smith depends on all that is compiled into it, the flags of module files and the build conventions included.

**What it does not do.** A file the graph does not know (not a source, not part of smith) selects nothing; the train's full run catches what that misses (a test that reads files as data, such as the tree-sitter test that parses every `.c`). Recording what tests read at run time is not built; it becomes an issue if misses are more than rare. The files that select every test: `build.c`, `smith.c`, `runtime/lang*`, `patches/`, `.github/`, `shim/`, `tests/units/`. Conservative is fine.
