// Generated split of smith.c (#2685): what runtime/executor_state.c shares with the other files.
#pragma once

#include "core.h"
#include "executor.h"

const SmithLogEntry *smith_state_log_find(SmithExec *exec, const char *path);

const SmithDepsEntry *smith_state_deps_find(SmithExec *exec, const char *path);

void smith_state_open(SmithExec *exec);

void smith_state_record(SmithExec *exec, const SmithEdge *edge, uint64_t start_ms, uint64_t end_ms,
                               int64_t mtime);

void smith_state_record_deps(SmithExec *exec, const SmithEdge *edge, int64_t mtime, const char *const *deps,
                                    uint32_t count);

