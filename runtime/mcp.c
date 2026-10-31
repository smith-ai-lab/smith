// `smith mcp`: an MCP server over stdio that gives an agent a debugger (issue #22). It holds debug sessions, each a
// `smith dap` child process (lldb-dap) that it speaks the Debug Adapter Protocol to, and exposes them as tools:
// launch, breakpoint, continue, step_over, step_in, step_out, pause, stack, variables, evaluate and stop. Results are
// compact text an agent reads easily; the program's events (stopped, output, exited) are queued and come back with
// the next call. The design, the tools and what each returns are in knowledge/mcp-server.md and the smith-debug skill.
//
// Everything runs on <runtime/io.h>: the server's own stdin, every session's pipes and exit, and the clock are ops on
// one event loop in one thread, and nothing is allocated but in arenas. A tool call is a function that runs the loop
// (smith_mcp_pump) until what it waits for has happened or its time is up; while it waits, input that arrives is
// buffered, so a client that pipelines requests is answered in order.
//
// Neither protocol is written by hand: every message is a type of the code generated from the official schemas
// (.smith/protocol/dap.[ch] and mcp.[ch], knowledge/protocol-generation.md). What is read from a tree by name is what
// the schemas do not have: the `initialize` request of the MCP revisions before 2026-07-28, and a tool's arguments,
// which are the tool's own schema.
//
// The files, in the order they are included:
//   mcp_text.c    text a result is built in, and the readers of a tool's arguments
//   mcp_dap.c     a session: the child, DAP's framing, requests and their replies, the queue of events
//   mcp_show.c    what a stop, a stack, variables and an evaluation look like as text
//   mcp_tools.c   the tools, their schemas, and the table that finds them
//   mcp_serve.c   MCP: the messages, the version negotiation, and the loop
//
// It is included by smith.c after lib/text.c, lib/json.c and the protocol code, and compiled alone, with those linked,
// by the tests of tests/mcp_test.smith.
#include "mcp.h"
#include <runtime/io.h>
#include <smith/json.h>
#include <smith/mem.h>
#include "dap.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef SMITH_ASSERT
#include <assert.h>
#define SMITH_ASSERT(condition) assert(condition)
#endif

constexpr size_t smith_mcp_sessions_max = 4;
constexpr size_t smith_mcp_events_max = 64;
constexpr size_t smith_mcp_responses_max = 6;
constexpr size_t smith_mcp_breakpoints_max = 64;
constexpr size_t smith_mcp_frame_max = (size_t)256 << 10; // one DAP message
constexpr size_t smith_mcp_line_max = (size_t)1 << 20;    // one MCP message
constexpr size_t smith_mcp_text_max = (size_t)96 << 10;   // one tool result, in characters
constexpr size_t smith_mcp_output_max = 6144;             // the program's output a call reports
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
    bool stopped;  // the program is stopped (the last stop event was taken, and it has not resumed)
    bool finished; // the program has exited or the adapter has terminated
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

// One block each: clang-format sorts a block, and these depend on the order.
#include "mcp_text.c"

#include "mcp_dap.c"

#include "mcp_show.c"

#include "mcp_tools.c"

#include "mcp_serve.c"
