// The checker of `smith docs` in process (issue #712, part of #646): compiled into the units runner with smith.c
// -DSMITH_CORE. Safe to call again in the same process. Not shipped to users.
#pragma once
#include <stdbool.h>
#include <stddef.h>

// The check of the project at `folder` (its knowledge/ and .agents/skills/, with the types knowledge/index.md
// declares): the problems, one `path: message` a line in the order of the rules, then the summary line `smith docs: N
// concepts, M problems`, all in `out`, NUL-terminated. False when the buffer is too small or cannot be made. A
// check that runs out of memory ends the process, as the command does.
bool smith_core_docs_check(const char *folder, char *out, size_t out_size);
