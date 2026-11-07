// Generated split of smith.c (#2685): what runtime/make_sub.c shares with the other files.
#pragma once

#include "core.h"
#include "make.h"
#include "make_expand.h"
#include "make_graph.h"
#include "make_read.h"

constexpr size_t smith_make_subs_max = 32;

typedef struct {
    const char *path; // the folder, from the project's root
    const char **goals;
    size_t goal_count;
    const char *default_goal; // the goal of the folder's Makefile that no goal was given for, under the folder
} SmithMakeSub;

extern SmithMakeSub smith_make_subs[smith_make_subs_max];

extern size_t smith_make_sub_count;

extern SmithMake smith_make_saved_make;

extern SmithMakeReader smith_make_saved_reader;

extern SmithMakeGraph smith_make_saved_graph;

extern SmithMakeAuto smith_make_saved_auto;

const char *smith_make_under(const char *dir, const char *name);

typedef enum { SmithMakeCall_None, SmithMakeCall_Self, SmithMakeCall_Folder, SmithMakeCall_Other } SmithMakeCall;

SmithMakeCall smith_make_parse_make(const SmithMakeCommand *command, const char **folder, const char ***goals,
                                    size_t *count);

const char *smith_make_find_makefile(void);

SmithMakeSub *smith_make_sub_find(const char *path, const char *const *goals, size_t goal_count);

void smith_make_import_all(void);
