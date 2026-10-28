// The project root of a program, for the debugger (issue #621). Included by smith.c before symbolize.c, and by mcp.c.
#pragma once
//
// A debug object names no folder: smith_object compiles it with -fdebug-prefix-map=<root>=., so its compilation
// directory is `.` and a source is `./dir/file.c`. A debugger run from another folder resolves that against its own
// folder and finds nothing. `smith dbg`, `smith dap`, `smith mcp` and `smith symbolize` map `.` to the root of the
// program they debug: the nearest folder above the program that holds a build file or the build's `.smith/build.info`.
// A program no smith built has no such folder, and nothing is mapped.

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef SMITH_ASSERT
#include <assert.h>
#define SMITH_ASSERT(condition) assert(condition)
#endif

constexpr size_t smith_project_levels_max = 64;

static bool smith_project_marked(const char *folder) {
    static const char *const markers[] = {"build.c", "build.smith", ".smith/build.info"};
    char path[PATH_MAX];
    for (size_t i = 0; i < sizeof markers / sizeof markers[0]; i++) {
        int written = snprintf(path, sizeof path, "%s/%s", folder, markers[i]);
        if (written > 0 && (size_t)written < sizeof path && access(path, F_OK) == 0) return true;
    }
    return false;
}

// The root above `from` (a file or a folder; relative to the current folder). False when no folder above it, up to the
// top, is marked. `root` is the real path.
static bool smith_project_root(const char *from, char *root, size_t capacity) {
    SMITH_ASSERT(from != nullptr && root != nullptr && capacity >= PATH_MAX);
    char folder[PATH_MAX];
    if (realpath(from, folder) == nullptr) return false;
    struct stat info;
    bool is_folder = stat(folder, &info) == 0 && S_ISDIR(info.st_mode);
    for (size_t level = 0; level < smith_project_levels_max; level++) { // bounded by the depth of the path
        if (!is_folder || level > 0) {
            char *slash = strrchr(folder, '/');
            if (slash == nullptr || slash == folder) return false; // the top is never a project's root
            *slash = '\0';
        }
        is_folder = true;
        if (smith_project_marked(folder)) {
            snprintf(root, capacity, "%s", folder);
            return true;
        }
    }
    return false;
}
