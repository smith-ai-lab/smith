// Generated split of smith.c (#2685): what runtime/executor_cache.c shares with the other files.
#pragma once

#include "core.h"
#include "executor.h"

bool smith_stepcache_restore(SmithExec *exec, uint32_t number, SmithStepKey *key, int64_t now);

uint32_t smith_stepcache_graph_deps(SmithExec *exec, const SmithEdge *edge, const char *const *deps, uint32_t count,
                                    const char ***kept);

void smith_stepcache_store(SmithExec *exec, SmithEdge *edge, const SmithJob *job, int64_t now);

void smith_stepcache_prune(SmithExec *exec);
