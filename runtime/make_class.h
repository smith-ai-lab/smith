// Generated split of smith.c (#2685): what runtime/make_class.c shares with the other files.
#pragma once

#include "core.h"
#include "make_graph.h"

typedef enum {
    SmithMakeKind_Drop,
    SmithMakeKind_Alias,
    SmithMakeKind_Object,
    SmithMakeKind_Program,
    SmithMakeKind_Step,
    SmithMakeKind_Task,
} SmithMakeKind;

typedef struct {
    SmithMakeKind kind;
    const char *c_name;    // object_3, program_1, step_7
    const char *emit_name; // the name of a task or an alias in build.c
    size_t *kept;          // the commands that do something
    size_t kept_count;
    const char **prerequisites; // without FORCE, with the goals of a forwarded $(MAKE)
    size_t prerequisite_count;
    bool always;
    bool test;
    bool directory; // made by `mkdir -p`: smith makes the folder of an output itself
    const char *note;
} SmithMakeInfo;

extern SmithMakeInfo *smith_make_infos;

extern size_t smith_make_object_count;

extern size_t smith_make_program_count;

extern size_t smith_make_step_count;

SmithMakeInfo *smith_make_info_of(const SmithMakeNode *node);

void smith_make_classify(void);

constexpr size_t smith_make_inputs_max = 65536;

const char **smith_make_inputs(const char *const *names, size_t count, size_t *out_count);
