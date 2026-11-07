// Generated split of smith.c (#2685): what runtime/mcp_dap.c shares with the other files.
#pragma once

#include "core.h"
#include "mcp_text.h"
#include "mcp_unit.h"
#ifdef SMITH_MCP
#include "dap.h"
#endif

void smith_mcp_input_submit(SmithMcpServer *server);

void smith_mcp_pump(SmithMcpServer *server, int64_t timeout_ns);

int64_t smith_mcp_dap_seq(SmithMcpSession *session);

SmithJsonWriter smith_mcp_dap_writer(SmithMcpSession *session);

bool smith_mcp_dap_transmit(SmithMcpServer *server, SmithMcpSession *session, SmithJsonWriter *writer);

typedef enum {
    SmithMcpReply_Ok,
    SmithMcpReply_Failed,  // the adapter answered success: false; message says why
    SmithMcpReply_Timeout, // no answer in time
    SmithMcpReply_Dead,    // the adapter is gone, or would not take the request
    SmithMcpReply_Bad,     // an answer that is not a DAP response
} SmithMcpReplyStatus;

typedef struct {
    SmithMcpReplyStatus status;
    SmithDapResponse response; // in the server's scratch arena: valid until the caller's scope ends
    char message[256];
} SmithMcpReply;

SmithMcpReply smith_mcp_dap_await(SmithMcpServer *server, SmithMcpSession *session, int64_t seq, int64_t timeout_ns);

SmithMcpReply smith_mcp_reply_dead(void);

// Sends a typed request: the macro stamps its seq and calls the type's own write. A function cannot, since the
// generated writers take their own types.
#define SMITH_MCP_DAP_SEND(server, session, name, request, sent) \
    do { \
        (request).seq = smith_mcp_dap_seq(session); \
        SmithJsonWriter writer_ = smith_mcp_dap_writer(session); \
        smith_dap_##name##_write(&writer_, &(request)); \
        (sent) = smith_mcp_dap_transmit((server), (session), &writer_); \
    } while (false)

// Sends a request and takes its reply.
#define SMITH_MCP_DAP_CALL(server, session, name, request, timeout_ns, reply) \
    do { \
        bool sent_ = false; \
        SMITH_MCP_DAP_SEND(server, session, name, request, sent_); \
        (reply) = \
            sent_ ? smith_mcp_dap_await((server), (session), (request).seq, (timeout_ns)) : smith_mcp_reply_dead(); \
    } while (false)

void smith_mcp_reply_error(const SmithMcpReply *reply, const char *what, SmithMcpText *out);

typedef enum {
    SmithMcpTake_Nothing,
    SmithMcpTake_Stopped,  // the program stopped: stop says where
    SmithMcpTake_Finished, // the program exited or the adapter ended
} SmithMcpTake;

SmithMcpTake smith_mcp_events_take(SmithMcpSession *session, SmithMcpText *notes, SmithMcpEvent *stop);

SmithMcpTake smith_mcp_wait_stop(SmithMcpServer *server, SmithMcpSession *session, int64_t timeout_ns,
                                 SmithMcpText *notes, SmithMcpEvent *stop);

bool smith_mcp_wait_initialized(SmithMcpServer *server, SmithMcpSession *session, int64_t launch_seq,
                                int64_t timeout_ns);

SmithMcpSession *smith_mcp_session_find(SmithMcpServer *server, const char *id);

SmithMcpSession *smith_mcp_session_latest(SmithMcpServer *server);

SmithMcpSession *smith_mcp_session_start(SmithMcpServer *server, SmithMcpText *out);

void smith_mcp_session_close(SmithMcpServer *server, SmithMcpSession *session, bool polite);
