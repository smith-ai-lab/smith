// Generated split of smith.c (#2685): what runtime/slots.c shares with the other files.
#pragma once

#include "core.h"

constexpr int smith_slots_denied = -1;    // smith_slots_extra: no token is free for another worker

constexpr int smith_slots_unlimited = -2; // smith_slots_extra: there is no pool, the worker needs none

int smith_slots_cores(void);

void smith_slots_take(const char *verb);

int smith_slots_extra(void);

void smith_slots_give(int token);

void smith_slots_arm(const char *verb);

void smith_slots_need(void);

bool smith_slots_verb(const char *name);

int smith_slots_main(int count, char **args);

