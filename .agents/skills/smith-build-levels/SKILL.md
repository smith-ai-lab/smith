---
name: smith-build-levels
description: "The three levels of a smith build and how to move between them: no build file, the high-level API written out, the low-level API. Load when conventions are not enough, to 'go outside the conventions', or when choosing between the high-level and low-level API."
---

# Build levels: conventions, high level, low level

## Level 0: no build file

A folder with `main.smith` or `main.c` at the root (never both) and no `build.c` or `build.smith` builds, runs and tests as it is. `smith new [--language c] [name]` writes such a project. Nothing to write; read `.smith/conventions.c` to see what ran.

- A program that imports `smith::autodiff` (or includes `<smith/autodiff.h>`) gets other flags in every profile: `-flto=full`, `-Xclang -disable-llvm-passes` and, in debug, AddressSanitizer but no UBSan (smith-autodiff). The low-level calls (`build::profile_flags`, `build::source_flags`) return the flags of a program that does not differentiate.
- Debug is the default (`smith build`, `smith run`): `build/debug/<folder name>`, `-g -O0`, address and undefined sanitizers. `smith build release`: `build/release/`, `-O2`. `smith build fast`: `build/fast/`, `-O2 -DNDEBUG`. All three: `-std=gnu2y` with strict warnings and `-Werror`, `-ffp-contract=off`, `-mcpu=apple-m1` on Apple silicon, Clang modules.
- What is compiled is what `main` reaches: `.smith` by `import a::b;` to `a/b.smith`, C by quoted `#include "foo.h"` to the `foo.c` beside it. smith's libraries need no declaration: `#include <smith/io.h>` or `import smith::io;` compiles and links them.
- Tests: every `*_test.smith` and `*_test.c` file below the root, run by `smith test`.
- It does not cover: packages, a source that no header names, other flags or libraries (`-lm`, `-framework`), extra tasks, shaders. Those need level 1 or 2.

## Beyond: the rest of the low level

Raw steps (`smith_step`: argv with `$in`/`$out`), `.run = fn` steps (C functions that check build outputs), `smith_defer` (read a generated file's lines to declare more steps), `smith_package` (a pinned archive, load `smith-packages`), options with `-D`, and `.stamp`/`.depfile`/`.restat`: all in `smith-build`. Shaders: `smith-metal`.

## Choosing

Stay at the highest level that does the job: a change of flags is level 1 (a field), not level 2. Move one call down only for the part that needs control. Dump the file before editing so the diff shows what you changed, and compare `smith build -t commands <target>` before and after.

Also in this family: [smith-build-levels-high](../smith-build-levels-high/SKILL.md), [smith-build-warnings](../smith-build-warnings/SKILL.md), [smith-build-levels-low](../smith-build-levels-low/SKILL.md).
