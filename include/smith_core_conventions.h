// The choice of a build file in process (issue #1292, part of #646), through `smith_core_build_file` (smith.c compiled
// as a library by tests/units/build.c, as include/smith_core.h). It is the decision `smith build` makes first from the
// file names of the folder, with no compile. Safe to call again in the same process: it runs in a child of the caller,
// so the process it ends (a refusal calls `smith_die` or `exit`) is the child's. Not shipped to users.
#pragma once
#include <stddef.h>

// What the folder gets, as `smith build` reads it.
enum {
    SMITH_CORE_BUILD_FILE_C = 0,           // the project's build.c
    SMITH_CORE_BUILD_FILE_SMITH = 1,       // the project's build.smith
    SMITH_CORE_BUILD_FILE_CONVENTIONS = 2, // smith's conventions: a main.smith or main.c and no build file
    SMITH_CORE_BUILD_FILE_REFUSED = -1,    // a refusal: see `out` and the status
};

// The build file the existing `folder` gets. Returns the kind above. On a refusal it returns
// SMITH_CORE_BUILD_FILE_REFUSED, `out` gets what `smith build` prints (standard output and error together,
// NUL-terminated and cut short when it does not fit) and `*status` the exit status (2 for a refusal); otherwise `out`
// is empty and `*status` is 0. `status` may be nullptr. A child that cannot be made returns
// SMITH_CORE_BUILD_FILE_REFUSED with status -1.
int smith_core_build_file(const char *folder, char *out, size_t out_size, int *status);
