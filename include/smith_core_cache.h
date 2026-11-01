// `smith cache` in process (issue #1297, part of #646), through `smith_core_cache` (runtime/cache.c in smith.c compiled
// as a library by tests/units/build.c, as include/smith_core.h). What `smith cache [-v]` and `smith cache clean
// [modules|packages|lint|steps]` do to a cache folder: the listing, the usage errors and what a clean removes. The
// pruning rule is the package fetch's (include/smith_core_package.h). Safe to call again in the same process: it
// returns the status where the command ends the process, and it gives the clean's lock back. Not shipped.
#pragma once
#include <stddef.h>

// Runs `smith cache` with the `count` arguments `args` against the cache `xdg_cache_home`/smith (or `home`/.cache/smith
// when `xdg_cache_home` is nullptr or empty, as $XDG_CACHE_HOME and $HOME say it for the command; both nullptr read the
// process's own variables, which a test does not set). The text the command prints, then the message it would end the
// process with, go to `out` (`out_size` bytes, NUL-terminated and cut short when it does not fit). The result is the
// command's status: 0, or 2 for a usage error or a clean while a build holds the caches.
int smith_core_cache(const char *xdg_cache_home, const char *home, int count, char **args, char *out, size_t out_size);
