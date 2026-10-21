# smith dbg and smith dap

`smith dbg` is lldb's driver and `smith dap` is lldb-dap's main, both run in smith's process from the LLVM it links
(23.1.3), each with the arguments unchanged, like `smith fmt`, `smith lint` and `smith lsp`. The skill
[smith-debug](../.agents/skills/smith-debug/SKILL.md) is the short guide for users; this is how it is built.

## How it is built

- **LLVM** (`scripts/llvm.sh`) builds with `lldb` in `LLVM_ENABLE_PROJECTS`. LLVM installs lldb as a dylib and no static
  libraries, so a marked step of the script copies lldb's static libraries from the build tree into the install
  (`liblldb[A-Z]*.a`) and makes `libsmithlldb.a`: the objects that make `liblldb.dylib` (the SB API; no library holds
  them), the driver's and lldb-dap's objects with the symbol `main` renamed (`smith_lldb_driver_main`,
  `smith_lldb_dap_tool_main`), and Platform.cpp. All of them have their static initializers moved out of the section
  dyld runs at load (`scripts/defer_init.c`, section `__smith_lldb`), run by `shim/lldb.cpp` and `shim/lldb_dap.cpp`
  with `smith_tool_start` when a debugger command runs.
- **build.c** links them when the install has `libsmithlldb.a` (`-DSMITH_LLDB`, the shims `lldb` and `lldb_dap`,
  `-ledit` and the frameworks Foundation, CoreFoundation, CoreServices and Security). An install without lldb gives a
  smith whose `dbg` and `dap` run an external lldb (`$SMITH_LLDB`, `$SMITH_LLDB_DAP`), as the stage0 does.
- **smith.c** has two entries in the table of tools, `dbg` and `dap`, and sets, for each the caller has not set:
  `LLDB_DEBUGSERVER_PATH` (the developer directory's, else Xcode's, else the Command Line Tools' debugserver) and
  `LLDB_ARGDUMPER_PATH`. lldb expands a program's arguments through `lldb-argdumper`, which it looks up beside
  smith; smith is a fourth hard link in the programs folder (`bin/lldb-argdumper`) and, run under that name, prints
  `{"arguments": [...]}` as LLVM's does.

## The lldb options, and what each costs when off

| Option | Value | Cost of the choice |
|---|---|---|
| `LLDB_USE_SYSTEM_DEBUGSERVER` | ON | no debugserver is built; the system's is used. smith needs Xcode or the Command Line Tools, and the debugserver the system signs. |
| `LLDB_ENABLE_PYTHON` | OFF | no scripting: Python pretty printers, `script`, scripted breakpoints and commands. Everything in a batch session works. With it ON, smith would link or load libpython. |
| `LLDB_ENABLE_LIBEDIT` | ON | history and line editing in `smith dbg`, from the system's `/usr/lib/libedit.dylib`. Off, the prompt reads plain lines. |
| `LLDB_ENABLE_CURSES` | OFF | no `gui` command. |
| `LLDB_ENABLE_LIBXML2` | OFF | lldb reads no XML (some platform and plist queries). |
| `LLDB_ENABLE_LZMA` | OFF | no compressed symbol sections. |
| `LLDB_ENABLE_LUA`, `TREESITTER` | OFF | no Lua scripting, no grammar-based source colouring. |

## Signing and permissions on macOS (measured here)

- The debuggee launched by lldb: an ad-hoc, linker-signed program (what smith's lld makes) and game's `build/game` and
  test runner debug as they are. A program signed with the hardened runtime (`codesign -s - --options runtime`) is
  refused: "Not allowed to attach to process". Signing it with `com.apple.security.get-task-allow` makes it debuggable.
- debugserver is Apple's, signed with `com.apple.private.cs.debugger` and related entitlements; nothing a smith build
  can sign matches that, so `LLDB_USE_SYSTEM_DEBUGSERVER` is the only way.
- The account needs Developer mode (`DevToolsSecurity -status`; `-enable` as an administrator) and the `_developer`
  group; the first debug shows the "Developer Tools Access" prompt.

## Cost

Measured on this machine (unstripped, `-g`; the machine was busy, so the milliseconds are the best of several runs):

| | size | `smith --smith-version` |
|---|---|---|
| main (0251349), no lldb linked | 210.1 MB | 15.7 ms |
| this change: lldb linked, exporting only `main` | 225.7 MB | 10.5 ms |
| (before the prover and #20: same code, lldb linked as before, without the flag) | 174.8 MB | 19.1 ms |

On the r2 install, lldb adds 15.6 MB, and with the flag every smith process starts in less time than before it:
10.5 ms against 15.7 ms (`smith cc -c` of an empty file: 24 ms against 28 ms). lldb loads the frameworks it needs: 702 images instead of 84.
Without `-Wl,-exported_symbol,_main` the same lldb added 9 ms: smith's 57,000 weak definitions (inline functions and
templates of LLVM) were exported, and dyld binds each of them at every start against every image loaded (296 million
instructions to start, against 52 million). The flag takes the 4 ms of that off the smith without lldb too. `smith dbg
--version` took 27 to 29 ms on the busy machine.

## Tests

`tests/dap.sh` runs `smith dbg -b` on a small program and `tests/dap_client.c`, a DAP client that writes the requests as
JSON and reads the replies, through `initialize`, `launch`, `setBreakpoints`, `configurationDone`, `stopped`,
`stackTrace`, `scopes`, `variables`, `continue` and `terminated`. A smith without lldb, or an account that may not debug,
skips them.
