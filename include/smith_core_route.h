// The routing of the compiler verbs and links (`smith cc`, `smith c++`, smith-cc, smith-c++, clang, clang++), the tools
// (fmt, lint, lsp, dbg, dap) and the bare arguments, in process (issue #716, part of #646), through `smith_core_route`
// (smith.c compiled as a library by tests/units/build.c, as include/smith_core.h). It runs what `main` runs after it
// knows its own path, and stops where smith would start a program: the program and the arguments it would run are the
// result, so no compiler and no stub is started. A call is a command: the name smith is run as and its arguments.
// Safe to call again in the same process: the route runs in a child of the caller, so the environment it sets, the
// folder it enters and the process it ends (`smith_die`) are the child's.
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *folder;             // the folder the command runs in, or nullptr for the caller's
    const char *const *arguments;   // argv, ended by nullptr: arguments[0] is the name smith is run as ("smith")
    const char *self;               // the path of the smith program, which a compiler may not be a link to
    const char *const *environment; // "NAME=value" to set, "-NAME" to unset, ended by nullptr, or nullptr
    const char *const *show;        // names of variables to report in `exec` as `env NAME=value`, or nullptr
} SmithCoreRoute;

typedef struct {
    char exec[16384]; // what would run: `exec <program>` or `in-process <program>`, then `[<argument>]` on a line for
                      // each argument after the first, then the `env` lines of `show`; empty when nothing would run
    char out[4096];   // what smith printed to standard output (`--smith-which`, the usage)
    char err[4096];   // and to standard error (the refusals)
    int status;       // the exit status of the route: 0 when a program would run
} SmithCoreRouted;

// False when the child cannot be made or an output does not fit.
bool smith_core_route(const SmithCoreRoute *route, SmithCoreRouted *routed);
