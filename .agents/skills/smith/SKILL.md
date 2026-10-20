---
name: smith
description: "Using smith, the compiler driver that builds this project. Load when a Makefile says CC := smith, when a build runs smith or smith++, on any message that starts with smith:, or when asked which compiler builds the project or how to make compile_commands.json."
---

# Use smith

This skill ships in the smith plugin, from `.agents/skills/smith/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there; projects get the update through the plugin.

smith is a drop-in replacement for clang, written in C. Its home is https://github.com/adam-r-kowalski/smith, and the source is https://github.com/adam-r-kowalski/smith/blob/main/smith.c. It takes clang's flags and passes them through unchanged. Then it replaces itself with the platform's compiler (`execv`). Exit status, signals and output are the compiler's, and a build through smith is byte-identical to one through the compiler directly.

## Which compiler runs

- Inputs that are all `.metal` files go to Apple's metal compiler: `xcrun -sdk macosx metal`, or `$SMITH_METAL`.
- Everything else goes to the platform's best clang: Apple's on macOS (`/usr/bin/clang`, which runs through xcrun and finds the SDK), otherwise upstream clang.
- `$SMITH_CLANG` overrides the clang choice.
- Invoked as `smith++`, it uses clang++ (`$SMITH_CLANGXX`). It is the same program under another name. Only linking C++ needs it.

## Use it

1. A project says `CC := smith` in its Makefile, and its shader rules use `$(CC)` too. smith must be on PATH. Write flags as you would for clang.
2. See what it does: `SMITH_VERBOSE=1` prints `smith: exec <compiler> <args>` on stderr before running. `smith --smith-which <args>` prints the compiler it would choose without running it. `smith --smith-version` prints its version.
3. For `compile_commands.json`, smith adds nothing. Pass `-MJ fragments/<file>.json` as with clang, keeping the fragments
   in their own folder, then merge them:
   `{ echo '['; sed '$ s/,$//' fragments/*.json; echo ']'; } > compile_commands.json`
4. A project with a `build.c` at its root builds with `smith build`; the smith-build skill covers it.

## When something goes wrong

1. A compiler error or warning that smith prints is clang's (or metal's), not smith's. Fix it as you would for clang.
2. A message that starts with `smith:` is smith's own:
   - `.metal and other inputs need different compilers and flags; compile the shaders in their own command`: `.metal` needs Apple's metal compiler and the rest needs clang, with different flags (`-std=gnu23` and link flags against `-std=metal...`) and different outputs, and smith hands each command to exactly one compiler. Run the shaders in their own command with their own flags, as a Makefile shader rule does.
   - `not an executable: <path>` or `refers to smith itself: <path>`: a `SMITH_*` variable is wrong. Fix or unset it.
   - `cannot find a compiler; set SMITH_CLANG or SMITH_CLANGXX, tried <tool>`: no clang is installed. Set the variable to one.
   - `cannot find xcrun`: the Xcode command line tools are missing, so metal cannot run.
   - `cannot execute <program>`: the chosen program exists but would not start. Run it by hand.
   The wording is in https://github.com/adam-r-kowalski/smith/blob/main/smith.c.
3. If smith is missing from PATH or seems broken, stop and tell the owner. Installing and repairing smith is done in smith's own repository, not here.

## Rules

- In a project whose Makefile uses smith, keep it. Never swap in another compiler to get around a problem.
- If smith seems wrong, run the command again with `SMITH_VERBOSE=1`. Report the command and its output to the owner. Do not work around it, and do not reinstall it.
