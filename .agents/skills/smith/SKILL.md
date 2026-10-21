---
name: smith
description: "Using smith, the compiler driver that builds this project. Load when a Makefile says CC := smith-cc, when a build runs smith cc, smith c++, smith-cc or smith-c++, on any message that starts with smith:, or when asked which compiler builds the project or how to make compile_commands.json."
---

# Use smith

This skill ships in the smith plugin, from `.agents/skills/smith/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there; projects get the update through the plugin.

smith is a toolchain for C, written in C, and `smith cc` is a drop-in replacement for clang. Its home is https://github.com/adam-r-kowalski/smith, and the source is https://github.com/adam-r-kowalski/smith/blob/main/smith.c. `smith cc` takes clang's flags and passes them through unchanged. Clang is linked into smith, so smith compiles in its own process, by running clang's own `main` on the arguments. With `$SMITH_CLANG` set, or in a stage0 smith (built by one `cc` command, with no clang inside), it replaces itself with an external clang (`execv`) instead. Exit status, signals and output are clang's, and a build through smith is byte-identical to one through the clang of the same LLVM directly.

## The commands

smith's top level is only verbs, as zig's is. `smith foo.c -o foo` is an error that says to run `smith cc foo.c -o foo`.

- `smith cc <clang arguments>` compiles and links C, Objective-C and Metal; `smith c++ <clang arguments>` is the same for C++ (linking C++ needs it). Nothing is added, removed or reordered.
- **For one program path**, where a command cannot take an argument (`CC=` in a Makefile, CMake's `CMAKE_C_COMPILER`, the first word of a `compile_commands.json` command, which clangd reads as a compiler), use the links `smith-cc` and `smith-c++`. They are smith under another name and act as `smith cc` and `smith c++`; `smith build install` puts them beside smith. The old `smith++` is gone.
- `smith build` runs the project's `build.c` (the smith-build skill). The project's own verbs run the task or alias of the same name in it: `smith run` (its arguments go to the program), `smith test` (its arguments are the test runner's options, such as `--test name --seed 7`; the smith-test skill), `smith check`, and `smith fmt` and `smith lint` with no arguments. `smith build run` is refused with a pointer to the verb. `smith prove` and `smith dbg` say they are not available yet.
- `smith new`, `smith fetch`, `smith cache` and `smith fmt`, `smith lint`, `smith lsp` with arguments (below).

## Which compiler runs

- Inputs that are all `.metal` files go to Apple's metal compiler: `xcrun -sdk macosx metal`, or `$SMITH_METAL`.
- Everything else goes to upstream LLVM's clang, the same compiler on every platform: the one linked into smith (LLVM 23.1.3, built by `smith build llvm`; its builtin headers and compiler-rt are read from that install). A stage0 smith on macOS runs Homebrew's `llvm` instead (`/opt/homebrew/opt/llvm/bin/clang`, from `brew install llvm`). Apple's `/usr/bin/clang` is not used.
- `$SMITH_CLANG` (`$SMITH_CLANGXX` for `smith c++`) names an external clang to run instead of the one inside smith.
- On macOS smith picks the SDK through the environment, never a flag. Homebrew's clang (`$SMITH_CLANG`) reads a default config that forces `-isysroot` to the Command Line Tools SDK, which can differ from the Xcode SDK that `xcrun` and `metal` use. So, for each variable you have not set, smith sets `CLANG_NO_DEFAULT_CONFIG=1` and `SDKROOT` to `xcrun --show-sdk-path`. Your own `SDKROOT`, `-isysroot` or `--config` wins; set `CLANG_NO_DEFAULT_CONFIG` yourself to keep the default config.
- `smith c++`, and the link `smith-c++`, use clang++ (`$SMITH_CLANGXX`). Only linking C++ needs it.

## Format, lint and the editor

`smith fmt`, `smith lint` and `smith lsp` are clang-format, clang-tidy and clangd, linked into smith from the same LLVM as the compiler. Each is recognised only as the exact first argument, and the rest of the arguments go to the tool unchanged, so `smith fmt -i main.c`, `smith lint --quiet main.c -- <flags>` and `smith lsp --check=main.c` behave as the tools do alone (with no arguments they are the project's `fmt` and `lint`, above): a project's `.clang-format` and `.clang-tidy` apply, and without them the tools' own defaults do. Their output is byte-identical to Homebrew's 23.1.3. A stage0 smith runs an external tool instead (Homebrew's, or `$SMITH_CLANG_FORMAT`, `$SMITH_CLANG_TIDY`, `$SMITH_CLANGD`).

- **Editor:** point the editor's clangd at the command `smith lsp`, with no `--query-driver` (with it clangd cannot build the SDK's modules). It reads the project's `compile_commands.json` (`smith build` writes it), and it uses the resource directory and the SDK that the compiler does. In VS Code's clangd extension: `"clangd.path": "smith", "clangd.arguments": ["lsp"]`. Neovim: `vim.lsp.config('clangd', { cmd = { 'smith', 'lsp' } })`. Helix: `[language-server.clangd]` with `command = "smith"` and `args = ["lsp"]`. Zed: `"lsp": {"clangd": {"binary": {"path": "smith", "arguments": ["lsp"]}}}`. Emacs eglot: `(add-to-list 'eglot-server-programs '((c-mode c++-mode objc-mode) . ("smith" "lsp")))`. `smith lsp --check=<file>` is the same server run on one file, for a check without an editor.
- **Claude Code:** the smith plugin declares `smith lsp` as the language server for `.c`, `.h`, `.m`, `.mm`, `.cpp`, `.cc` and `.hpp` files, so an agent gets go-to-definition, references, types, symbols and call hierarchies through its LSP tool, and the diagnostics after each edit. `smith` must be on the PATH of the shell that starts Claude Code. If the official `clangd-lsp` plugin is also enabled, the first registered server handles a file, so disable one of them.
- clangd keeps its index in `.cache/clangd` of the project. The index shards Homebrew's clangd wrote are compressed, and smith's LLVM has no zlib, so `smith lsp` logs "Error while reading shard" for them once and indexes again; delete `.cache/clangd` to start clean.

## Use it

1. A project says `CC := smith-cc` in its Makefile (or `smith cc` where a command may have an argument), and its shader rules use `$(CC)` too. smith must be on PATH, with its links. A Makefile that says `CC := smith` fails with the hint to use `smith cc`. Write flags as you would for clang.
2. See what it does: `SMITH_VERBOSE=1` prints `smith: in-process <clang> <args>` (or `smith: exec <compiler> <args>` for an external compiler) on stderr before running. `smith cc --smith-which <args>` (or `smith-cc --smith-which <args>`) prints the compiler it would choose without running it: `clang in-process <clang>` for the one inside smith, `clang <path>` for an external one. `smith --smith-version` prints its version.
3. For `compile_commands.json`, smith adds nothing. Pass `-MJ fragments/<file>.json` as with clang, keeping the fragments
   in their own folder, then merge them:
   `{ echo '['; sed '$ s/,$//' fragments/*.json; echo ']'; } > compile_commands.json`
4. A project with a `build.c` at its root builds with `smith build`; the smith-build skill covers it.

## When something goes wrong

1. A compiler error or warning that smith prints is clang's (or metal's), not smith's. Fix it as you would for clang.
2. A message that starts with `smith:` is smith's own:
   - `.metal and other inputs need different compilers and flags; compile the shaders in their own command`: `.metal` needs Apple's metal compiler and the rest needs clang, with different flags (`-std=gnu23` and link flags against `-std=metal...`) and different outputs, and smith hands each command to exactly one compiler. Run the shaders in their own command with their own flags, as a Makefile shader rule does.
   - `<arg> is not a smith command; to compile, run `smith cc ...``: the top level is only verbs, so a compile needs `smith cc` (or `smith c++`), or a Makefile's `CC` must be `smith-cc`. A `.compiler = "smith"` in a build.c fails the same way: use `"smith-cc"`.
   - `<verb> is not available yet (issue #N)`: `smith prove` and `smith dbg` have not arrived.
   - `<verb> is a verb, not a target of smith build; run `smith <verb>``: run, test, check, fmt, lint, prove and dbg are verbs only.
   - `not an executable: <path>` or `refers to smith itself: <path>`: a `SMITH_*` variable is wrong. Fix or unset it.
   - `cannot find clang in /opt/homebrew/opt/llvm/bin: run `brew install llvm`, or set SMITH_CLANG` (macOS, in a stage0 smith with no clang inside), or `cannot find a compiler; set SMITH_CLANG or SMITH_CLANGXX, tried <tool>` (elsewhere): no external clang is installed. Install LLVM, or set the variable to a clang.
   - `cannot find xcrun`: the Xcode command line tools are missing, so metal cannot run.
   - `cannot execute <program>`: the chosen program exists but would not start. Run it by hand.
   The wording is in https://github.com/adam-r-kowalski/smith/blob/main/smith.c.
3. If smith is missing from PATH or seems broken, stop and tell the owner. Installing and repairing smith is done in smith's own repository, not here.

## Rules

- In a project whose Makefile uses smith, keep it. Never swap in another compiler to get around a problem.
- If smith seems wrong, run the command again with `SMITH_VERBOSE=1`. Report the command and its output to the owner. Do not work around it, and do not reinstall it.
