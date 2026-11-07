// Generated split of smith.c (#2685): what runtime/executor_plan.c shares with the other files.
#pragma once

#include "core.h"
#include "executor.h"

int64_t smith_plan_stat_path(const char *path);

uint32_t smith_plan_input_count(const SmithEdge *edge);

uint32_t smith_plan_input(const SmithExec *exec, const SmithEdge *edge, uint32_t k);

bool smith_plan_is_order_only(const SmithEdge *edge, uint32_t k);

uint32_t smith_plan_output(const SmithExec *exec, const SmithEdge *edge, uint32_t k);

void smith_plan_explain(const SmithExec *exec, const char *format, ...);

size_t smith_plan_targets(SmithExec *exec, uint32_t *targets);

void smith_plan_done(SmithExec *exec, uint32_t number);

void smith_plan_settle(SmithExec *exec);

void smith_plan_clean(SmithExec *exec, uint32_t first, uint32_t *stack);

bool smith_plan_build(SmithExec *exec, const uint32_t *targets, size_t count);

#ifdef SMITH_CORE
#include "smith_core_plan.h"

extern FILE *smith_plan_core_stream;

void smith_plan_core_command(SmithExec *exec, const SmithCorePlan *build, SmithEdge *edge);
#endif
