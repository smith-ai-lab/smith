// The core of smith as a library (issue #646): smith.c compiled with -DSMITH_CORE has no main and exports the entry
// points below, which the units runner links so that a test calls them in process. Not shipped to users (the headers
// smith writes into a project are listed in build.c; this one is not). Each entry point is safe to call again in the
// same process.
#pragma once
#include <stdbool.h>
#include <stddef.h>

// The translation of a Makefile of `folder` (runtime/make*.c, as `smith build --dump-config` run there reads it, but
// not formatted): the build.c in `build` and what is not translated (`smith: Makefile:1: not translated: ...`, one
// line each) in `errors`, both NUL-terminated (and cut short when they do not fit). `makefile` is its name, or nullptr
// for the one `smith build` finds (GNUmakefile, makefile, Makefile). False when a buffer cannot be made, the folder
// cannot be entered or it has no Makefile. A Makefile smith refuses ends the process.
bool smith_core_make_translate(const char *folder, const char *makefile, char *build, size_t build_size, char *errors,
                               size_t errors_size);
