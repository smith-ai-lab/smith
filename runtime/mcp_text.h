// Generated split of smith.c (#2685): what runtime/mcp_text.c shares with the other files.
#pragma once

#include "core.h"

// A bounded text in the arena. Once it is full the rest is dropped and `truncated` says so, so a result is never
// longer than smith_mcp_text_max however much the program wrote.
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
    bool truncated;
    const char *tag; // the last output source named in the text, so that a run of one source is named once
} SmithMcpText;

SmithMcpText smith_mcp_text_make(SmithArena *arena, size_t capacity);

void smith_mcp_text_bytes(SmithMcpText *text, const char *bytes, size_t size);

void smith_mcp_text_add(SmithMcpText *text, const char *format, ...);

void smith_mcp_text_add_text(SmithMcpText *text, struct smith_array_const_char_n piece);

char *smith_mcp_cstring(SmithArena *arena, struct smith_array_const_char_n text);

void smith_mcp_copy(char *to, size_t capacity, struct smith_array_const_char_n from);

const SmithJsonValue *smith_mcp_argument(const SmithJsonValue *arguments, const char *name, SmithJsonKind kind);

const char *smith_mcp_argument_string(SmithArena *arena, const SmithJsonValue *arguments, const char *name);

int64_t smith_mcp_argument_integer(const SmithJsonValue *arguments, const char *name, int64_t fallback);

bool smith_mcp_argument_bool(const SmithJsonValue *arguments, const char *name, bool fallback);

int64_t smith_mcp_argument_timeout(const SmithJsonValue *arguments, int64_t fallback);
