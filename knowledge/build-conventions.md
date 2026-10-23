---
type: convention
title: Builds by convention, smith_app and smith_app_tests
description: A project that follows smith's conventions needs no build file, and smith_app and smith_app_tests say what the conventions do.
tags: [build, conventions]
---
# Builds by convention, smith_app and smith_app_tests

A third layer sits above the high level, and a project that follows its conventions needs no build file (issue #43).
When the root has no `build.c` but has a `main.smith` or `main.c`, `smith build` and every verb (`run`, `test`, `lint`,
`prove`, `check`) run an embedded build program, `.smith/conventions.c`, which is nothing but the two calls below with
their defaults. It is compiled and run like any build.c (in `.smith/`, by the stage0 too, since a C project needs no
`.smith` compiler), and writes `.smith/build.info` and `.smith/compile_commands.json` as any build does, so the verbs
cannot tell it from a hand-written file. A hand-written build.c that calls the same two calls, or their expansions,
builds the same commands.

```c
#include <smith/build.h>

void build(SmithBuild *b) {
    smith_app(b, &(SmithApp){});
    smith_app_tests(b, &(SmithAppTests){});
}
```

**The conventions.**
- **Entry point:** `main.smith` or `main.c` at the root (both is an error). The program is named after the folder and
  built to `build/<profile>/<name>`; each source's object is `build/<profile>/<source without its extension>.o`.
- **`.smith`:** the program is `main.smith` and every module it imports, transitively. An `import a::b;` is found at
  `a/b.smith` under the root of the importing module, as the compiler finds an import's source (for `module main;` in
  `main.smith` the root is the project's, and for `geometry::box` in `geometry/box.smith` it is too). A module
  nothing imports is not compiled, and its header is not written.
- **C and Objective-C:** every quoted `#include "foo.h"` (or `#import`), transitively from `main.c`, is resolved as the
  compiler resolves it (the including file's folder, then the project's root, which is `-I .`, then the folders of the
  `-I` and `-iquote` flags of `smith_defaults` and the program's `.flags`: with `-I src`, `"base/math.h"` is
  `src/base/math.h`); the `foo.c` beside `foo.h` (else the `foo.m`) is compiled, and `foo.h` is read in turn. Includes in
  `.smith` files (`#include "x.h"`, `import "x.h";`) follow the same rule.
- **What the scan misses.** It is lexical, line by line, with `//` and `/* */` comments dropped. A conditional include
  is followed anyway (harmless: the compiler decides), an include built by a macro is missed, a `.c` file nothing
  includes by its header's name is not compiled, an include of a `.c` file (a unity build) is read and not compiled
  again, and a C file that includes a `.smith` module's header does not make that module part of the program. A project
  with such a file writes a build file, or renames to `.smith`.
- **smith's libraries** need no declaration: `import smith::text;` or `import smith::io;` (`#include <smith/io.h>` in C)
  compiles the library with the profile's flags and links it, and so does what it needs (io needs the arena
  and random: its header and source are scanned the same way). `<smith/test.h>` needs no mention; `smith_tests` builds the test library.
  The same resolver serves `smith_library`: `smith_program` links, beside the objects it is given, the libraries that
  the libraries among them need (found by scanning their header and source, `smith_library_needs`), so naming
  `smith_library(b, "contract")` is enough (trace comes with it) and so is `"io"` (arena and random; arena brings contract, math and trace).
  **A library may declare compile flags** (`SMITH_LIBRARY_FLAGS` in smith.c: `-ffp-contract=off` for random): smith
  writes them beside the source (`.smith/lib/smith/random.flags`, one flag a line), and every compile of that source
  (`smith_object`, `smith_compile_command` for the compile database) adds them after the project's own flags, so a
  project that compiles the library through `smith_library` with its own flags still gets them. A needed library
  is the object the project made of that source (any `smith_object` of it), else one compiled with the flags and
  compiler of the library that needs it, as `<folder of its object>/<name>.o`.
- **Tests:** every `.smith` file and every `test_*.c` below `tests/`, built with the debug flags, and every other `.c`
  directly in `tests/` is a helper, compiled and linked into the runner. The code under test is what the tests and the
  helpers include or import (and the libraries they use), compiled once in the debug profile: an object a program made
  of the same source with the same flags is the same object. No test file means no task.
- **Packages** have no convention (`smith_package` takes a URL and a hash): a project that needs one writes a build
  file.
- **A folder that follows no convention** (no `build.c`, no `main.smith`, no `main.c`) is an error that says what
  `smith build` looked for and how to start a build file, and when a Makefile is beside it, that `CC = smith-cc` builds it
  unchanged. It never guesses. A root `build.smith` is a build file like `build.c` (below).

**The profiles.** `debug` is the default target; `release` and `fast` are aliases (`smith build release`, `smith build
fast`). The flags are those `smith new` used to write into a build.c, moved into smith:

| | flags |
|---|---|
| all three | `-std=gnu2y -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wimplicit-fallthrough -Wmissing-prototypes -Wstrict-prototypes -Wvla -Werror`, `-ffp-contract=off`, `-mcpu=apple-m1` on Darwin arm64, `-I . -I .smith/include`, then the project's `.flags`; Clang modules (`smith_modules` with no folders) |
| debug | `-g -O0 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -ftrivial-auto-var-init=pattern`; the link has `-fsanitize=address,undefined` |
| release | `-O2 -flto=thin` (assertions on); the link has `-flto=thin` |
| fast | `-O2 -DNDEBUG -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function -flto=thin` (a variable only an assert reads is unused once the asserts are gone); the link has `-flto=thin` |

ThinLTO is how a project gets inlining across files without combining them into one translation unit (smith has no
unity builds: Clang modules and their shared cache already parse each header once). On game, 12 duels per pairing of
its balance tool on one core ran in 30.3 s with ThinLTO against 42.4 s without, with the same results; see the
measurements in the commit that added it.

## `smith_app`: the program

```c
typedef struct {
    const char *entry;                 // "main.smith" or "main.c"; by default the one the root holds
    const char *name;                  // the program's name; by default the project's folder
    const char *const *flags;          // added to every profile's compiles, after the profile's own and the defaults'
    const char *const *link_flags;     // added to every profile's link
    const char *const *libraries;      // after the objects: -lm, -framework Metal
    bool tool;                         // not built by default: `smith build tools`, release and fast build it
    const SmithObject *const *objects; // set: the debug objects of everything but the entry
    SmithStep step;                    // set: the debug program
} SmithApp;
void smith_app(SmithBuild *b, SmithApp *app);
```

**Several programs.** A project calls `smith_app` once for each program (`.entry` is required for all but the project's
own `main`). Each finds its own sources by its includes; a source two programs share, and a smith library, is compiled
once in each profile (`build/<profile>/<source without its extension>.o`) and linked into each program that reaches it.
Two programs may compile a source only with the same flags: when they would not, `smith_app` stops and names the source.
`smith build` builds the debug program of every call that is not a `.tool`, so `smith run` has the one to run when the
project has one program that is not; a `.tool` is built by `smith build tools` (its debug programs), by `release` and
`fast`, and as the file (`smith build build/debug/<name>`). No alias is made of a program's name, so a task may carry it
(`smith build report` runs `build/release/report`).

**`smith_defaults`: what every program and test shares.** Called once, before the first `smith_app`:

```c
typedef struct { const char *path; const char *const *flags; const char *const *inputs; } SmithFolder;
typedef struct {
    const char *const *flags;     // added to every compile of every profile, after the profile's own
    const SmithFolder *folders;   // settings for the sources below a folder; a row with a nullptr path ends the list
    const char *const *link_flags; // added to every link
} SmithDefaults;
void smith_defaults(SmithBuild *b, const SmithDefaults *defaults);
```

A folder row covers the sources below the path (or the one file) in every profile: `{"src/base", {"-O2"}}` compiles
the base layer at `-O2` even in debug, and `.inputs` are files the build makes that those sources embed (`#embed`, a
generated include): their compiles wait for them. smith's own libraries are the sources under `.smith/lib`. Flags come in
the order profile, defaults, folder rows, the program's `.flags`.

**For checks and low-level builds that start from the conventions:**
- `smith_profile_flags(b, profile)` and `smith_profile_link_flags(b, profile)`: the compile and link flags of a profile
  ("debug", "release", "fast"), with the defaults', to add one's own to.
- `smith_source_flags(b, profile, source)`: the flags the build compiles `source` with (the folder rows and the first
  program's `.flags` included), for a check that must see a source as the build does.
- `smith_sources(b, folder)` and `smith_objects(b, profile, folder)`: the sources the programs compile below `folder`
  ("" for all) and their objects in the profile, after the `smith_app` calls.

Expands to (`flags(P)` is the table above for profile P):

```c
smith_modules(b, &(SmithModules){});   // unless modules are already on or an object was already declared
// for P in debug, release, fast (debug first, so the editor reads the debug flags), for each source S found
// (S followed from the entry), and each library L used (smith_library(b, "L"); its object is build/P/smith/L.o):
smith_object(b, &(SmithObject){.compiler = "smith-cc", .source = S,
                               .output = "build/P/<S without its extension>.o", .flags = flags(P)});
smith_program(b, &(SmithProgram){.linker = "smith-cc", .output = "build/P/<name>",
                                 .flags = <the debug link flag, for P = debug> + .link_flags,
                                 .objects = the objects above, .libraries = .libraries});
smith_alias(b, "release", release programs);   // every program adds its own
smith_alias(b, "fast", fast programs);
smith_alias(b, "tools", debug programs of the .tool ones);
smith_default(b, debug programs that are not .tool);
```

It writes no root `compile_commands.json`: the compile commands are in `.smith/` on every build, and a project that wants
the root file for another tool calls `smith_compile_commands(b, "compile_commands.json")` after these calls.

## `smith_app_tests`: the tests

```c
typedef struct {
    const char *folder;            // by default "tests"
    const char *const *inputs;     // files the tests read when they run, such as compiled shaders; built first
    const char *const *link_flags; // added to the runner's link, such as -framework Metal
    SmithStep step;                // set: the task test
} SmithAppTests;
void smith_app_tests(SmithBuild *b, SmithAppTests *tests);
```

Call it after `smith_app`. Expands to:

```c
smith_tests(b, &(SmithTests){
    .sources = smith_glob(b, {"<folder>/**/*.smith", "<folder>/**/test_*.c"}),
    .objects = what the sources and the helpers (the other .c directly in the folder) include or import, and the
               libraries they use, each compiled with the debug flags as build/debug/<source>.o,
    .flags = flags(debug), .inputs = .inputs, .link_flags = <the debug link flag> + defaults + .link_flags});
```

The debug objects of the program are shared with the tests (one compile of each source). The tests' libraries that
`smith_tests` compiles itself (arena, random, trace, contract) are skipped when the program already links them.

`smith build --dump-config` prints exactly this program, with the defaults as fields (below), and a file that has it
builds the same commands as no file.
