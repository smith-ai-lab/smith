// Generated split of smith.c (#2685): what runtime/verbs.c shares with the other files.
#pragma once

#include "core.h"

bool smith_front_end_flag(const char *arg);

bool smith_lint_expand_dirs(int *argc, char ***argv);

int smith_lint_project(int count, char **args, const char *self);

int smith_prove_project(int count, char **args, const char *self);

int smith_test_project(int count, char **args, const char *self);

int smith_run_project(int count, char **args, const char *self);

int smith_check_project(int count, char **args, const char *self);

int smith_fmt_verb(int count, char **args, const char *self);

