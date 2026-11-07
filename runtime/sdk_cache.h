// What runtime/sdk_cache.c shares with smith.c (#2685): the remembered SDK path, on macOS.
#pragma once

#include "core.h"

bool smith_sdk_cache_get(const char *xcrun, char *out);

void smith_sdk_cache_put(const char *xcrun, const char *sdk);

// smith.c: puts the SDK path in the environment the compiles inherit.
void smith_sdk_environment(const char *self);
