---
name: smith-build-levels
description: "The three levels of a smith build and how to move between them: no build file (main.smith or main.c, tests/, the debug/release/fast profiles), smith build --dump-config > build.smith (or build.c) to make the conventions explicit, editing smith_app / build::app fields (.flags, .link_flags, .libraries, .entry, .name), adding a task or step beside them, then replacing smith_app with smith_object, smith_program and smith_tests. Load when the conventions are not enough, to 'add a flag', 'link a library', 'add a build step or task', 'go outside the conventions', 'write a build.smith', or when choosing between the high-level and the low-level build API."
---

# Build levels: conventions, high level, low level

This skill ships in the smith plugin, from `.agents/skills/smith-build-levels/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. The reference for every call is `smith-build`; this skill is the path through it. Every rung gives the same commands as the one above until you change something, so check with `smith build -t commands <target>` after each move.

## Level 0: no build file

A folder with `main.smith` or `main.c` at the root (never both) and no `build.c` or `build.smith` builds, runs and tests as it is. `smith new [--language c] [name]` writes such a project. Nothing to write; read `.smith/conventions.c` to see what ran.

- A program that imports `smith::autodiff` (or includes `<smith/autodiff.h>`) gets other flags in every profile: `-flto=full`, `-Xclang -disable-llvm-passes` and no sanitizers (smith-autodiff). The low-level calls (`build::profile_flags`, `build::source_flags`) return the flags of a program that does not differentiate.
- Debug is the default (`smith build`, `smith run`): `build/debug/<folder name>`, `-g -O0`, address and undefined sanitizers, assertions on. `smith build release`: `build/release/`, `-O2`, assertions on. `smith build fast`: `build/fast/`, `-O2 -DNDEBUG`. All three: `-std=gnu2y` with strict warnings and `-Werror`, `-ffp-contract=off`, `-mcpu=apple-m1` on Apple silicon, Clang modules.
- What is compiled is what `main` reaches: `.smith` by `import a::b;` to `a/b.smith`, C by quoted `#include "foo.h"` to the `foo.c` beside it. smith's libraries need no declaration: `#include <smith/io.h>` or `import smith::io;` compiles and links them.
- Tests: every `.smith` file and `test_*.c` under `tests/`, run by `smith test`.
- It does not cover: packages, a source that no header names, other flags or libraries (`-lm`, `-framework`), extra tasks, shaders. Those need level 1 or 2.

## Level 1: the high-level API, written out

```sh
smith build --dump-config > build.smith      # a main.smith project; build.c for a main.c project
```

It prints exactly what level 0 does and changes nothing (on a project that has a build file it says so on stderr and prints nothing):

```smith
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
| `.tool` | a program `smith build` does not build by default (`smith build tools`, `release`, `fast` do) |

A second program is a second `build::app` with its own `.entry` and `.name`; a source both reach is compiled once per profile. Flags every program and test shares, and flags for a folder, go in one call before them:

```c
build::defaults(b, &(build::Defaults){
    .flags = build::list(b, "-I", "src", nullptr),                       // "base/math.h" is src/base/math.h; the scan follows -I
    .folders = (const build::Folder[]){{"src/base", build::list(b, "-O2", nullptr)}, {}},
    .link_flags = build::list(b, "-mmacosx-version-min=26.0", nullptr),
});
build::app(b, &(build::App){.entry = "src/main.c", .name = "game"});
build::app(b, &(build::App){.entry = "tools/report.c", .name = "report", .tool = true});
build::app_tests(b, &(build::AppTests){.inputs = build::list(b, "build/meshes", nullptr)});   // helpers in tests/ are linked too
```

`build::AppTests` has `.folder`. A list ends in `nullptr`: `build::list(b, "-DX=1", "-O3", nullptr)`.

Add a task or a step beside the calls, in the same function (this does not leave the level):

```c
build::task(b, "greet", &(build::Step){.command = build::list(b, "echo", "hi", nullptr)});   // smith build greet
```

A task may not be named after a verb (`run`, `test`, `check`, `fmt`, `lint`, `docs`, `prove`). Packages, options (`build::option_value`), `build::glob`, `build::env` and `build::capture` are all available beside `build::app`: they are the same functions as `smith_x` (`smith-build`). `smith build --dump-config` prints the same file for a C project, where the calls are `smith_app` and `smith_task`. `smith_app` makes the aliases `release` and `fast` and the default target itself, so a build file that calls it declares no `smith_default`. Call `smith_compile_commands(b, "compile_commands.json")` after it if another tool wants a root file.

### Relaxing one of the conventions' warnings

The conventions compile with `-Wall -Wextra -Wpedantic ... -Werror`, then your flags, so a `-Wno-...` in `.flags` (or the folder's list) wins and the rest stay on. Turn off as little as you can: a folder's list for files that break a rule on purpose (a linter's fixtures are empty or odd translation units), the app's `.flags` for a construct the whole program uses (a float in a `static_assert`, which clang calls a GNU constant-folding extension under `-Wpedantic`). The two game needed:

```smith
import smith::build;

void build(build::Build *b) {
    build::defaults(b, &(build::Defaults){
        .folders = (const build::Folder[]){{.path = "fixtures", .flags = build::list(b, "-Wno-empty-translation-unit", nullptr)}, {}},
    });
    build::app(b, &(build::App){
        .entry = "main.smith",
        .name = "hello",
        .flags = build::list(b, "-Wno-gnu-folding-constant", nullptr),   // static_assert(0.5f + 0.5f == 1.0f)
    });
}
```

Find the flag's name in the diagnostic: clang ends it with `[-Wname]`, and `-Wno-name` turns it off. Check with `smith build -t commands <target>` that the flag is last on the files you meant, and `-Werror=name` puts one back.

## Level 2: the low-level API, replacing one call by its expansion

When you need a step the high level has no field for (a shader, a data-baking step), add it beside the calls: `build::profile_flags(b, "debug")` and `build::source_flags` give the conventions' flags to add yours to, and `build::sources(b, folder)` / `build::objects(b, "debug", folder)` list what the programs compile. When you need one program with other flags than another, a source outside the scan, a generated file, or a step between compile and link, replace `build::app` (or `smith_app`) with what it expands to. `docs/build-c.md` ("`smith_app`: the program") gives the expansion; for a program of two modules in `build.smith`:

```smith
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
