// Generated split of smith.c (#2685): what runtime/quiet.c shares with the other files.
#pragma once

#include "core.h"

// Quiet output (issue #78): what a run prints is a line per stage, and the rest goes to a log under `.smith/log/`.
// A failure shows a bounded excerpt of its output (the lines from the first one that reports a failure) and the path
// of the log. `-v` (or SMITH_OUTPUT=full, which the outermost run sets for everything beneath it, so a test of smith
// that runs `smith test` sees what it always saw) prints everything as it happens. Linked into smith.
constexpr size_t smith_quiet_lines = 30;

constexpr size_t smith_quiet_columns = 200;

constexpr size_t smith_quiet_excerpt_max = smith_quiet_lines * (smith_quiet_columns + 8) + 256;

bool smith_quiet_full(void);

size_t smith_quiet_count_lines(const char *text, size_t size);

size_t smith_quiet_excerpt(char *out, const char *text, size_t size, size_t *hidden);

const char *smith_quiet_log_path(char *path, size_t capacity, const char *name);

const char *smith_quiet_read(const char *path, size_t *size);

bool smith_quiet_field(const char **at, size_t *value, const char *after);

void smith_quiet_detail(const char *verb, const char *text, size_t size, char *out, size_t capacity, char *coverage,
                        size_t coverage_capacity);
