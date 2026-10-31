---
type: architecture
title: smith mcp
description: smith mcp is the debugger as an MCP server over stdio, on the fake-able io and the generated protocol types, with eleven tools.
tags: [mcp, debugger]
---
# smith mcp

**`smith mcp` is the debugger as an MCP server over stdio** (issue #22; `runtime/mcp*.c`, [smith mcp](mcp-server.md), skill smith-debug): eleven tools
(`launch`, `breakpoint`, `continue`, the steps, `pause`, `stack`, `variables`, `evaluate`, `stop`) over `smith dap` children, on smith's private io layer (`runtime/io.smith`, `<runtime/io.h>`, until #888) and the
generated protocol types, no malloc. Only the full smith has it (`-DSMITH_MCP`); the smith plugin declares it (`mcpServers`). `tests/mcp_test.smith` runs it on the
fake io and end to end.

`smith mcp` is a Model Context Protocol server over stdio that gives an agent a debugger (issue #22). It holds debug
sessions, each a `smith dap` child (lldb-dap, [the debugger](debugger.md)) that it speaks the Debug Adapter Protocol
to, and offers eleven tools: `launch`, `breakpoint`, `continue`, `step_over`, `step_in`, `step_out`, `pause`, `stack`,
`variables`, `evaluate` and `stop`. The skill smith-debug is the short guide
for users; this is how it is built.

It is a feature of the full smith (`-DSMITH_MCP`, which `smith build` passes when clang is inside): the stage0 has the
verb and says "mcp needs the full smith". The smith plugin declares it (`mcpServers` in `.claude-plugin/plugin.json`),
so a project that enables the plugin has the tools; by hand: `claude mcp add smith -- smith mcp`.

## How it is built

The server is `runtime/mcp.c` and the files it includes, in order: `mcp_text.c` (the text a result is built in, the
readers of a tool's arguments), `mcp_dap.c` (a session: the child, DAP's framing, requests and replies, the queue of
events), `mcp_show.c` (what a stop, a stack and variables look like), `mcp_tools.c` (the tools and their table) and
`mcp_serve.c` (MCP: the messages, the versions, the loop). smith.c includes it after the JSON, text and protocol code.

- **On `<runtime/io.h>` and in one thread.** The server's stdin, every session's pipes and exit, and the clock are ops on
  one event loop. A tool call is a function that runs the loop (`smith_mcp_pump`) until what it waits for has happened or
  its time is up; input that arrives meanwhile is buffered, so a client that pipelines requests is answered in order.
  Nothing is allocated but in arenas (the io's), nothing recurses, every loop has a bound.
- **No protocol is written by hand.** Every message is a type of the code generated from the official schemas
  (`.smith/protocol/dap.[ch]` and `mcp.[ch]`, build outputs; [the protocol code](protocol-generation.md)). What is read from a tree by name is what the
  schemas do not have: the `initialize` request and result of the MCP revisions before 2026-07-28, and a tool's
  arguments, which are the tool's own schema.
- **Versions.** It answers `server/discover` (2026-07-28: every request then stands alone) with `supportedVersions:
  ["2026-07-28"]`, and `initialize` with the version the client asks for when it is one of 2026-07-28, 2025-11-25,
  2025-06-18, 2025-03-26 and 2024-11-05, else 2025-11-25. Claude Code 2.1.304 sends `server/discover` first and falls
  back to `initialize` with 2025-11-25 (seen with a logging probe and `claude mcp list`).
- **Events are queued and come back with the next call.** The program's output, a stop, an exit are events of the
  adapter; a tool takes them in order. A call that resumes the program takes events until the program stops or ends or its
  `timeout_ms` (default 30 s, at most 10 minutes) is up. While the program is stopped, a stop or exit further on in the
  queue is not taken: it has not happened yet. Output is reported as `[stdout]` (or `[stderr]`, `[adapter]`) once per
  run of one source, at most 6 KiB a call, and a result is cut at 96 KiB with `[result truncated]`.
- **A session** is `s1`, `s2`, ... (at most four); a call without `session` means the latest. `launch` starts the
  adapter, sends `initialize`, `launch` (`program`, `args`, `cwd`, and `stopOnEntry` unless breakpoints were given),
  waits for `initialized`, sends the breakpoints and `configurationDone`, and waits for the first stop. A program that is
  not a file, an adapter that exits or refuses, is an error result and leaves no session. When the client's input ends,
  every session is disconnected with `terminateDebuggee`, then the adapter is closed, then signalled (SIGTERM, SIGKILL):
  no program outlives the server.
- **Evaluation is read-only by default.** An expression with an assignment, `++`, `--`, a call of a function or a
  backtick (an lldb command) is refused before it is sent, unless `allow_side_effects` is true. The test is lexical, so
  it errs towards refusing (`sizeof(x)` is allowed, `f(x)` is not).
- **The folder is the shell's.** A relative `file` or `program` is made absolute against the session's `cwd`, which
  defaults to the folder `smith mcp` was started in, spelled as `$PWD` has it when that is the same folder: a compile
  writes that spelling in its debug info (`/var/folders/...`, not `/private/var/folders/...` on macOS), and lldb matches a
  breakpoint's file by it.
- **A test is launched by name.** `launch` with `test` runs a smith project's test runner (`runner`, else the one
  `.smith/build.info` names with its `runner` line, else, for a project built before that line, the first of `build/tests`,
  `build/debug/tests`, `build/sanitize/tests` and `build/test/tests`) with `--test <name> --in-process`, since
  lldb on macOS does not follow the fork of the runner's workers; the breakpoints are then hit in the test.

## The tools

Results are text for an agent to read. Examples from the tests (`tests/mcp_test.smith`), on a C program:

| Tool | Arguments | A result |
|---|---|---|
| `launch` | `program` (with `args`) or `test`; `runner`, `cwd`, `breakpoints` `[{file, line, condition?}]`, `timeout_ms` | `session s1: DIR/prog` / `breakpoint 1: prog.c:10 (verified)` / `stopped: breakpoint (breakpoint 1)` / `  #0 add prog.c:10` / `  > 10:     return sum;` |
| `breakpoint` | `file`, `line`; `condition`, `remove`, `session` | `breakpoint 2: prog.c:17 if r == 7 (verified)`; `removed the breakpoint at prog.c:10` |
| `continue`, `step_over`, `step_in`, `step_out` | `session`, `timeout_ms` | `[stdout]` / `7` / `stopped: breakpoint (breakpoint 2)` / `  #0 main prog.c:17` ...; `exited: the program exited with code 0` |
| `pause` | `session`, `timeout_ms` | the stop, or `the program is already stopped` |
| `stack` | `levels` (20), `session` | `thread 5590224, 2 frames` / `#0 main prog.c:17` / `#1 start /usr/lib/dyld`start:1673` |
| `variables` | `frame` (0), `scope` (locals, globals, registers), `depth` (1, to 6), `max` (50), `session` | `frame #1, scope locals` / `p (Point)` / `  x = 3 (int)` / `  y = 4 (int)` / `r = 1 (int)` |
| `evaluate` | `expression`; `frame`, `depth`, `allow_side_effects`, `session` | `p.x + p.y = 7 (int)`; `refused: the expression may change the program ...` |
| `stop` | `session` | `session s1 stopped (the program had exited with code 0)` |

A tool that is not given what it needs, or has no session, is a result with `isError: true` and the reason
(`no debug session: call launch first`). `.smith` programs show their names as written (`geometry::box::area`, `b
(geometry::box::Box)`), as in [the debugger](debugger.md).

## lldb's behaviour that shows

- After a `step_out` that ended at a breakpoint (not at the caller), the next `continue` stops once more, as a `step`
  in the caller's caller, and a second `continue` runs on. Seen with prog.c above: stopped in `add`, `step_over` to
  main:15, `step_out` stops at the second breakpoint (main:17), and `continue` reports `stopped: step  #0 start`. It looks like
  lldb keeping the interrupted step out; the agent just calls `continue` again.
- The frame under `main` is `start /usr/lib/dyld`start:1673`.
- A program's output arrives as `[stdout]` events while it runs (lldb gives it a terminal, so a `printf` line shows at
  once).

## Tests

`tests/mcp_fake.c` runs the server on the fakes (162 checks, built three ways by `tests/mcp_test.smith`): a scripted client on
the console and a scripted `smith dap` child (the process fake), whose frames answer the requests the server
sends, numbered from 1. It covers the versions, the refusals, every tool, a whole session, a test launched by name, an
adapter that exits at once or refuses, and timeouts on the fake clock. `tests/mcp_test.smith` also drives the real `smith mcp`,
a real adapter and real programs, on a C program, a `.smith` module and a smith project's test, and checks that no
program outlives the server. They are the tests `test_mcp_*`; a smith without lldb or an account that may not debug skips the
second half.
