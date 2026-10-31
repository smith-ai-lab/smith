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
