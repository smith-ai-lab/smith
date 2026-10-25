---
name: smith-adopt-build
description: "Adopting smith, steps 2 to 4: translate the Makefile with smith build --dump-config, then smith fmt, lint and check, then tests. Load when a project already compiles with smith-cc and you move its build and tests."
---

# Translate the Makefile, check, test

## 2. Translate the Makefile into a build.c

```sh
smith build --dump-config > build.c           # in the folder of the Makefile; also GNUmakefile / makefile
smith build                                   # builds exactly what make built
smith build -t commands build/app             # the compile and link commands, to diff against `make -n -B`
```

- Until you do this, a folder with a Makefile and no build file does not build: `smith build` and the verbs stop with `this folder has a Makefile and no build.c or build.smith` (they never build by convention behind a Makefile, even beside a `main.c`).
- **Read the top of build.c.** It lists the options the Makefile read from the command line (`make CFLAGS=...` is `smith build -DCFLAGS=...`) and, under `NOT TRANSLATED`, every construct it did not follow, with the Makefile line (also on stderr as `smith: Makefile:12: not translated: ...`). Do those by hand. `$(shell ...)` is run once during translation and frozen.
- The result is the low-level API (`smith_object`, `smith_program`, `smith_task`...). Edit it like any build.c (load `smith-build-c`; the translation's rules are in `smith-build-makefile`).
- Compare before deleting the Makefile: build with both from clean, check the compile and link commands match (apart from the compiler's name and the dependency flags) and the outputs match. `make test` becomes the task `make-test` (a build.c may not name a task after a verb); a plain run of the program is `smith run`.
- `smith build` runs independent steps in parallel (`-j1` gives make's order). `smith build -t clean` replaces `make clean`.
- Then delete the Makefile (or keep it one release as a fallback). Commit `build.c`; `.smith/` ignores itself.

## 3. Format, lint, check

`smith fmt` and `smith lint` need no config file: smith's `.clang-format` and `.clang-tidy` apply where the project has none. Run `smith fmt --dump-config > .clang-format` and `smith lint --dump-config > .clang-tidy` to make them the project's and edit what you disagree with (loading `smith-check`). The translated build.c is linted and formatted too and passes both (`CFLAGS` is `build_cflags`; the file is written formatted, and as a function for each run of targets when it is long), except a single recipe or list longer than 68 lines, which `readability-function-size` reports: shorten it by hand. `smith check` runs fmt, docs, lint, test and prove and reports every part.

## 4. Tests

Write `<name>_test.c` files beside the code they test with `void test_<name>(void) {` (and `void test_x(SmithKey key)` for a property test), then in build.c, after the objects of the code under test:

```c
smith_tests(b, &(SmithTests){
    .sources = smith_glob(b, smith_list(b, "**/*_test.c", nullptr)),
    .objects = (const SmithObject *[]){&object_util, nullptr},   // the code under test, without main's object
    .flags = flags,                                              // the project's flags + -I .smith/include
});
```

`smith test` runs them; `smith test --test name --seed 7` replays one (load `smith-test` and `smith-run-test`). If the Makefile had a `test` target, keep it as the task `make-test` until `smith test` covers it.

Also in this family: [smith-adopt](../smith-adopt/SKILL.md), [smith-adopt-prove](../smith-adopt-prove/SKILL.md).
