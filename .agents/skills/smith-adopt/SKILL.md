---
name: smith-adopt
description: "Adopting smith in an existing C project, step by step: CC=smith-cc in a Makefile or CMake, translating the Makefile with smith build --dump-config > build.c, adding tests, contracts and smith prove, then renaming .c files to .smith one module at a time. Load when asked to 'use smith', 'switch to smith', 'migrate', 'port' or 'adopt smith' in a project that has a Makefile, CMakeLists.txt or plain .c files, or on 'this folder has a Makefile and no build.c or build.smith'."
---

# Adopt smith in an existing C project

This skill ships in the smith plugin, from `.agents/skills/smith-adopt/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

Every step is optional and leaves the project building. Stop at any rung; do not do the next one unless asked. Check after each step that the program still builds and its tests pass, and commit it by itself (small Conventional Commits). smith changes no default of the project: `smith cc` is clang with the arguments untouched, and a build file only appears when you write it.

Run `smith version` first. If smith is missing from PATH, stop and tell the owner; never swap in another compiler.

## 1. Use smith as the compiler (nothing else changes)

```sh
CC=smith-cc make                              # Makefile with CC ?= cc / CC = cc
cmake -DCMAKE_C_COMPILER=smith-cc -DCMAKE_CXX_COMPILER=smith-c++ ...   # CMake
```

- `smith-cc` and `smith-c++` are links to smith that act as `smith cc` and `smith c++`. For C, C++ and Objective-C it is a passthrough: no flag is added, removed or reordered, the exit status and diagnostics are clang's, so the build cannot change. Diff `make -n` before and after, and the object files if the project cares.
- If the Makefile hard-codes `CC = gcc`, change that line to `CC = smith-cc` (or `CC ?= smith-cc`). A `.metal` file in the build goes to Apple's metal compiler (load `smith-metal`). Errors from smith itself start with `smith:` (load `smith-cc`).
- A project that wants an editor: `compile_commands.json` from the build (`-MJ`, or step 2 writes it) and `smith lsp` (load `smith-lsp`).

## 2. Translate the Makefile into a build.c

```sh
smith build --dump-config > build.c           # in the folder of the Makefile; also GNUmakefile / makefile
smith build                                   # builds exactly what make built
smith build -t commands build/app             # the compile and link commands, to diff against `make -n -B`
```

- Until you do this, a folder with a Makefile and no build file does not build: `smith build` and the verbs stop with `this folder has a Makefile and no build.c or build.smith` (they never build by convention behind a Makefile, even beside a `main.c`).
- **Read the top of build.c.** It lists the options the Makefile read from the command line (`make CFLAGS=...` is `smith build -DCFLAGS=...`) and, under `NOT TRANSLATED`, every construct it did not follow, with the Makefile line (also on stderr as `smith: Makefile:12: not translated: ...`). Do those by hand. `$(shell ...)` is run once during translation and frozen.
- The result is the low-level API (`smith_object`, `smith_program`, `smith_task`...). Edit it like any build.c (load `smith-build`; the translation's rules are in its "Replace a Makefile" section).
- Compare before deleting the Makefile: build with both from clean, check the compile and link commands match (apart from the compiler's name and the dependency flags) and the outputs match. `make test` becomes the task `make-test` (a build.c may not name a task after a verb); a plain run of the program is `smith run`.
- `smith build` runs independent steps in parallel (`-j1` gives make's order). `smith build -t clean` replaces `make clean`.
- Then delete the Makefile (or keep it one release as a fallback). Commit `build.c`; `.smith/` ignores itself.

## 3. Format, lint, check

`smith fmt` and `smith lint` need no config file: smith's `.clang-format` and `.clang-tidy` apply where the project has none. Run `smith fmt --dump-config > .clang-format` and `smith lint --dump-config > .clang-tidy` to make them the project's and edit what you disagree with (loading `smith-check`). The translated build.c is linted and formatted too and passes both (`CFLAGS` is `build_cflags`; the file is written formatted, and as a function for each run of targets when it is long), except a single recipe or list longer than 68 lines, which `readability-function-size` reports: shorten it by hand. `smith check` runs fmt, docs, lint, test and prove and reports every part.

## 4. Tests

Write `tests/test_<name>.c` files with `void test_<name>(void) {` (and `void test_x(SmithKey key)` for a property test), then in build.c, after the objects of the code under test:

```c
smith_tests(b, &(SmithTests){
    .sources = smith_glob(b, smith_list(b, "tests/test_*.c", nullptr)),
    .objects = (const SmithObject *[]){&object_util, nullptr},   // the code under test, without main's object
    .flags = flags,                                              // the project's flags + -I .smith/include
});
```

`smith test` runs them; `smith test --test name --seed 7` replays one (load `smith-test` and `smith-run-test`). If the Makefile had a `test` target, keep it as the task `make-test` until `smith test` covers it.

## 5. Contracts and proofs

For code whose correctness matters (parsers, move generators, bounds), write `smith_requires`, `smith_ensures` and `smith_invariant` (`#include <smith/contract.h>`) and run `smith prove` (load `smith-contract`). In build.c the program links smith's contract library (trace comes with it), and its compiles take `-I .smith/include`:

```c
const char *const *flags = smith_cat(b, project_flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr);
SmithObject contract = {.compiler = "smith-cc", .source = smith_library(b, "contract"), .output = "build/contract.o", .flags = flags};
smith_object(b, &contract);                 // a failed contract calls smith_failure, which is in trace; smith_program links trace too
```

- One file: `smith prove src/util.c -- -I .smith/include` (the flags after `--` are the include paths and defines). The project form, `smith prove` with no arguments, proves the C sources of each `smith_program` together, and also proves a function that calls a function with an `smith_ensures`: `main` calling `printf` fails with `cannot be proved: a call of printf, which has neither a body nor a contract`, so prove the files whose functions have no I/O by name (`smith prove src/util.c -- ...`) and use the project form only where every caller is provable.
- A failed contract at run time prints `smith-trace` lines; `./app 2> log; smith symbolize --binary ./app log` shows the failing line and call chain (load `smith-trace`).
- Run the real thing: report the `N proved, M failed` line and, for a counterexample, the values.

## 6. Rename files to .smith, then make them idiomatic, one at a time

A `.smith` file is C: renaming `x.c` to `x.smith` changes nothing about what it means, and it is the first half of the step, never the whole of it. **Rename, then make it idiomatic (load `smith-idiom` before you touch the file):** sized types for the standard C ones, `import <h>;` and `c::` for `#include`, `arena::push(&a, T)` for `sizeof` and `alignof` passed to a function, `constexpr` for `#define`, the capability a function needs as a parameter for a raw call. Run the counts of the porting checklist in `smith-idiom` before and after; a file with a standard C type, an `#include` or a `#define` left is not done. Do one file per commit:

1. `mv src/main.c src/main.smith` (`git mv`); in build.c change that `.source` to the new name (the compiler stays `smith-cc`). `smith build`; the program is the same. Commit it as the rename. Then make it idiomatic (`smith-idiom`), build and test again, and commit that on its own.
2. A file with its own header becomes a module: `src/util.c` + `util.h` become `src/util.smith` starting `module util;`, functions you want others to call marked `export`, and `import util;` in the callers (`util::clamp(...)`, C names as `c::printf` after `import <stdio.h>;`). Remove the header. Callers still in C include the generated `.smith/include/util.h` (add `-I .smith/include` to their flags and the header to their `.implicit_inputs`). Load `smith-lang`, then `smith-lang-modules`, `smith-lang-c-interop` and `smith-lang-sized-types`; use `smith-lang-generics` where a function must work for several types or takes a type; `smith-idiom` says which of these a port needs.
3. `smith build` writes each module's header first and compiles after; a stale or cyclic import is reported before any compile (`smith: import cycle: a::x -> a::y -> a::x`).
4. `smith prove` with no arguments proves C sources only: prove a module by file, `smith prove -v src/util.smith -- -I .smith/include`, which names functions as written (`util::clamp`).
5. Tests in `.smith` are `tests/test_x.smith` files (load `smith-test`, ".smith tests"). The editor, formatter, linter and debugger all read `.smith` as written.

When every `.c` is a `.smith`, the project can drop the explicit sources and build by convention: `main.smith` at the root, modules found by `import`, and `smith build --dump-config` no longer needs a Makefile (load `smith-build-levels`).

## What not to do

- Do not add flags to `smith cc` commands "for smith"; if a command needs a change it is the project's own change.
- Do not translate by hand what `--dump-config` reports as not translated without telling the owner what you approximated.
- Do not claim a speedup or a proof you did not run.
