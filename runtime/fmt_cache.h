// What runtime/fmt_cache.c, the cache of `smith fmt` (#3081), shares with runtime/verbs.c.
#pragma once

#include "core.h"

bool smith_fc_open(void);

bool smith_fc_formatted(const char *file);

void smith_fc_keep(const char *file);
