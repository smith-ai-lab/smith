// Generated split of smith.c (#2685): what runtime/mcp_show.c shares with the other files.
#pragma once

#include "core.h"
#include "mcp_text.h"
#include "mcp_unit.h"
#ifdef SMITH_MCP
#include "dap.h"
#endif

constexpr size_t smith_mcp_depth_max = 6; // how deep `variables` may expand

void smith_mcp_path_add(SmithMcpText *out, const SmithMcpSession *session, struct smith_array_const_char_n path);

void smith_mcp_frame_add(SmithMcpText *out, const SmithMcpSession *session, size_t index,
                         const SmithDapStackFrame *frame);

bool smith_mcp_stack_get(SmithMcpServer *server, SmithMcpSession *session, int64_t start, int64_t levels,
                         SmithDapStackTraceResponseBody *body, SmithMcpText *out);

void smith_mcp_show_stop(SmithMcpServer *server, SmithMcpSession *session, const SmithMcpEvent *stop,
                         SmithMcpText *out);

void smith_mcp_show_finished(const SmithMcpSession *session, SmithMcpText *out);

void smith_mcp_show_variables(SmithMcpServer *server, SmithMcpSession *session, int64_t reference, int64_t depth,
                              int64_t max, SmithMcpText *out);
