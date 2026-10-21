---
name: smith-debug
description: "Debugging a C, Objective-C or C++ program with smith: smith dbg (lldb) at a terminal, smith dap (lldb-dap, the Debug Adapter Protocol) for editors such as VS Code, Neovim, Helix and Emacs. Load for debugging, smith mcp (the debugger as MCP tools for an agent), breakpoints, stepping, a backtrace, stopping at a line, lldb, lldb-dap, DAP, launch.json, debugserver, 'Not allowed to attach to process', get-task-allow, Developer Tools permission."
---

# Debug with smith

This skill ships in the smith plugin, from `.agents/skills/smith-debug/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

`smith dbg` is lldb's own command line and `smith dap` is `lldb-dap`'s own main, both from the LLVM linked into smith (23.1.3), with the arguments unchanged. A stage0 smith runs an external one instead (`$SMITH_LLDB`, `$SMITH_LLDB_DAP`). Build the program with `-g -O0` (`smith new` projects have a debug build). Details: [docs/debugging.md](../../../docs/debugging.md).

## smith dbg: a person at a terminal

- Interactive: `smith dbg build/debug/app -- arg1 arg2`; then `b main.c:42`, `run`, `bt`, `frame variable`, `p expr`, `n`, `s`, `c`, `q`.
- Batch, which is how to script it and what an agent runs: `smith dbg -b -o 'b file.c:42' -o run -o 'frame variable' -o bt -o kill build/debug/app -- args`. `-b` exits after the last `-o`; add `-o kill` when the program would otherwise run on.
- Set a breakpoint after the prologue (a line of the body, not the function's first line), or arguments read as garbage.
- A program that forks (smith's test runner `--jobs`, servers): the breakpoint hits in the child, which the debugger does not follow on macOS and which dies with SIGTRAP. Run the program with its option for one process. For smith's test runner that is `--in-process`: `smith dbg -b -o 'b test_x.c:12' -o run -o 'frame variable' -o kill build/tests -- --test name --in-process`. It runs the tests in the runner's own process, with no fork and no timeout, so a crash or hang ends the run; `--jobs 1` and `--isolate` still fork, and the breakpoint then hits in a child the debugger does not follow.
- **Debug one test** (a project with `smith_tests`): `smith dbg -- build/tests --test <name> --in-process`, with `b file:line` inside the test (`-b -o ... ` for a script). `<name>` is as written, `test_x`, or `module::path::test_x` for a `.smith` test module. `--in-process` is needed because the runner forks a worker for each test and the debugger stays in the parent, so a breakpoint in the test is never hit. `.smith/build.info` names the runner on its `runner` line (usually `build/tests`).
- No Python scripting, no `gui` (curses): the `script` command is absent. Everything else is lldb's.

- **`.smith` programs** (compile each source to a `.o` and link, or use `smith build`): frames, `bt` and types show the whole module name, `geometry::box::area(b=...)` and `(geometry::box::Box) b`, and `p (geometry::box::Box)b` and `type lookup geometry::box::Box` work. Break with `b geometry::box::area` or `br set -r box::area`; `b box::area` and `b area` do not resolve. A function cannot be called from `p` by its module name.

## smith dap: an editor

`smith dap` speaks DAP on stdin and stdout. An editor starts it as the debug adapter; the editor sends `launch` with `program`, `args`, `cwd`, then `setBreakpoints`.
- **VS Code:** the LLDB DAP extension (`llvm-vs-code-extensions.lldb-dap`); set `"lldb-dap.executable-path"` to a wrapper (`#!/bin/sh` / `exec smith dap "$@"`), since the setting is one path. `launch.json`: `{"type": "lldb-dap", "request": "launch", "name": "debug", "program": "${workspaceFolder}/build/debug/app"}`.
- **Neovim (nvim-dap):** `dap.adapters.smith = { type = 'executable', command = 'smith', args = { 'dap' } }`, and a `dap.configurations.c` entry with `type = 'smith'`, `request = 'launch'`, `program`. The smith repository's Neovim plugin (`plugin/smith.lua`, docs/neovim.md) registers the adapter and one configuration, "smith: launch", when nvim-dap is installed, so nothing asks which to use. Start a session (`:lua require('dap').continue()`) and it decides: with the cursor inside a function named `test_...` (in a `c` or `smith` buffer) it builds the project's test runner and launches it with `--test <name> --in-process` (`a::b::test_x` when the buffer has `module a::b;`), so a breakpoint in that test stops; anywhere else it runs `smith build` and launches the one program `smith_default` names, as `smith run` picks it. `cwd` is the project root; a failed build shows its output and aborts; it asks for a path only without a project or when the build names no single program. nvim-dap alone does not build, so a hand-written configuration launches a stale binary unless it builds first.
- **Neovim (neotest):** the smith repository's `neotest-smith` adapter (`editors/neovim/lua/neotest-smith`) debugs the nearest test with neotest's `dap` strategy: it builds the runner and gives nvim-dap `type = 'smith'`, the runner's path, `args = { '--test', <name>, '--in-process' }`, `cwd` the project root. Add it with `require('neotest').setup({ adapters = { require('neotest-smith') } })`; docs/neovim.md.
- **Helix:** in `languages.toml`, `[language.debugger]` with `name = "lldb-dap"`, `transport = "stdio"`, `command = "smith"`, `args = ["dap"]`, and a template with `request = "launch"`.
- **Emacs (dape):** a `dape-configs` entry with `command "smith"`, `command-args ("dap")`, `:request "launch"`, `:program`.
Only the protocol was exercised by a script (`tests/dap_client.c`); the editors' own settings above were not run.

## macOS: signing and permissions

- Debugging needs the system's debugserver, which Apple signs; smith finds it (Xcode's, else the Command Line Tools') and sets `LLDB_DEBUGSERVER_PATH`; set it yourself to choose another.
- The account needs Developer mode on: `DevToolsSecurity -status`; `sudo DevToolsSecurity -enable` and membership in the `_developer` group (`dseditgroup -o edit -a $USER -t user _developer`). The first debug asks to let "Developer Tools Access" take control of another process: allow it, once.
- A smith-built program is ad-hoc signed by the linker and debuggable as it is. A hardened-runtime or notarized one fails with "Not allowed to attach to process": re-sign it for debugging, `codesign -s - -f --options runtime --entitlements get-task-allow.plist app`, with `com.apple.security.get-task-allow` true in the plist.

## Agents: smith mcp

`smith mcp` is an MCP server over stdio with the debugger as tools (issue #22, [docs/mcp.md](../../../docs/mcp.md)); the
smith plugin declares it, so with the plugin enabled an agent has `launch`, `breakpoint`, `continue`, `step_over`,
`step_in`, `step_out`, `pause`, `stack`, `variables`, `evaluate` and `stop`. Without the plugin: `claude mcp add smith --
smith mcp`. It needs the full smith (the stage0 says so), and the macOS permissions below.
- Build the program with `-g -O0` first. `launch` with `program` (and `args`, `cwd`) runs it; with `test` it runs the
  project's test runner (`build/tests`, `build/debug/tests`, `build/sanitize/tests`; `runner` names another) on that
  test, `--in-process`, so a breakpoint in the test is hit. Give `breakpoints` `[{file, line, condition?}]` and it runs to
  the first one; give none and it stops at the entry, then `breakpoint` and `continue`.
- A result is text: where it stopped (`stopped: breakpoint (breakpoint 1)`, `#0 add prog.c:10`, the source line), the
  program's output as `[stdout]`, and how it ended (`exited: the program exited with code 0`). Then `stack`, `variables`
  (`frame`, `depth`, `scope`) and `evaluate` read the stopped program; `stop` ends the session and kills the program. The
  program is also killed when the client goes away.
- `evaluate` is read-only: an assignment, `++`, a call or a backtick is refused; pass `allow_side_effects: true` to run it.
- Set a breakpoint on a line of a function's body, not its first line. A relative `file` is against the session's `cwd`.
- A `step_out` that ends at a breakpoint leaves the next `continue` to stop once more as a `step`; call `continue` again.
- `.smith` programs show their names as written (`geometry::box::area`). A long run needs a `timeout_ms` on the call that
  waits (default 30 s): `still running after N ms` means call `continue` to wait again, or `pause`.
- Without the plugin's server an agent runs `smith dbg -b` as above.
