// Generated split of smith.c (#2685): what runtime/executor_cache.c shares with the other files.
#pragma once

#include "core.h"
#include "executor.h"

bool smith_stepcache_restore(SmithExec *exec, uint32_t number, SmithStepKey *key, int64_t now);

uint32_t smith_stepcache_graph_deps(SmithExec *exec, const SmithEdge *edge, const char *const *deps, uint32_t count,
                                    const char ***kept);

void smith_stepcache_store(SmithExec *exec, SmithEdge *edge, const SmithJob *job, int64_t now);

void smith_stepcache_prune(SmithExec *exec);

bool smith_stepcache_answer_get(const char *hex, char *out, size_t capacity, size_t *size);

void smith_stepcache_answer_put(const char *hex, const char *bytes, size_t size);

bool smith_stepcache_answer_file(const char *hex, char *path);

void smith_stepcache_answer_put_parts(const char *hex, const char *head, size_t head_size, const char *body,
                                      size_t body_size);
