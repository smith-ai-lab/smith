---
name: smith-cc
description: "Compiling with smith: smith cc, smith c++, the smith-cc and smith-c++ links, CC := smith-cc, compile_commands.json, .metal shaders, the SDK, SMITH_CLANG, SMITH_VERBOSE, --smith-which. Load for a compile or link error from smith, 'is not a smith command', 'refers to smith itself', 'cannot find clang', or a Makefile or CMake that names the compiler."
---

# Compile with smith

This skill ships in the smith plugin, from `.agents/skills/smith-cc/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

`smith cc <clang arguments>` is a drop-in for clang: flags pass through unchanged (none added, removed or reordered), and the exit status, signals, output and files are clang's. `smith c++` is the same for C++ and is what links C++. Clang is linked into smith, so it compiles in its own process. `smith foo.c -o foo` is an error that says to use `smith cc foo.c -o foo`.

## One program path

Where a command cannot take an argument, use the links that act as the verbs by their name: `smith-cc` and `smith-c++` (`smith build install` puts them beside smith). `CC := smith-cc` in a Makefile, CMake's `CMAKE_C_COMPILER`, and the first word of a `compile_commands.json` command (clangd reads it as a compiler). A `build.c` names them too: `.compiler = "smith-cc"`; `"smith"`, or a path that ends in `/smith` (`CC=$HOME/.local/bin/smith` in the environment, `-DCC=`), is an error before anything runs, with the hint to use `smith-cc` or `smith-c++`. `smith++` is gone.

## Which compiler runs

- Inputs that are all `.metal` go to Apple's metal compiler (`xcrun -sdk macosx metal`, or `$SMITH_METAL`); everything else to clang. `.metal` mixed with other inputs is refused: compile shaders in their own command, with their own flags.
- `$SMITH_CLANG` (`$SMITH_CLANGXX` for `smith c++`) runs an external clang instead. A stage0 smith (built by one `cc` command, no clang inside) runs Homebrew's `/opt/homebrew/opt/llvm/bin/clang`.
- On macOS smith picks the SDK through the environment, never a flag: for each variable you have not set it sets `CLANG_NO_DEFAULT_CONFIG=1` and `SDKROOT=$(xcrun --show-sdk-path)`. Your own `SDKROOT`, `-isysroot` or `--config` wins.

## See what it does

- `smith cc --smith-header x.smith -o x.h` and `smith cc --smith-lower x.smith` are the `.smith` helpers (a module's generated header, and a file's lowering; load `smith-lang`); `smith build` runs the first itself. The top-level `smith --smith-header` is gone.
- A `.smith` input is compiled as `-x c -std=gnu2y` (smith's own language, C2y; a `-std=` of yours wins; nothing is added beside a C++ input; your own `-x c` is kept and the standard is still added, another `-x` leaves the command alone), and `smith cc main.smith foo.smith -o prog` builds a program of modules in one command (load `smith-lang`).
- `SMITH_VERBOSE=1` prints the command on stderr. `smith cc --smith-which <args>` (or `smith-cc --smith-which`) prints the compiler it would choose: `clang in-process <path>` or `clang <path>`. `smith version` prints the version and, after `commit`, the commit it was built from (a smith built with one `cc` command has none).
- For `compile_commands.json` smith adds nothing: pass `-MJ fragments/<file>.json` as with clang, then merge: `{ echo '['; sed '$ s/,$//' fragments/*.json; echo ']'; } > compile_commands.json`. A `build.c` writes it with `smith_compile_commands`.

## Errors

A compiler error or warning is clang's, not smith's: fix it as for clang. Messages that start with `smith:`:
- `<arg> is not a smith command; to compile, run` `smith cc ...`: add `cc`, or fix `CC`.
- `.metal and other inputs need different compilers...`: split the command.
- `not an executable: <path>`, `refers to smith itself: <path>`: a `SMITH_*` variable is wrong; fix or unset it.
- `cannot find clang in /opt/homebrew/opt/llvm/bin`, `cannot find a compiler; set SMITH_CLANG`: a stage0 needs `brew install llvm` or `SMITH_CLANG`.
- `cannot find xcrun`: install the Xcode command line tools (metal needs them). `cannot execute <program>`: run it by hand.

In a project whose Makefile uses smith, keep it. Report anything odd; do not reinstall smith.
