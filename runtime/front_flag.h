// Generated split of smith.c (#2685): what runtime/front_flag.c shares with the other files.
#pragma once

#include "core.h"

#if !defined(SMITH_CORE)
constexpr size_t smith_front_args_max = 1024;
#endif

#if !defined(SMITH_CORE)
extern char *smith_front_arguments[smith_front_args_max + 8];
#endif

#if !defined(SMITH_CORE)
bool smith_front_requested(int count, char **args);
#endif

#if !defined(SMITH_CORE)
int smith_front_rewrite(char **argv, int first, int count, char **args, const char *self);
#endif

bool smith_lint_fix_requested(int count, char **args);

int smith_lint_fix(int count, char **args, const char *self);

#if !defined(SMITH_CORE)
int smith_fmt_front(int *count, char **args, bool *only);
#endif

