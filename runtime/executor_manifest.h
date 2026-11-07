// Generated split of smith.c (#2685): what runtime/executor_manifest.c shares with the other files.
#pragma once

#include "core.h"
#include "executor.h"

uint32_t smith_exec_node(SmithExec *exec, const char *path, size_t length, bool create, bool *created);

void smith_manifest_read(SmithExec *exec);

