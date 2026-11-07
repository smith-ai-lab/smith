// Generated split of smith.c (#2685): what runtime/coverage.c shares with the other files.
#pragma once

#include "core.h"

constexpr size_t smith_cov_files_max = 1024;

constexpr size_t smith_cov_functions_max = 1U << 16;

constexpr size_t smith_cov_lines_max = 1U << 15;

typedef struct {
    uint32_t file;
    uint32_t first;
    uint32_t last;
    const char *name;
} SmithCovFunction;

extern size_t smith_cov_pool_used;

extern const char *smith_cov_files[smith_cov_files_max];

extern size_t smith_cov_file_count;

extern SmithCovFunction smith_cov_functions[smith_cov_functions_max];

extern size_t smith_cov_function_count;

extern bool smith_cov_function_run[smith_cov_functions_max];

extern bool smith_cov_overflow;

void smith_cov_cross(const char *limit);

const char *smith_cov_copy(const char *text);

int smith_cov_file_index(const char *path);

extern char smith_cov_root[PATH_MAX];

FILE *smith_cov_open(const char *path, char *temporary, size_t size);

bool smith_cov_close(FILE *out, const char *temporary, const char *path);

void smith_cov_clean_name(const char *name, char *out, size_t size);

const char *smith_cov_env(const char *name);

int smith_cov_spawn(const char *const *argv, const char *output, bool output_to_stderr);

extern double smith_cov_cpu_seconds;

bool smith_cov_code_line(const char *text);

int smith_coverage_main(int argc, char **argv);
