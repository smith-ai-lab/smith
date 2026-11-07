// Generated split of smith.c (#2685): what runtime/mcp.c shares with the other files.
#pragma once

#include "core.h"

#if !defined(SMITH_ASSERT)
#define SMITH_ASSERT(condition) assert(condition)
#endif

constexpr size_t smith_mcp_sessions_max = 4;

constexpr size_t smith_mcp_events_max = 64;

constexpr size_t smith_mcp_responses_max = 6;

constexpr size_t smith_mcp_breakpoints_max = 64;

constexpr size_t smith_mcp_frame_max = (size_t)256 << 10; // one DAP message

constexpr size_t smith_mcp_line_max = (size_t)1 << 20;    // one MCP message

constexpr size_t smith_mcp_text_max = (size_t)96 << 10;   // one tool result, in characters

constexpr size_t smith_mcp_scratch = (size_t)256 << 20;   // address space for the messages of one call

constexpr size_t smith_mcp_turns_max = (size_t)1 << 40;   // a bound for loops that end by their own condition

constexpr int64_t smith_mcp_second = 1'000'000'000;

constexpr int64_t smith_mcp_request_timeout = 15 * smith_mcp_second; // a DAP request that does not resume

constexpr int64_t smith_mcp_start_timeout = 60 * smith_mcp_second;   // starting `smith dap`, the first time included

constexpr int64_t smith_mcp_resume_timeout = 30 * smith_mcp_second;  // a resuming call, when the caller names none

// A message of the adapter that is not a response to a request: what the next call reports. Output is kept as text, a
// stop as its reason and place; nothing else of an event is.
typedef enum {
    SmithMcpEvent_Initialized,
    SmithMcpEvent_Stopped,
    SmithMcpEvent_Output,
    SmithMcpEvent_Exited,
    SmithMcpEvent_Terminated,
} SmithMcpEventKind;

typedef struct {
    SmithMcpEventKind kind;
    int64_t thread;     // Stopped
    int64_t breakpoint; // Stopped: the id of the breakpoint hit, or 0
    int64_t code;       // Exited
    char reason[48];    // Stopped
    char category[16];  // Output: stdout, stderr, console...
    char text[1024];    // Stopped: the adapter's description; Output: what was written
    size_t size;        // Output: bytes in text
} SmithMcpEvent;

// A response of the adapter, kept as the bytes it came in until the call that sent the request asks for it. (The
// adapter answers in the order it likes, and a script of the fake io answers before it is asked.)
typedef struct {
    bool used;
    int64_t request_seq;
    size_t size;
    unsigned char frame[smith_mcp_frame_max];
} SmithMcpResponse;

typedef struct {
    char path[512];
    int64_t line;
    char condition[256];
    int64_t id;
    bool verified;
    char message[128]; // why it is not verified
} SmithMcpBreakpoint;

// One debug session: a `smith dap` child. The struct is big (its buffers are in it) and lives in the arena.
typedef struct {
    bool used;
    char id[8];
    SmithChild child;
    SmithOp read, write, wait;
    bool read_pending, write_pending, wait_pending;
    size_t skip;       // bytes of an oversize message still to drop
    size_t serial;     // which session this is, counting from 1
    bool input_closed; // the child's stdin is closed
    bool output_ended; // the child closed its stdout
    bool process_gone; // the child exited
    unsigned char chunk[16384];
    unsigned char input[smith_mcp_frame_max + 16384]; // bytes read and not yet a whole message
    size_t input_size;
    unsigned char frame_out[smith_mcp_frame_max + 64]; // the message being written, header first
    int64_t next_seq;
    SmithMcpResponse responses[smith_mcp_responses_max];
    SmithMcpEvent events[smith_mcp_events_max];
    size_t event_first, event_count, events_dropped;
    size_t responses_dropped; // responses that found the table full and replaced the first
    bool stopped;             // the program is stopped (the last stop event was taken, and it has not resumed)
    bool finished;            // the program has exited or the adapter has terminated
    bool has_exit_code;
    int64_t exit_code;
    int64_t thread; // the thread of the last stop
    SmithMcpBreakpoint breakpoints[smith_mcp_breakpoints_max];
    size_t breakpoint_count;
    char cwd[PATH_MAX];
    char program[PATH_MAX];
} SmithMcpSession;

typedef struct {
    SmithIo *io;
    const char *const *dap_argv; // the adapter: smith dap
    SmithArena scratch;          // the messages and results of one call (a scope per call)
    SmithArena input_arena;      // what the console's reads land in
    SmithOp input_op;
    bool input_pending;
    bool input_end;
    bool overlong;      // dropping a message that has no end within smith_mcp_line_max
    bool overlong_seen; // one was dropped: the client is told
    char *line;         // stdin's bytes not yet a whole message
    size_t line_size;
    char *request; // the message being handled, copied out of line
    SmithMcpSession *sessions;
    size_t sessions_made; // how many sessions were ever started: the ids
    char cwd[PATH_MAX];
} SmithMcpServer;

