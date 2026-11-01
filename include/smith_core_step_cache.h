// The shared cache of compile steps (runtime/executor_cache.c, #621), in process (issue #646), through
// `smith_core_step_cache` (smith.c compiled as a library by tests/units/build.c, as include/smith_core.h). Like
// `smith_core_plan` (include/smith_core_plan.h), whose struct it takes: the manifest is a text the test writes, the
// files have the times the test gives them, and a "run" is a fake that makes the outputs its command names. The compile
// commands must look like clang's (`cc -MD -MF <depfile> -c a.c -o a.o`, a program that exists on PATH) for the cache
// to serve them; the fake writes the depfile from `plan.depends`. The cache lives in `cache` (its `steps` folder), so
// folders built one after another in one process share it. Safe to call again in the same process: each call resets
// the cache's state (its memo, its roots) and reads the manifest and the logs of the folder afresh.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "smith_core_plan.h"

typedef struct {
    SmithCorePlan plan;
    const char *cache; // the folder that holds the cache (it makes `steps` in it), or nullptr for the cache turned off
} SmithCoreStepCache;

// Plans and fake-runs the build with the cache on. `output` has the reason of each output found out of date
// (`smith explain: ...`, the cache's `restored from the step cache` among them) and one line for each step in order:
// `ran <first output>` for a command run (its result is stored) or `served <first output>` for one the cache served;
// or `no work to do`. `ran` and `served` count them. False when the folder cannot be entered, a buffer cannot be made
// or the cache cannot be opened. A manifest smith refuses ends the process.
bool smith_core_step_cache(const SmithCoreStepCache *build, char *output, size_t output_size, size_t *ran,
                           size_t *served);

// The check a build starts with (at most once an hour, stamped in the cache): results unused for 31 days go, and the
// least recently used while the results are over `cap` bytes, at the time `now` (seconds). False when the cache
// cannot be opened.
bool smith_core_step_cache_prune(const char *cache, uint64_t cap, int64_t now);
