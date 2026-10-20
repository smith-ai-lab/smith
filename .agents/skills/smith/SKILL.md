---
name: smith
description: "Using smith, the compiler driver that builds this project. Load when a Makefile says CC := smith, when a build runs smith or smith++, on any message that starts with smith:, or when asked which compiler builds the project, how to reinstall it, or how to make compile_commands.json."
---

# Use smith

This file is copied from `~/Documents/smith/.agents/skills/smith/SKILL.md`. Change it there, then copy it again.

smith is a drop-in replacement for clang, written in C. The source is `~/Documents/smith` (GitHub adam-r-kowalski/smith, private). It takes clang's flags and passes them through unchanged. Then it replaces itself with the compiler (`execv`). Exit status, signals and output are the compiler's, and a build through smith is byte-identical to one through the compiler directly.

## Which compiler runs

- Inputs that are all `.metal` files go to Apple's metal compiler: `xcrun -sdk macosx metal`, or `$SMITH_METAL`.
- Everything else goes to the platform's best clang: Apple's on macOS (`/usr/bin/clang`, which runs through xcrun and finds the SDK), otherwise upstream clang.
- `$SMITH_CLANG` overrides the clang choice.
- Invoked as `smith++`, it uses clang++ (`$SMITH_CLANGXX`). It is the same program under another name. Only linking C++ needs it.

## Use it

1. Projects say `CC := smith`, and their shader rules use `$(CC)` too. Write flags as you would for clang.
2. `~/.zshenv` sets `CC` and `CXX` to smith and smith++, installed in `~/.local/bin`.
3. See what it does: `SMITH_VERBOSE=1` prints `smith: exec <compiler> <args>` on stderr before running. `smith --smith-which <args>` prints the compiler it would choose without running it. `smith --smith-version` prints its version.
4. For `compile_commands.json`, smith adds nothing. Pass `-MJ <fragment.json>` as with clang and merge the fragments. `~/Documents/checkers/Makefile` merges them with sed.
5. `.smith` files (C with extra syntax) are planned but not supported yet.

## When something goes wrong

1. A compiler error or warning that smith prints is clang's (or metal's), not smith's. Fix it as you would for clang.
2. A message that starts with `smith:` is smith's own:
   - `cannot compile .metal files together with other inputs in one command; split it`: run one command for the shaders and another for the rest.
   - `.smith inputs are not supported yet; milestone 0 is a passthrough`: compile the `.c` files instead.
   - `not an executable: <path>` or `refers to smith itself: <path>`: a `SMITH_*` variable is wrong. Fix or unset it.
   - `cannot find a compiler; set SMITH_CLANG or SMITH_CLANGXX, tried <tool>`: no clang is installed. Set the variable to one.
   - `cannot find xcrun`: the Xcode command line tools are missing, so metal cannot run.
   - `cannot execute <program>`: the chosen program exists but would not start. Run it by hand.
   The wording is in `~/Documents/smith/smith.c`.
3. If the installed smith is broken, reinstall it: `make -C ~/Documents/smith install CC=/usr/bin/clang`. It runs smith's tests first, then copies smith and the smith++ link into `~/.local/bin`.

## Rules

- In checkers, learner and game the owner decided that smith is the compiler. Never replace it with `xcrun clang`.
- If smith seems wrong, run the command again with `SMITH_VERBOSE=1`. Report the command and its output to the owner. Do not work around it.
