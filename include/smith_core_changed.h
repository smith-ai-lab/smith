// What `smith test --changed` selects (runtime/changed.c, issue #646), in process, through `smith_core_changed_select`
// (smith.c compiled as a library by tests/units/build.c, as include/smith_core.h). No git runs: the files and the
// changes are texts the test gives, and the files' contents are read from the folder, so a scratch folder written
// by the test is all it needs. build.c judged by the functions of its diff asks git and stays an end-to-end test.
// Safe to call again in the same process: each call forgets the one before.
#pragma once
#include <stdbool.h>
#include <stddef.h>

// The selection for `files` (the repository's files, one path a line, as `git ls-files` prints them) and `changes` (one
// `A`, `M` or `D`, a tab and a path a line, as `git diff --name-status` prints them), in `folder`. `out` has,
// NUL-terminated (cut short when it does not fit), what `smith test --changed --dry-run` prints: the reasons and the
// last line, `--dry-run: ...` and `--test <names>`. False when the folder cannot be entered or `out` cannot be made.
bool smith_core_changed_select(const char *folder, const char *files, const char *changes, char *out, size_t out_size);

// The files `smith lint --changed` chooses, from the same texts (`files`, `changes`) in `folder`. `out` has,
// NUL-terminated (cut short when it does not fit), each file wanted, one a line, then the line `smith lint --changed`
// prints ("N changed files since ...: M of K files to lint"), or the line saying every file is linted. The base
// printed is `HEAD`. False when the folder cannot be entered or `out` cannot be made. Safe to call again.
bool smith_core_changed_lint(const char *folder, const char *files, const char *changes, char *out, size_t out_size);

// The base `--changed` chooses with no ref, in the git repository `folder`: the merge-base with the upstream, else with
// origin/<default branch> (origin/HEAD, else origin/main), else with a local main; `out` has "<ref> (<12 hex>)" for the
// ref chosen ("upstream" for the upstream), or is empty when there is none. False when the folder cannot be entered.
bool smith_core_changed_base(const char *folder, char *out, size_t out_size);
