---
name: smith-build-smith
description: "build.smith: the build file written in .smith with import smith::build and build::app, module names for the build API. Load when a project has a build.smith or you write one."
---

# build.smith

A root `build.smith` is the build file when there is no `build.c` (both is an error naming both; smith's own repository stays `build.c`). The full smith compiles it as a `.smith` file; the stage0 refuses it (`build.smith needs the full smith`). The verbs, `smith lint` included (it lints the file as written), work on it as on a build.c. It is `module build;` and defines `export void build(build::Build *b)`, which keeps the C name `build` that smith's build program calls (a file with no `module` line and a plain `void build(...)` still works, but `smith lint` suggests the module line). Inside `module build;` the import `build` wins when written with `::`: `build::app` is the API's, and the module's own `build` is written unqualified:

```smith
module build;

import smith::build;

export void build(build::Build *b) {
    build::app(b, &(build::App){.entry = "main.smith"});
    build::app_tests(b, &(build::AppTests){});
    build::task(b, "greet", &(build::Step){.command = build::list(b, "echo", "hi", nullptr)});
}
```

A `build::Step` is a C struct, so it is written as a declaration (`build::Step step = {...};`) or a compound literal (`(build::Step){...}`); there is no `build::Step{...}` and no `auto step = build::Step{...}`. `build::step` and `build::task` take the step's address and replace its lists with their own copies, so one `Step` is never passed to two calls. A shape you want twice is a function that returns a new `Step` from a designated initializer on each call:

```smith fragment
module build;

import smith::build;

// One shape, a new Step on every call.
build::Step greet(build::Build *b, const char *word) {
    return (build::Step){.command = build::list(b, "echo", word, nullptr)};
}

export void build(build::Build *b) {
    build::Step app = {
        .command = build::list(b, "smith-cc", "-std=gnu2y", "$in", "-o", "$out", nullptr),
        .inputs = build::list(b, "main.smith", nullptr),
        .outputs = build::list(b, "app", nullptr),
    };
    build::step(b, &app); // app.outputs now points at the build's own copy: later steps and defaults can use it
    build::Step hello = greet(b, "hello");
    build::task(b, "hello", &hello); // `smith build hello`
    build::Step goodbye = greet(b, "goodbye");
    build::task(b, "goodbye", &goodbye);
    build::default_targets(b, app.outputs);
}
```

`smith::build` is the API of this skill with module names: `SmithX` is `build::X` (`Build`, `Step`, `Run`, `Object`, `Program`, `Modules`, `Tests`, `Package`, `App`, `AppTests`) and `smith_x` is `build::x` (`build::app`, `build::object`, `build::program`, `build::tests`, `build::task`, `build::list`, `build::glob`, `build::fmt`, `build::run_lines`...), except `smith_default`, which is `build::default_targets`. They are the same functions a build.c calls (the module's header is `include/smith/build.h`), so everything below applies; read `smith_x` as `build::x`.

Also in this family: [smith-build](../smith-build/SKILL.md), [smith-build-c](../smith-build-c/SKILL.md), [smith-build-functions](../smith-build-functions/SKILL.md), [smith-build-objects](../smith-build-objects/SKILL.md), [smith-build-makefile](../smith-build-makefile/SKILL.md).
