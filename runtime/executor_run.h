// Generated split of smith.c (#2685): what runtime/executor_run.c shares with the other files.
#pragma once

#include "core.h"
#include "executor.h"

void smith_run_watch_signals(void);

void smith_run_printer_open(SmithExec *exec);

bool smith_run_take_depfile(SmithExec *exec, const SmithEdge *edge, SmithJob *job);

int64_t smith_run_wall_ns(void);

int64_t smith_run_restat(SmithExec *exec, SmithEdge *edge, int64_t started, uint32_t *stack);

void smith_run_make_parents(const char *path);

int smith_run_plan(SmithExec *exec);

int smith_run_targets(SmithExec *exec, const uint32_t *targets, size_t count, bool say_done);

uint32_t smith_run_manifest_node(SmithExec *exec);
