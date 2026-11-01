// The verb names a build file may not give a task or an alias (runtime/build_runtime.c, issue #1300, part of #646), in
// process through `smith_core_verb_name_refused` (build_runtime.c compiled with -DSMITH_BUILD_CORE by
// tests/units/build.c: no `main`, and the build file's `build` is never called). Not shipped to users. Safe to call
// again in the same process: each call starts from no recorded mistake.
#pragma once
#include <stddef.h>

// True when `smith_task` (`what` "task") or `smith_alias` (`what` "alias") refuses `name` because it is a verb's, and
// then `message` has the mistake as the build prints it, without the `smith: build.c: ` prefix (which names the build
// file of the folder the process is in), NUL-terminated and cut short when it does not fit. False, with `message`
// empty, for a name that is no verb's.
bool smith_core_verb_name_refused(const char *what, const char *name, char *message, size_t message_size);
