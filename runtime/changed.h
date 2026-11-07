// Generated split of smith.c (#2685): what runtime/changed.c shares with the other files.
#pragma once

#include "core.h"

constexpr size_t smith_changed_files_max = 16384;

constexpr size_t smith_changed_edges_max = 1U << 17;

constexpr size_t smith_changed_text_max = 1U << 21;

constexpr size_t smith_changed_argument_max =
    12000; // the runner's arguments travel as -DSMITH_ARGS (16384 bytes in all)

extern const char *smith_changed_files[smith_changed_files_max];

extern size_t smith_changed_file_count;

extern uint32_t smith_changed_from[smith_changed_edges_max];

extern uint32_t smith_changed_to[smith_changed_edges_max];

extern size_t smith_changed_edge_count;

extern bool smith_changed_overflow;

extern char smith_changed_text[smith_changed_text_max + 1];

// Text that grows as names are added, in memory mapped from the OS (smith's own C has no malloc): the filter of a
// selection has no size at which it silently becomes "every test".
typedef struct {
    char *data;
    size_t used;
    size_t capacity;
} SmithChangedBuffer;

const char *smith_changed_buffer_text(const SmithChangedBuffer *buffer);

bool smith_changed_starts(const char *text, const char *prefix);

bool smith_changed_ends(const char *text, const char *suffix);

bool smith_changed_is_test(const char *path);

bool smith_changed_is_source(const char *path);

bool smith_changed_word(char c);

size_t smith_changed_read(const char *path);

void smith_changed_add_file(const char *path);

bool smith_changed_git(const char *const *arguments, void (*line)(char *));

void smith_changed_on_file(char *text);

void smith_changed_build_graph(void);

bool smith_changed_runs_smith(const char *text);

extern const char *smith_changed_self;

void smith_changed_filter_add(SmithChangedBuffer *filter, const char *name);

void smith_changed_filter_file(const char *path, SmithChangedBuffer *filter);

bool smith_changed_write_costs(char *path, size_t size);

typedef enum { SmithChanged_Failed, SmithChanged_Full, SmithChanged_None, SmithChanged_Some } SmithChangedResult;

SmithChangedResult smith_changed_select(const char *base_name, const char *head, SmithChangedBuffer *filter);

SmithChangedResult smith_changed_lint_select(const char *given, char *base, size_t size, const char **why);

bool smith_changed_lint_wants(const char *path);

void smith_changed_lint_says_all(const char *why);

void smith_changed_lint_says(size_t kept, size_t count, const char *base);
