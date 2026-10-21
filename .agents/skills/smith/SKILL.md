---
name: smith
description: "Using smith, the compiler driver that builds this project. Load when a Makefile says CC := smith, when a build runs smith or smith++, on any message that starts with smith:, or when asked which compiler builds the project or how to make compile_commands.json."
---

# Use smith

This skill ships in the smith plugin, from `.agents/skills/smith/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there; projects get the update through the plugin.

smith is a drop-in replacement for clang, written in C. Its home is https://github.com/adam-r-kowalski/smith, and the source is https://github.com/adam-r-kowalski/smith/blob/main/smith.c. It takes clang's flags and passes them through unchanged. Clang is linked into smith, so smith compiles in its own process, by running clang's own `main` on the arguments. With `$SMITH_CLANG` set, or in a stage0 smith (built by one `cc` command, with no clang inside), it replaces itself with an external clang (`execv`) instead. Exit status, signals and output are clang's, and a build through smith is byte-identical to one through the clang of the same LLVM directly.

## Which compiler runs

- Inputs that are all `.metal` files go to Apple's metal compiler: `xcrun -sdk macosx metal`, or `$SMITH_METAL`.
- Everything else goes to upstream LLVM's clang, the same compiler on every platform: the one linked into smith (LLVM 23.1.3, built by `smith build llvm`; its builtin headers and compiler-rt are read from that install). A stage0 smith on macOS runs Homebrew's `llvm` instead (`/opt/homebrew/opt/llvm/bin/clang`, from `brew install llvm`). Apple's `/usr/bin/clang` is not used.
- `$SMITH_CLANG` (`$SMITH_CLANGXX` for `smith++`) names an external clang to run instead of the one inside smith.
- On macOS smith picks the SDK through the environment, never a flag. Homebrew's clang (`$SMITH_CLANG`) reads a default config that forces `-isysroot` to the Command Line Tools SDK, which can differ from the Xcode SDK that `xcrun` and `metal` use. So, for each variable you have not set, smith sets `CLANG_NO_DEFAULT_CONFIG=1` and `SDKROOT` to `xcrun --show-sdk-path`. Your own `SDKROOT`, `-isysroot` or `--config` wins; set `CLANG_NO_DEFAULT_CONFIG` yourself to keep the default config.
- Invoked as `smith++`, it uses clang++ (`$SMITH_CLANGXX`). It is the same program under another name. Only linking C++ needs it.

## Use it

1. A project says `CC := smith` in its Makefile, and its shader rules use `$(CC)` too. smith must be on PATH. Write flags as you would for clang.
2. See what it does: `SMITH_VERBOSE=1` prints `smith: in-process <clang> <args>` (or `smith: exec <compiler> <args>` for an external compiler) on stderr before running. `smith --smith-which <args>` prints the compiler it would choose without running it: `clang in-process <clang>` for the one inside smith, `clang <path>` for an external one. `smith --smith-version` prints its version.
3. For `compile_commands.json`, smith adds nothing. Pass `-MJ fragments/<file>.json` as with clang, keeping the fragments
   in their own folder, then merge them:
   `{ echo '['; sed '$ s/,$//' fragments/*.json; echo ']'; } > compile_commands.json`
4. A project with a `build.c` at its root builds with `smith build`; the smith-build skill covers it.

## When something goes wrong

1. A compiler error or warning that smith prints is clang's (or metal's), not smith's. Fix it as you would for clang.
2. A message that starts with `smith:` is smith's own:
   - `.metal and other inputs need different compilers and flags; compile the shaders in their own command`: `.metal` needs Apple's metal compiler and the rest needs clang, with different flags (`-std=gnu23` and link flags against `-std=metal...`) and different outputs, and smith hands each command to exactly one compiler. Run the shaders in their own command with their own flags, as a Makefile shader rule does.
   - `not an executable: <path>` or `refers to smith itself: <path>`: a `SMITH_*` variable is wrong. Fix or unset it.
   - `cannot find clang in /opt/homebrew/opt/llvm/bin: run `brew install llvm`, or set SMITH_CLANG` (macOS, in a stage0 smith with no clang inside), or `cannot find a compiler; set SMITH_CLANG or SMITH_CLANGXX, tried <tool>` (elsewhere): no external clang is installed. Install LLVM, or set the variable to a clang.
   - `cannot find xcrun`: the Xcode command line tools are missing, so metal cannot run.
   - `cannot execute <program>`: the chosen program exists but would not start. Run it by hand.
   The wording is in https://github.com/adam-r-kowalski/smith/blob/main/smith.c.
3. If smith is missing from PATH or seems broken, stop and tell the owner. Installing and repairing smith is done in smith's own repository, not here.

## Rules

- In a project whose Makefile uses smith, keep it. Never swap in another compiler to get around a problem.
- If smith seems wrong, run the command again with `SMITH_VERBOSE=1`. Report the command and its output to the owner. Do not work around it, and do not reinstall it.
