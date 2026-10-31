// The decisions of `smith lint`'s cache (issue #621, runtime/lint_cache.c) in process (issue #646), through
// `smith_core_lint_plan` (smith.c compiled as a library by tests/units/build.c, as include/smith_core.h). No
// clang-tidy starts: a file the cache cannot answer "runs" a fake that reads the files the case names, ends with the
// status it names and prints the output it names, and the cache stores that as it stores the real run's. Safe to call
// again in the same process: the run's tables start empty each call, as in a new `smith lint`.
#pragma once
#include <stdbool.h>
#include <stddef.h>

// One file of a lint run, and what the fake clang-tidy does when the cache does not have it.
typedef struct {
    const char *file;   // its name as `smith lint` gives it, relative to the project's folder
    const char *flags;  // the tab-separated flags of its compile (the cache key)
    const char *reads;  // the files clang-tidy reads for it, names separated by newlines, relative to the folder
    int status;         // the exit status of the fake: 0 or 1 are results the cache keeps, another is a crash
    const char *output; // what it prints
} SmithCoreLintFile;

// What happened to one file: it ran (a miss) or came from the cache, and the exit status either way.
typedef struct {
    bool ran;
    bool cached;
    int status;
} SmithCoreLintResult;

// A lint run of `count` files of the project in `folder`, the cache in `cache` (as XDG_CACHE_HOME) and `setting` as
// SMITH_LINT_CACHE ("0": off, "why", or nullptr). results[i] says what happened to files[i]; `output` has the output of
// the files in order, replayed or fresh, NUL-terminated (cut short when it does not fit). False when the folder cannot
// be entered, a buffer cannot be made or there are too many files.
bool smith_core_lint_plan(const char *folder, const char *cache, const char *setting, const SmithCoreLintFile *files,
                          size_t count, SmithCoreLintResult *results, char *output, size_t output_size);
