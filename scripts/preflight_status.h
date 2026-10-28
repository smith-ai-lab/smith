// The text of the `smith/preflight` status (issue #638), apart from build.c so that a test can run it: a plain function
// of what the steps did, with no smith, no git and no gh. `build_preflight` in build.c calls it.
#pragma once

#include <stdio.h>
#include <string.h>

constexpr size_t preflight_steps_max = 8;

typedef struct {
    const char *failed; // " lint test" (the failed steps), "" when none
    bool test_capped;   // the test step reached its cap: the run was cut short
    long ran;           // selected tests that ran, -1 when not known
    long total;         // tests selected, -1 when not known
    size_t step_count;  // the steps below
    const char *step_names[preflight_steps_max];
    double step_seconds[preflight_steps_max];
} PreflightResult;

// A duration as the status shows it: 41s, 3m.
static void preflight_duration(char *out, size_t size, double seconds) {
    if (seconds >= 120)
        snprintf(out, size, "%.0fm", seconds / 60);
    else
        snprintf(out, size, "%.0fs", seconds);
}

// "tests 10m, build 2m": the two slowest steps, slowest first.
static void preflight_slowest(const PreflightResult *result, char *out, size_t size) {
    out[0] = '\0';
    bool taken[preflight_steps_max] = {};
    for (size_t round = 0; round < 2 && round < result->step_count; round++) { // two rounds
        size_t best = preflight_steps_max;
        for (size_t i = 0; i < result->step_count; i++) { // bounded by the steps
            if (!taken[i] && (best == preflight_steps_max || result->step_seconds[i] > result->step_seconds[best]))
                best = i;
        }
        taken[best] = true;
        char duration[16];
        preflight_duration(duration, sizeof duration, result->step_seconds[best]);
        size_t used = strlen(out);
        snprintf(out + used, size - used, "%s%s %s", round == 0 ? "" : ", ", result->step_names[best], duration);
    }
}

// "1200 of 2054 tests ran": how many of the selected tests ran, "?" for a number not known.
static void preflight_ran(const PreflightResult *result, char *out, size_t size) {
    char ran[24] = "?";
    char total[24] = "?";
    if (result->ran >= 0) snprintf(ran, sizeof ran, "%ld", result->ran);
    if (result->total >= 0) snprintf(total, sizeof total, "%ld", result->total);
    snprintf(out, size, "%s of %s tests ran", ran, total);
}

// Whether the commit passed. A run the cap cut short never passes: the tests it did not run are unchecked.
static bool preflight_ok(const PreflightResult *result) {
    return result->failed[0] == '\0' && !result->test_capped;
}

// The status description, under GitHub's 140 characters: the verdict, the times and how many selected tests ran.
static void preflight_description(const PreflightResult *result, const char *times, char *out, size_t size) {
    char counts[64];
    preflight_ran(result, counts, sizeof counts);
    if (result->test_capped && result->failed[0] == '\0') {
        char slowest[64];
        preflight_slowest(result, slowest, sizeof slowest);
        snprintf(out, size, "preflight exceeded its budget: %s (%s)", slowest, counts);
    } else if (result->failed[0] != '\0') {
        snprintf(out, size, "failed:%s (%s; %s)", result->failed, times, counts);
    } else {
        snprintf(out, size, "ok: %s (%s)", times, counts);
    }
    if (size > 140) out[140] = '\0';
}
