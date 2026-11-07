// Generated split of smith.c (#2685): what runtime/make_print.c shares with the other files.
#pragma once

#include "core.h"
#include "make_class.h"
#include "make_graph.h"

constexpr size_t smith_make_comment_width = 112;

const char *smith_make_summary(const char *text, size_t width);

void smith_make_emit_node(const SmithMakeNode *node, const SmithMakeInfo *info);
