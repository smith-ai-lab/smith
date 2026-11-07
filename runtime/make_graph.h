// Generated split of smith.c (#2685): what runtime/make_graph.c shares with the other files.
#pragma once

#include "core.h"
#include "make.h"

typedef struct {
    const char *text; // expanded, without the leading @, - and +
    bool silent;
    bool ignore;
    SmithMakeWhere where;
} SmithMakeCommand;

typedef struct {
    const char *name;
    SmithMakeWhere where;
    const char **prerequisites;
    size_t prerequisite_count;
    const char **order_only;
    size_t order_count;
    const char *stem;
    int64_t rule; // the rule whose recipe it runs, or -1
    const char *guard;
    bool phony;
    bool has_recipe;
    bool built_in;   // its recipe is make's own
    const char *cwd; // the folder its commands run in, when it comes from the Makefile of another folder
    SmithMakeCommand *commands;
    size_t command_count;
    bool expanded;
} SmithMakeNode;

typedef struct {
    SmithMakeNode *nodes;
    size_t node_count;
    uint32_t *slots;
    size_t builtin_first; // the first built-in pattern rule
    const char **missing;
    size_t missing_count;
    const char **listed;    // the names in the folder, for smith_make_exists_here
    uint32_t *listed_slots; // an open-addressed table of indexes into `listed`, plus one
    size_t listed_mask;
    int listing; // 0 not read yet, 1 usable, 2 not used
} SmithMakeGraph;

extern SmithMakeGraph smith_make_graph;

SmithMakeNode *smith_make_node_find(const char *name);

SmithMakeNode *smith_make_node_new(const char *name);

void smith_make_append(const char ***list, size_t *count, const char *const *more, size_t more_count);

void smith_make_graph_build(void);

void smith_make_node_expand(SmithMakeNode *node);

