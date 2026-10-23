---
type: design
title: How smith build builds .smith sources
description: A header step per module comes first and every .smith compile waits for all of them.
tags: [build, smith-lang]
---
# How smith build builds .smith sources

A source that ends in `.smith` in `smith_object` is a module (its first line is `module a::b;`) or a plain `.smith`
file ([the language](smith-language.md)). Nothing else in build.c changes: the manifest gets

- for each module, a **header step**: `<compiler> --smith-header <source> -o .smith/include/a/b.h` (the compiler is the
  object's, `smith-cc`, which runs the helper in `smith cc --smith-header` without compiling; the object's `-I`, `-D`, `-std=` and similar flags follow, for the question of which of several C imports an export's name comes from). The step is `.restat`
  and the header is written only when its bytes differ, so an edit to a module's body leaves it alone. A module's
  name is read from its `module` line when build.c runs, and its path must end in `a/b.smith` (an error from
  `smith_object` otherwise, once). Two sources that declare one module are an error; one source compiled twice with
  other flags shares its header step (the first compile's flags are the ones it is given).
- one **check step** (`.smith/imports.stamp`, a `.run` step, described "check the imports of the modules") that reads
  every header's `// smith-module:` and `// smith-imports:` lines and fails, naming the cycle (`import cycle: a::x ->
  a::y -> a::x`), when the modules import each other in a circle.
- every compile of a `.smith` source gets `-I .smith/include` (before the object's `.flags`) and the check's stamp as an
  order-only input, so every header is written and the imports checked before any `.smith` compile starts, and the
  compiles then run in parallel. The headers an importer read are in its depfile, so it recompiles when, and only when,
  a header it read changed: an edit to a module's body recompiles that module alone, an edit to its exports its
  importers.
- a C or C++ source that includes a module's header (`#include "game/replay.h"`) needs `-I .smith/include` in its own
  flags and the header in its `.implicit_inputs` (`.smith/include/game/replay.h`), as for any generated header.
- `compile_commands.json` lists a `.smith` source with its flags, `-I .smith/include` included, and `-x c -std=gnu2y`
  (unless the flags name a standard) after the compiler (not in the manifest: `smith-cc` adds both itself, and clangd,
  which reads this file, knows neither the extension nor smith's standard).

A stage0 (no clang inside, so no token hook) still writes the headers; the compile of a `.smith` source fails there,
saying the compiler needs the token hook. `tests/build.sh` section 15 builds three modules and a C file, and checks the
rebuilds, a cycle, a path that does not match, a plain file and the helpers.
