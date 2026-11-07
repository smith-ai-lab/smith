// Generated split of smith.c (#2685): what runtime/account.c shares with the other files.
#pragma once

#include "core.h"

void smith_account_start(void);

uint64_t smith_account_ms(struct timeval time);

uint64_t smith_account_now_ms(void);

int smith_account_main(int count, char **args);

void smith_account_report(const char *path, const char *what, bool all);
