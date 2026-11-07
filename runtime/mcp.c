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
// It is linked into smith, lib/json.c and the protocol code, and compiled alone, with those linked,
// by the tests of tests/mcp_test.smith.
#include "core.h"
#include "mcp_unit.h"

#include <runtime/io.h>
#include <smith/json.h>
#include <smith/mem.h>
#include "dap.h"
#include "mcp.h"

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
#endif

// One block each: clang-format sorts a block, and these depend on the order.
