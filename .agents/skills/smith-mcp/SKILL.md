---
name: smith-mcp
description: "smith mcp: the debugger as MCP tools for an agent (launch, breakpoint, continue, step_over, step_in, step_out, pause, stack, variables, evaluate, stop). Load when you need to debug a running C, Objective-C, C++ or .smith program or one test yourself, to stop at a line and read variables, on 'no debug session: call launch first', 'refused: the expression may change the program', 'still running after N ms', 'mcp needs the full smith', or to add smith mcp to an agent with claude mcp add."
---

# smith mcp: debug a program as an agent

This skill ships in the smith plugin, from `.agents/skills/smith-mcp/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. The terminal debugger is `smith dbg` and editors use `smith dap` (`smith-debug`, which also has the macOS permissions every debugger needs); the server's design is `knowledge/mcp-server.md`.

`smith mcp` is a Model Context Protocol server on stdio that drives lldb through `smith dap` children. The smith plugin declares it (`mcpServers`), so with the plugin enabled you already have the tools. Without the plugin: `claude mcp add smith -- smith mcp`. It needs the full smith (a stage0 says `mcp needs the full smith`). If no tools are available, fall back to `smith dbg -b -o ... ` (batch lldb, `smith-debug`).

## The loop

1. **Build with debug info and no optimization:** the debug profile does (`smith build`; `build/debug/<name>`).
2. **`launch`** with `program` (and `args`, `cwd`) and `breakpoints` `[{file, line, condition?}]`. It runs to the first breakpoint; with none it stops at the entry. Or `launch` with `test` to debug one test of the project: it runs the test runner (`build/tests`, or the `runner` named in `.smith/build.info`; `runner` overrides) on `--test <name> --in-process`, so a breakpoint in the test is hit. A `.smith` module's test is `module::path::test_x`.
3. Read the stop: the result is text, such as `stopped: breakpoint (breakpoint 1)`, `#0 add prog.c:10` and the source line; the program's output as `[stdout]`; the end as `exited: the program exited with code 0`.
4. **Inspect:** `stack` (`levels`), `variables` (`frame`, `scope` locals/globals/registers, `depth` up to 6, `max`), `evaluate` (`expression`, `frame`).
5. **Move:** `continue`, `step_over`, `step_in`, `step_out` (each takes `timeout_ms`, default 30 s, at most 10 minutes); `pause` stops a running program; `breakpoint` adds (`file`, `line`, `condition`) or removes (`remove`) one.
6. **`stop`** ends the session and kills the program. The program is also killed when the client goes away.

## Rules that save a round trip

- Put a breakpoint on a line of a function's body, not its first line, or arguments read as garbage. A relative `file` is resolved against the session's `cwd`.
- `evaluate` is read-only: an assignment, `++`, `--`, a call or a backtick is refused (`refused: the expression may change the program`); pass `allow_side_effects: true` to run it. `sizeof(x)` is allowed, `f(x)` is not.
- A call that resumes waits until the program stops, ends or `timeout_ms` passes. `still running after N ms` means call `continue` to wait again, or `pause`.
- A `step_out` that ends at a breakpoint leaves the next `continue` to stop once more as a `step`; call `continue` again.
- Up to four sessions at once (`s1`, `s2`...); a call without `session` means the latest.
- A program that forks (a server, smith's test runner with `--jobs`) hits breakpoints in the child, which the debugger does not follow on macOS: use the program's one-process option (`--in-process` for the test runner, which `launch` with `test` does).
- `.smith` programs show their names as written: `geometry::box::area`, `b (geometry::box::Box)`.
- An error result (`isError: true`) says why, such as `no debug session: call launch first`.
- macOS must allow debugging (Developer mode, "Not allowed to attach to process" for hardened programs): see `smith-debug`.
- Say what you saw; do not guess a variable's value from the code when you can read it.
