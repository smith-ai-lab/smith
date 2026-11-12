// What runtime/fmt_cache.c, the format answers of the one store (#3081, #3177), shares with runtime/verbs.c.
#pragma once

#include "core.h"

bool smith_fc_open(void);

bool smith_fc_formatted(const char *file);

void smith_fc_keep(const char *file);
