// `smith new` in process (issue #968, part of #646), through `smith_core_new` (smith.c compiled as a library by
// tests/units/build.c, as include/smith_core.h). It writes the files and makes the commit as the command does, and
// prints what the command prints, but makes no first build: a test that needs the built project runs the command.
// Safe to call again in the same process: it runs in a child of the caller, so the folder it enters and the process it
// ends (`smith_die`) are the child's. Git is the caller's environment (HOME, GIT_* variables). Not shipped to users.
#pragma once
#include <stddef.h>

// `smith new [--language <language>] [<name>]` run in the existing `folder`: `name` and `language` are nullptr for
// none (the folder's own name; smith). `out` gets standard output and standard error together, NUL-terminated and cut
// short when it does not fit. `environment` is "NAME=value" to set and "-NAME" to unset in the child (git's HOME and
// identity), ended by nullptr, or nullptr; the caller's own is never changed (the test runner frees what a test
// allocated, which a process-wide environment would point into). Returns the exit status of the command (0, or 2 for a
// refusal), or -1 when the child cannot be made.
int smith_core_new(const char *folder, const char *name, const char *language, const char *const *environment,
                   char *out, size_t out_size);
