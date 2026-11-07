// Generated split of smith.c (#2685): what runtime/lint_cache.c shares with the other files.
#pragma once

#include "core.h"

void smith_lc_plan(size_t number, const char *file, const char *flags);

bool smith_lc_replay(size_t number, int *status);

size_t smith_lc_extra(size_t number, char **extra);

void smith_lc_store(size_t number, int wait_status, FILE *log);

bool smith_lc_open(void);
