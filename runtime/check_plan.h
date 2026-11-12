// What `smith check --plan` reads (#3163): the scope of a change, the one rule CI's `changes` job and the pre-push hook
// share.
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum { SmithCheckScope_None, SmithCheckScope_Docs, SmithCheckScope_Full } SmithCheckScope;

typedef struct {
    size_t changed;  // the paths of the diff
    size_t built;    // those some build or check reads
    size_t code;     // those that are not documentation
    bool docs_tests; // a path README.md and the skills' tests read
} SmithCheckPlan;

// Counts one changed path into the plan.
void smith_check_plan_add(SmithCheckPlan *plan, const char *path);

// none: only documents no build reads. docs: also documentation the docs check reads. full: anything else.
SmithCheckScope smith_check_plan_scope(const SmithCheckPlan *plan);

const char *smith_check_scope_name(SmithCheckScope scope);

// Reads the plan of the diff from `base` (default: the merge-base with origin/main) to HEAD. False when git cannot say.
bool smith_check_plan_read(const char *base, SmithCheckPlan *plan);

// `smith check --plan [--base <ref>] [--event <name>]`: prints what runs for the diff as `key=value` lines: `scope=`
// (none, docs, full, or release for a push to main), `docs_tests=`, `docs_push=`, `mac_run=`, `changed=`, and the shape
// of the tests
// (`test_parts=`, `linux_flags=`, `mac_flags=`). `event` is the GitHub event ("" for a local run); the rules per event
// are here and nowhere else. A diff that cannot be read is full (release for a push). Returns the exit status.
int smith_check_plan_print(const char *base, const char *event);
