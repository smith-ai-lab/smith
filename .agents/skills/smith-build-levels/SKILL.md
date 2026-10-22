---
name: smith-build-levels
description: "The three levels of a smith build and how to move between them: no build file (main.smith or main.c, tests/, the debug/release/fast profiles), smith build --dump-config > build.smith (or build.c) to make the conventions explicit, editing smith_app / build::app fields (.flags, .link_flags, .libraries, .entry, .name), adding a task or step beside them, then replacing smith_app with smith_object, smith_program and smith_tests. Load when the conventions are not enough, to 'add a flag', 'link a library', 'add a build step or task', 'go outside the conventions', 'write a build.smith', or when choosing between the high-level and the low-level build API."
---

# Build levels: conventions, high level, low level

This skill ships in the smith plugin, from `.agents/skills/smith-build-levels/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. The reference for every call is `smith-build`; this skill is the path through it. Every rung gives the same commands as the one above until you change something, so check with `smith build -t commands <target>` after each move.

## Level 0: no build file

A folder with `main.smith` or `main.c` at the root (never both) and no `build.c` or `build.smith` builds, runs and tests as it is. `smith new [--language c] [name]` writes such a project. Nothing to write; read `.smith/conventions.c` to see what ran.

- Debug is the default (`smith build`, `smith run`): `build/debug/<folder name>`, `-g -O0`, address and undefined sanitizers, assertions on. `smith build release`: `build/release/`, `-O2`, assertions on. `smith build fast`: `build/fast/`, `-O2 -DNDEBUG`. All three: `-std=gnu2y` with strict warnings and `-Werror`, `-ffp-contract=off`, `-mcpu=apple-m1` on Apple silicon, Clang modules.
- What is compiled is what `main` reaches: `.smith` by `import a::b;` to `a/b.smith`, C by quoted `#include "foo.h"` to the `foo.c` beside it. smith's libraries need no declaration: `#include <smith/io.h>` or `import smith::text;` compiles and links them.
- Tests: every `.smith` file and `test_*.c` under `tests/`, run by `smith test`.
- It does not cover: packages, a source that no header names, other flags or libraries (`-lm`, `-framework`), extra tasks, shaders. Those need level 1 or 2.

## Level 1: the high-level API, written out

```sh
smith build --dump-config > build.smith      # a main.smith project; build.c for a main.c project
```

It prints exactly what level 0 does and changes nothing (on a project that has a build file it says so on stderr and prints nothing):

```c
// build.smith
import smith::build;

void build(build::Build *b) {
    build::app(b, &(build::App){
        .entry = "main.smith",
        .name = "hello",
    });
    build::app_tests(b, &(build::AppTests){.folder = "tests"});
}
```

(`build.c` is the same with `#include <smith/build.h>`, `SmithBuild`, `smith_app(b, &(SmithApp){...})`, `smith_app_tests(b, &(SmithAppTests){...})`.) A `build.smith` has no `module` line and defines `void build(build::Build *b)`; `build::X` is `SmithX` and `build::x` is `smith_x`. It needs the full smith.

Edit the fields; each applies to all three profiles:

| field of `App` | meaning |
|---|---|
| `.entry`, `.name` | the entry file; the program's name (`build/<profile>/<name>`) |
| `.flags` | added to every compile, after the profile's own (`-DX=1`, `-I vendor`, `-Wno-...`) |
| `.link_flags` | added to every link |
| `.libraries` | after the objects: `-lm`, `-framework Metal` |

`build::AppTests` has `.folder`. A list ends in `nullptr`: `build::list(b, "-DX=1", "-O3", nullptr)`.

Add a task or a step beside the calls, in the same function (this does not leave the level):

```c
build::task(b, "greet", &(build::Step){.command = build::list(b, "echo", "hi", nullptr)});   // smith build greet
```

A task may not be named after a verb (`run`, `test`, `check`, `fmt`, `lint`, `docs`, `prove`). Packages, options (`build::option_value`), `build::glob`, `build::env` and `build::capture` are all available beside `build::app`: they are the same functions as `smith_x` (`smith-build`). `smith build --dump-config` prints the same file for a C project, where the calls are `smith_app` and `smith_task`. `smith_app` makes the aliases `release` and `fast` and the default target itself, so a build file that calls it declares no `smith_default`. Call `smith_compile_commands(b, "compile_commands.json")` after it if another tool wants a root file.

## Level 2: the low-level API, replacing one call by its expansion

When you need one program with other flags than another, a source outside the scan, a generated file, or a step between compile and link, replace `build::app` (or `smith_app`) with what it expands to. `docs/build-c.md` ("`smith_app`: the program") gives the expansion; for a program of two modules in `build.smith`:

```c
import smith::build;

void build(build::Build *b) {
    auto flags = build::list(b, "-std=gnu2y", "-Wall", "-Wextra", "-Werror", "-I", ".", "-I", ".smith/include", "-g", "-O0",
                             "-fsanitize=address,undefined", nullptr);
    build::Object primes = {.compiler = "smith-cc", .source = "primes.smith", .output = "build/primes.o", .flags = flags};
    build::object(b, &primes);                  // an object before the program that links it
    build::Object main = {.compiler = "smith-cc", .source = "main.smith", .output = "build/main.o", .flags = flags};
    build::object(b, &main);
    build::Program program = {
        .linker = "smith-cc",
        .output = "build/hello",
        .flags = build::list(b, "-fsanitize=address,undefined", nullptr),
        .objects = (const build::Object *[]){&main, &primes, nullptr},
    };
    build::program(b, &program);
    build::default_targets(b, program.step.outputs);   // required: smith_default is build::default_targets
    build::task(b, "greet", &(build::Step){.command = build::list(b, "echo", "hi", nullptr)});
}
```

This builds and `smith run` runs it (tried). What you now own: the profiles (write a loop over `"debug", "release", "fast"` if you want them), the aliases, `smith_modules` if you want Clang modules, the libraries you use (`build::library(b, "text")` is a path you compile with `build::object`), and the tests: replace `build::app_tests` with `build::tests(b, &(build::Tests){.sources = build::glob(b, build::list(b, "tests/**/*.smith", nullptr)), .objects = ..., .flags = flags})` (`smith-test`). `smith run` runs the one program `default_targets` names; `smith test` runs what `tests` declares.

## Beyond: the rest of the low level

Raw steps (`smith_step`: argv with `$in`/`$out`), `.run = fn` steps (C functions that check build outputs), `smith_defer` (read a generated file's lines to declare more steps), `smith_package` (a pinned archive, load `smith-packages`), options with `-D`, and `.stamp`/`.depfile`/`.restat`: all in `smith-build`. Shaders: `smith-metal`.

## Choosing

Stay at the highest level that does the job: a change of flags is level 1 (a field), not level 2. Move one call down only for the part that needs control. Dump the file before editing so the diff shows what you changed, and compare `smith build -t commands <target>` before and after.
