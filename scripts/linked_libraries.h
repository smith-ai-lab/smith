// What smith links of its own libraries, in one place: build.c (the `smith build` of every platform) and
// scripts/seed_linux.c (the Linux seed, which has no smith to build with) both include this file, so the two lists
// cannot drift (issue #240).
#pragma once

// The .smith modules smith links, compiled by the smith that builds it (smith.c and the runtime see only their
// headers). mem and math are here for json, which takes its tree from the allocator in force (smith::mem).
static const char *const smith_linked_libraries[] = {"arena", "contract", "inflate", "io",
                                                     "math",  "mem",      "random",  "text"};

// The handlers runtime that smith links is its tables only (lib/handlers.c with this flag): smith::mem asks the table
// of the thread for the allocator itself, so smith's own malloc, LLVM's included, stays the system's.
static const char *const smith_linked_handlers_source = "lib/handlers.c";
static const char *const smith_linked_handlers_flag = "-DSMITH_HANDLERS_TABLES_ONLY";
