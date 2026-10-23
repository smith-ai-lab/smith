---
name: smith-adopt
description: "Adopting smith in an existing C project, step by step: CC=smith-cc, then the build file, check, tests, proofs and renaming to .smith. Load on 'use smith', 'switch to smith', 'migrate' or 'port' in a project with a Makefile, CMakeLists.txt or plain .c files."
---

# Adopt smith in an existing C project

Every step is optional and leaves the project building. Stop at any rung; do not do the next one unless asked. Check after each step that the program still builds and its tests pass, and commit it by itself (small Conventional Commits). smith changes no default of the project: `smith cc` is clang with the arguments untouched, and a build file only appears when you write it.

Run `smith version` first. If smith is missing from PATH, stop and tell the owner; never swap in another compiler.

## 1. Use smith as the compiler (nothing else changes)

```sh
CC=smith-cc make                              # Makefile with CC ?= cc / CC = cc
cmake -DCMAKE_C_COMPILER=smith-cc -DCMAKE_CXX_COMPILER=smith-c++ ...   # CMake
```

- `smith-cc` and `smith-c++` are links to smith that act as `smith cc` and `smith c++`. For C, C++ and Objective-C it is a passthrough: no flag is added, removed or reordered, the exit status and diagnostics are clang's, so the build cannot change. Diff `make -n` before and after, and the object files if the project cares.
- If the Makefile hard-codes `CC = gcc`, change that line to `CC = smith-cc` (or `CC ?= smith-cc`). A `.metal` file in the build goes to Apple's metal compiler (load `smith-metal`). Errors from smith itself start with `smith:` (load `smith-cc`).
- A project that wants an editor: `compile_commands.json` from the build (`-MJ`, or step 2 writes it) and `smith lsp` (load `smith-lsp`).

## What not to do

- Do not add flags to `smith cc` commands "for smith"; if a command needs a change it is the project's own change.
- Do not translate by hand what `--dump-config` reports as not translated without telling the owner what you approximated.
- Do not claim a speedup or a proof you did not run.

Also in this family: [smith-adopt-build](../smith-adopt-build/SKILL.md), [smith-adopt-prove](../smith-adopt-prove/SKILL.md).
