// What the executor (runtime/executor_plan.c) finds out of date, in process (issue #646), through `smith_core_plan`
// (smith.c compiled as a library by tests/units/build.c, as include/smith_core.h). No build.c is compiled or run and no
// command starts: the manifest is a text the test writes, the files have the times the test gives them, and a "run" is
// a fake that makes the outputs its command names, in the order the plan gives, and records them in the logs as the
// executor does. A call is a `smith build`: its plan, its reasons (`-d explain`) and what it ran. Safe to call again in
// the same process: each call reads the manifest and the logs of the folder afresh.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// One build of the manifest in `folder`.
typedef struct {
    const char *folder;           // the project's folder
    const char *manifest;         // the manifest's name in it, usually ".smith/build.ninja"
    const char *const *targets;   // the names to build, ended by nullptr, or nullptr for the manifest's defaults
    const char *const *unchanged; // outputs the fake command leaves as they are when they exist, ended by nullptr
    const char *const *depends;   // "output: dep dep" for each output with a depfile, ended by nullptr
    int64_t time;                 // the second the fake commands run at: the time of what they make and log
} SmithCorePlan;

// Writes `name` (relative to the folder, its folders made) with `text` and gives it the modification time `time`
// (seconds). False when it cannot be written.
bool smith_core_plan_write(const char *folder, const char *name, const char *text, int64_t time);

// Plans and fake-runs the build. `output` has, NUL-terminated (cut short when it does not fit), the reason of each
// output found out of date (`smith explain: ...`) as the plan makes them and one `ran <first output>` line for each
// command run, in order; or `no work to do` when the plan holds none. `ran` is how many commands ran. False when the
// folder cannot be entered or a buffer cannot be made. A manifest smith refuses ends the process.
bool smith_core_plan(const SmithCorePlan *build, char *output, size_t output_size, size_t *ran);
