// What shim/lldb_explain.cpp gives runtime/explain.c: a stopped process read through lldb. Plain C, included by both.
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char function[256]; // as written: `geometry::box::area`, `smith_check`
    char file[1024];    // "" when the frame has no line
    unsigned line;
} SmithExplainFrame;

typedef enum {
    SmithExplain_Ok,
    SmithExplain_Missing, // no such variable, member or index: not a value
    SmithExplain_Optimized,
    SmithExplain_Unreadable,
} SmithExplainState;

// Attaches to pid and picks the thread that holds smith's failure. False, with the reason in problem, when it cannot.
bool smith_explain_attach(long pid, const char *self, char *problem, size_t capacity);
// The frames of that thread, innermost first; returns how many.
size_t smith_explain_frames(SmithExplainFrame *out, size_t capacity);
// Reads the variable path (`a.b`, `p->q[1]`) in the frame as `frame variable` does. out gets the value as text and
// integer, when not null, its value as a signed integer.
SmithExplainState smith_explain_read(size_t frame, const char *path, char *out, size_t capacity, long long *integer);
// Lets the process run on.
void smith_explain_detach(void);
