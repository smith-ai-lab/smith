// The report of `smith test --coverage report` (issue #456, runtime/coverage.c) in process (issue #646), through
// `smith_core_coverage_report` (smith.c compiled as a library by tests/units/build.c, as include/smith_core.h). No git
// and no compiler start: the table is text and the added lines are given. Safe to call again in the same process: the
// tables start empty each call, as in a new `smith test --coverage`.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A run of lines a diff added: `count` lines of `path` from line `first` (0 lines is an insertion that adds none).
typedef struct {
    const char *path;
    uint32_t first;
    uint32_t count;
} SmithCoreChanged;

// The report for the table in `table` (the text of cov/table.tsv) and the lines in `changed[0..count)`, as the file
// `report` writes: the totals, one `uncovered` line for each added line that coverage counts and no test ran, and the
// `changed` totals. NUL-terminated in `output` (cut short when it does not fit). False when the table is malformed or
// the buffer cannot be made.
bool smith_core_coverage_report(const char *table, const SmithCoreChanged *changed, size_t count, char *output,
                                size_t output_size);
