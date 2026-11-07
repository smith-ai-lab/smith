// Generated split of smith.c (#2685): what runtime/make_cc.c shares with the other files.
#pragma once

#include "core.h"
#include "make_emit.h"

typedef struct {
    const char *compiler;
    size_t *others; // flags, libraries and other words that are not the compile's own, in order
    size_t other_count;
    size_t *objects;
    size_t object_count;
    int64_t source;
    int64_t output;
    bool has_c;
    bool dropped_dependency_flags;
    size_t source_count;
} SmithMakeCc;

const char *smith_make_basename(const char *path);

const char *smith_make_compiler_name(const char *text);

bool smith_make_parse_cc(const SmithMakeArgv *argv, SmithMakeCc *cc);

bool smith_make_is_compile(const SmithMakeCc *cc);

bool smith_make_is_link(const SmithMakeCc *cc);
