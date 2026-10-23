---
name: smith-build-levels-high
description: "Level 1 of a smith build: smith build --dump-config written out, with smith_app / build::app fields (.flags, .link_flags, .libraries, .entry, .name) and a task or step beside them. Load to add a flag, link a library, or add a build step or task."
---

# Level 1: the high-level API

```sh
smith build --dump-config > build.smith      # a main.smith project; build.c for a main.c project
```

It prints exactly what level 0 does and changes nothing (on a project that has a build file it says so on stderr and prints nothing):

```smith
// build.smith
module build;

import smith::build;

export void build(build::Build *b) {
    build::app(b, &(build::App){
        .entry = "main.smith",
        .name = "hello",
    });
    build::app_tests(b, &(build::AppTests){.folder = "tests"});
}
```

(`build.c` is the same with `#include <smith/build.h>`, `SmithBuild`, `smith_app(b, &(SmithApp){...})`, `smith_app_tests(b, &(SmithAppTests){...})`.) A `build.smith` is `module build;` and defines `export void build(build::Build *b)` (the one export of a module named `build` that keeps its plain C name `build`; a file with no module line and a plain `void build` works too); `build::X` is `SmithX` and `build::x` is `smith_x`. It needs the full smith.

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

Also in this family: [smith-build-levels](../smith-build-levels/SKILL.md), [smith-build-warnings](../smith-build-warnings/SKILL.md), [smith-build-levels-low](../smith-build-levels-low/SKILL.md).
