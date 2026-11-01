---
name: smith-build-levels-low
description: "Level 2 of a smith build: replacing one smith_app call by its expansion into smith_object, smith_program and smith_tests, starting from build::profile_flags and build::sources. Load when a program needs a step the high level has no field for."
---

# Level 2: the low-level API

When you need a step the high level has no field for (a shader, a data-baking step), add it beside the calls: `build::profile_flags(b, "debug")` and `build::source_flags` give the conventions' flags to add yours to, and `build::sources(b, folder)` / `build::objects(b, "debug", folder)` list what the programs compile. When you need one program with other flags than another, a source outside the scan, a generated file, or a step between compile and link, replace `build::app` (or `smith_app`) with what it expands to. `docs/build-c.md` ("`smith_app`: the program") gives the expansion; for a program of two modules in `build.smith`:

```smith fragment
module build;

import std::build;

export void build(build::Build *b) {
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

Also in this family: [smith-build-levels](../smith-build-levels/SKILL.md), [smith-build-levels-high](../smith-build-levels-high/SKILL.md), [smith-build-warnings](../smith-build-warnings/SKILL.md).
