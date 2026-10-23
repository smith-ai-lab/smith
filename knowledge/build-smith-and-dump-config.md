---
type: design
title: build.smith, the module smith::build, and --dump-config
description: build.smith is a build file in .smith over the module smith::build, and --dump-config writes the build file the conventions imply.
tags: [build, dump-config]
---
# build.smith, the module smith::build, and --dump-config

**`build.smith`.** A project's root may hold `build.smith` instead of `build.c` (issue #43). It is the same build file
in `.smith`: `smith build` compiles it as a `.smith` file (C2y, `smith cc build.smith`, through the same bootstrap
manifest as a build.c and the embedded `.smith/conventions.c`) and links it with the same runtime, so the verbs (`run`,
`test`, `lint`, `prove`, `check`, `docs`) work on it exactly as on a build.c. It is `module build;`, and the
function the runtime calls is `export void build(build::Build *b)`: in a module named `build` that one export keeps the
C name `build` (no `[[smith::c_name]]` needed), and the import `build` wins over the module's own name when written
with `::` (`build::app` is the API's, the module's `build` is written unqualified). A file with no `module` line and a
plain `void build(...)` still builds. `import smith::build;` gives the API:

```c
module build;

import smith::build;

export void build(build::Build *b) {
    build::app(b, &(build::App){.entry = "main.smith"});
    build::app_tests(b, &(build::AppTests){.folder = "tests"});
    build::task(b, "greet", &(build::Step){.command = build::list(b, "echo", "hello", nullptr)});
}
```

- **Both `build.c` and `build.smith`** is an error that names both. smith's own repository stays a `build.c`, so a fresh
  clone builds with a plain `cc`.
- **The stage0 refuses `build.smith`** (`build.smith needs the full smith`): it has no clang inside, so it has no token
  hook. A C project by convention, or with a build.c, still builds with it.
- **`smith lint` lints it as written**, like a build.c: the build file's compile is in `.smith/build.info` and
  `.smith/compile_commands.json` (`file` "build.smith", `arguments` `smith-cc -g -O0 -Wall -Wextra -I .smith/include -c
  build.smith`).

**`smith::build` is the same API with module names.** The header `<smith/build.h>` is generated from the module
`lib/smith/build.smith` (committed, like `random.h` and `text.h`; `smith build headers` regenerates it). Every call there
has no body: the build runtime (`runtime/build_runtime.c`, C) defines it under its C name (`[[smith::c_name("smith_task")]]`),
so a build.smith and a build.c call the very same functions and nothing is implemented twice. The C names are unchanged;
the module names drop the `smith_` and the `Smith`:

| C | `import smith::build;` |
|---|---|
| `SmithBuild`, `SmithStep`, `SmithRun` | `build::Build`, `build::Step`, `build::Run` |
| `SmithObject`, `SmithProgram`, `SmithModules`, `SmithTests`, `SmithPackage` | `build::Object`, `build::Program`, `build::Modules`, `build::Tests`, `build::Package` |
| `SmithApp`, `SmithAppTests` | `build::App`, `build::AppTests` |
| `smith_app`, `smith_app_tests` | `build::app`, `build::app_tests` |
| `smith_object`, `smith_program`, `smith_modules`, `smith_tests` | `build::object`, `build::program`, `build::modules`, `build::tests` |
| `smith_step`, `smith_task`, `smith_alias`, `smith_pool` | `build::step`, `build::task`, `build::alias`, `build::pool` |
| `smith_default` | `build::default_targets` (`default` is a keyword) |
| `smith_option`, `smith_option_value`, `smith_args`, `smith_env` | `build::option`, `build::option_value`, `build::args`, `build::env` |
| `smith_glob`, `smith_capture`, `smith_capture_or`, `smith_write`, `smith_library`, `smith_package` | `build::glob`, `build::capture`, `build::capture_or`, `build::write`, `build::library`, `build::package` |
| `smith_fmt`, `smith_cat`, `smith_map`, `smith_when`, `smith_join`, `smith_list`, `smith_replace_extension` | `build::fmt`, `build::cat`, `build::map`, `build::when`, `build::join`, `build::list`, `build::replace_extension` |
| `smith_defer`, `smith_lines`, `smith_compile_command`, `smith_compile_commands` | `build::defer`, `build::lines`, `build::compile_command`, `build::compile_commands` |
| `smith_run_lines`, `smith_run_status`, `smith_run_combined`, `smith_run_depend`, `smith_run_program` | `build::run_lines`, `build::run_status`, `build::run_combined`, `build::run_depend`, `build::run_program` |
| `build` (what the file defines) | `build::build` (declared; a build.smith defines `build`) |

Everything below this section is written with the C names; read `smith_x` as `build::x`. A `.run` function is a
function of the build.smith, taking a `build::Run *`. Fields are the same (`.entry`, `.flags`, `.output`...), and a list
is `build::list(b, "a", "b", nullptr)` or a compound literal `(const char *const[]){"a", nullptr}`.

**`smith build --dump-config`** prints the build file that does exactly what the conventions do, in the highest-level
API and the language of the entry point, with the defaults spelled out as fields, on stdout:

```
$ smith build --dump-config > build.smith     # a main.smith project
// build.smith: what `smith build` does with no build file (knowledge/build-conventions.md), written out by
// `smith build --dump-config`. Change a field, or replace a call with the low-level calls it expands to.
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

A `main.c` project gets the same as a build.c through `<smith/build.h>` (`smith_app`, `.entry = "main.c"`). Building with
the dumped file gives the same commands, for every target, as building with no file (`tests/build-smith.sh` compares
`smith build -t commands` for `.smith` and C). A project that already has a build file is already explicit: the
command says so on stderr, prints nothing to stdout and exits 0. A folder that follows no convention is the same error as
`smith build`. (A folder with a Makefile and no build file, with or without a main, is the Makefile translator's, issue #44: the command
prints the translation, and `smith build` and the verbs stop with a message that says to translate it or delete it, never
building by convention behind it. An empty `build.c` or `build.smith` counts as none, since the redirect makes it first.) Everything else the
command says goes to stderr, so the redirect makes the file.
