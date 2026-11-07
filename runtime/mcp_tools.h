// Generated split of smith.c (#2685): what runtime/mcp_tools.c shares with the other files.
#pragma once

#include "core.h"
#include "mcp_text.h"
#include "mcp_unit.h"

typedef bool (*SmithMcpToolFunction)(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

const SmithJsonValue *smith_mcp_json_string(SmithArena *arena, struct smith_array_const_char_n text);

bool smith_mcp_tool_breakpoint(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

bool smith_mcp_tool_launch(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

bool smith_mcp_tool_continue(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

bool smith_mcp_tool_step_over(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

bool smith_mcp_tool_step_in(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

bool smith_mcp_tool_step_out(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

bool smith_mcp_tool_pause(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

bool smith_mcp_tool_stack(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

bool smith_mcp_tool_variables(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

bool smith_mcp_tool_evaluate(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

bool smith_mcp_tool_stop(SmithMcpServer *server, const SmithJsonValue *arguments, SmithMcpText *out);

typedef struct {
    const char *name;
    const char *description;
    const char *schema; // the tool's input, as JSON Schema
    bool read_only;
    SmithMcpToolFunction run;
} SmithMcpToolEntry;

#define SMITH_MCP_SESSION_PROPERTY \
    "\"session\":{\"type\":\"string\",\"description\":\"The session id (s1, s2...). Default: the latest.\"}"

#define SMITH_MCP_TIMEOUT_PROPERTY \
    "\"timeout_ms\":{\"type\":\"integer\",\"description\":\"How long to wait for the program to stop or end, in ms. " \
    "Default 30000.\"}"

extern const SmithMcpToolEntry smith_mcp_tool_table[11];

constexpr size_t smith_mcp_tool_count = sizeof smith_mcp_tool_table / sizeof smith_mcp_tool_table[0];

