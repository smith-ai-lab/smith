---
name: smith-debug
description: "Debugging a C, Objective-C or C++ program with smith: smith dbg (lldb) at a terminal, smith dap (lldb-dap, the Debug Adapter Protocol) for editors such as VS Code, Neovim, Helix and Emacs. Load for debugging, breakpoints, stepping, a backtrace, stopping at a line, lldb, lldb-dap, DAP, launch.json, debugserver, 'Not allowed to attach to process', get-task-allow, Developer Tools permission."
---

# Debug with smith

This skill ships in the smith plugin, from `.agents/skills/smith-debug/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

`smith dbg` is lldb's own command line and `smith dap` is `lldb-dap`'s own main, both from the LLVM linked into smith (23.1.3), with the arguments unchanged. A stage0 smith runs an external one instead (`$SMITH_LLDB`, `$SMITH_LLDB_DAP`). Build the program with `-g -O0` (`smith new` projects have a debug build). Details: [docs/debugging.md](../../../docs/debugging.md).

## smith dbg: a person at a terminal

- Interactive: `smith dbg build/debug/app -- arg1 arg2`; then `b main.c:42`, `run`, `bt`, `frame variable`, `p expr`, `n`, `s`, `c`, `q`.
- Batch, which is how to script it and what an agent runs: `smith dbg -b -o 'b file.c:42' -o run -o 'frame variable' -o bt -o kill build/debug/app -- args`. `-b` exits after the last `-o`; add `-o kill` when the program would otherwise run on.
- Set a breakpoint after the prologue (a line of the body, not the function's first line), or arguments read as garbage.
- A program that forks (smith's test runner `--jobs`, servers): the breakpoint hits in the child, which the debugger does not follow on macOS and which dies with SIGTRAP. Run the program with its option for one process. For smith's test runner that is `--in-process`: `smith dbg -b -o 'b test_x.c:12' -o run -o 'frame variable' -o kill build/tests -- --test name --in-process`. It runs the tests in the runner's own process, with no fork and no timeout, so a crash or hang ends the run; `--jobs 1` and `--isolate` still fork, and the breakpoint then hits in a child the debugger does not follow.
- No Python scripting, no `gui` (curses): the `script` command is absent. Everything else is lldb's.

- **`.smith` programs** (compile each source to a `.o` and link, or use `smith build`): frames, `bt` and types show the whole module name, `geometry::box::area(b=...)` and `(geometry::box::Box) b`, and `p (geometry::box::Box)b` and `type lookup geometry::box::Box` work. Break with `b geometry::box::area` or `br set -r box::area`; `b box::area` and `b area` do not resolve. A function cannot be called from `p` by its module name.

## smith dap: an editor

`smith dap` speaks DAP on stdin and stdout. An editor starts it as the debug adapter; the editor sends `launch` with `program`, `args`, `cwd`, then `setBreakpoints`.
- **VS Code:** the LLDB DAP extension (`llvm-vs-code-extensions.lldb-dap`); set `"lldb-dap.executable-path"` to a wrapper (`#!/bin/sh` / `exec smith dap "$@"`), since the setting is one path. `launch.json`: `{"type": "lldb-dap", "request": "launch", "name": "debug", "program": "${workspaceFolder}/build/debug/app"}`.
- **Neovim (nvim-dap):** `dap.adapters.smith = { type = 'executable', command = 'smith', args = { 'dap' } }`, and a `dap.configurations.c` entry with `type = 'smith'`, `request = 'launch'`, `program`. The smith repository's Neovim plugin (`plugin/smith.lua`, docs/neovim.md) registers both for you when nvim-dap is installed: "smith: build and launch the program" first (its `program` runs `smith build` in the folder with `build.c`, shows the build's errors and aborts on failure, then returns the one program that `smith_default` names, as `smith run` picks it, with `cwd` the project root), and "smith: launch a program", which asks for a path. nvim-dap alone does not build, so a hand-written configuration launches a stale binary unless it builds first.
- **Helix:** in `languages.toml`, `[language.debugger]` with `name = "lldb-dap"`, `transport = "stdio"`, `command = "smith"`, `args = ["dap"]`, and a template with `request = "launch"`.
- **Emacs (dape):** a `dape-configs` entry with `command "smith"`, `command-args ("dap")`, `:request "launch"`, `:program`.
Only the protocol was exercised by a script (`tests/dap_client.c`); the editors' own settings above were not run.

## macOS: signing and permissions

- Debugging needs the system's debugserver, which Apple signs; smith finds it (Xcode's, else the Command Line Tools') and sets `LLDB_DEBUGSERVER_PATH`; set it yourself to choose another.
- The account needs Developer mode on: `DevToolsSecurity -status`; `sudo DevToolsSecurity -enable` and membership in the `_developer` group (`dseditgroup -o edit -a $USER -t user _developer`). The first debug asks to let "Developer Tools Access" take control of another process: allow it, once.
- A smith-built program is ad-hoc signed by the linker and debuggable as it is. A hardened-runtime or notarized one fails with "Not allowed to attach to process": re-sign it for debugging, `codesign -s - -f --options runtime --entitlements get-task-allow.plist app`, with `com.apple.security.get-task-allow` true in the plist.

## Agents

Agent tools (breakpoints, variables, stacks as MCP tools) will be `smith mcp` (issue #22), which will hold them. Until then an agent runs `smith dbg -b`.
