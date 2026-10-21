---
name: smith-development
description: "Start here for any change to smith itself: building, testing, installing and recovering it, the C rules, and where docs go. Load before editing smith.c, build.c, the tests or the docs."
---

# Develop smith

[AGENTS.md](../../../AGENTS.md) holds the rules and decisions, and GitHub issues hold the planned work. This skill holds the procedures every change shares. To use smith as a compiler, load [smith](../smith/SKILL.md) instead. To edit smith's own `build.c`, load [smith-build](../smith-build/SKILL.md) as well.

## Build, test, install

smith builds itself with its own `build.c`, so the installed smith must have `smith build`. Pass `-DCC=/opt/homebrew/opt/llvm/bin/clang` to any of these to build smith with Homebrew's clang instead of the installed smith.

## The two smiths, and how to get the full one

smith has clang inside (issue #1): it compiles in its own process, by running clang's own `clang_main` (`shim/clang.cpp`, the only C++ in smith) on the arguments. Two smiths come out of one `smith.c`:
- **The stage0** is `cc -std=gnu23 smith.c -o build/stage0`: no C++, no LLVM, and it runs an external clang (`$SMITH_CLANG`, else Homebrew's). It is all a fresh clone has.
- **The full smith** has `-DSMITH_IN_PROCESS`, the shim, and the archives of our LLVM install linked in with `-Wl,-dead_strip`. `smith build` makes it when the install has `lib/libclangmain.a` (else it makes the stage0); `-DLLVM=` names another install, empty forces the stage0.

From a fresh clone: `mkdir -p build && cc -std=gnu23 smith.c -o build/stage0`, then `build/stage0 build llvm` (builds LLVM 23.1.3 into `~/.cache/smith/llvm/23.1.3/install`, about 9 minutes cold, a few seconds when it is built), then `build/stage0 build -DCC=cc` (or `... install -DCC=cc`) links the full smith; set `SMITH_CLANG=/usr/bin/clang` if Homebrew's llvm is not installed, since the stage0 compiles build.c with an external clang. Keep a copy of the stage0 outside `build/` (it survives `smith build clean`) when you work on the full one, and rebuild with the full smith: `cp build/<OS>-<arch>/smith $S/stage1 && $S/stage1 build -DCC=$S/stage1` (a smith compiles smith in its own process).
- `scripts/llvm.sh` adds `lib/smith-clang.id` (the LLVM version and a hash of the archives linked; `smith build` compiles it into smith as the identity of its clang, which `--smith-which` prints and projects' objects depend on) and `lib/libclangmain.a` to the install: the four objects of clang's driver (`driver.cpp`, `cc1`, `cc1as`, `cc1gen-reproducer`), which hold `clang_main`. Running it again is cheap. Changing it needs `smith build llvm` again.
- The driver is told it is the clang of the install (`-no-canonical-prefixes` and the install's `bin/clang` as its path, in `shim/clang.cpp`), so builtin headers, compiler-rt, `dsymutil` and the rest are found there, with no flag in the user's command. `SMITH_CLANG` runs an external clang as before; `compare` checks the full smith against the install's clang binary.
- The full smith reads its LLVM install at run time (its path is compiled in, `-DSMITH_LLVM_BIN=`), until #2 embeds the resource directory.
- The unstripped full smith is about 107 MB (`-g`), about 80 MB stripped.

## Build, test, install

- `smith build` builds `build/<OS>-<arch>/smith` and the `smith++` link. `CC` is the installed smith, so smith builds smith; `-DCC=/opt/homebrew/opt/llvm/bin/clang` compiles it with Homebrew's clang instead. If the installed smith is broken, use the recovery line below.
- `smith build test` runs the 29 routing tests (27 for the stage0) against stub compilers, then the 371 `smith build` tests in `tests/build.sh`, the 126 `smith new` tests in `tests/new.sh` (the three builds, `assert(false)`, the tools on the generated project, and `smith_library` under the template's flags) and the 27 arena tests in `tests/arena.sh` (`<smith/arena.h>` alone, its capacity stop, ASan, and a project using `smith_library`), and the io tests in `tests/io.sh` (`<smith/io.h>`: real files, console pipes, processes, timers, the seed, the lazily committed reservation, and the fake io; sanitized and plain), the random tests in `tests/random.sh` (`<smith/random.h>`: Philox known answers, the distributions, digests across build configurations, the refusals, and a project using both libraries) the text tests in `tests/text.sh` (`<smith/text.h>`: `tests/text.c` built with smith's own test library in three builds, with property tests on keys for the integers and a float parser that must equal `strtof`; `smith build text-exhaustive` parses all 2^32 float patterns, minutes on all cores, and is not part of `test`) the cache tests in `tests/cache.sh` (the shared module cache, package pruning with a faked clock, `smith cache`, all in a temporary `XDG_CACHE_HOME`; never touch the real `~/.cache/smith`) and the tests of `<smith/test.h>` and `smith_tests` in `tests/test.sh` (a scratch project whose test files fail on purpose: the failure blocks, aborts, isolation, the finding of tests, shrinking, `-Wconversion`, clangd), which need ninja, a real clang, clangd and git. `smith build compare` runs 33 invocations (compiles, links with and without debug info and sanitizers, assembly, `-###`, `-v`, diagnostics with colours, response files, standard input) through clang and through smith and compares what each prints, how each exits and the bytes of the files; `REAL` defaults to the clang of the LLVM smith links, which every case must match. Homebrew's (`-DREAL=/opt/homebrew/opt/llvm/bin/clang`) says "Homebrew clang" in its version, in debug info and in bitcode, so those cases differ against it.
- The files `smith new` writes live in `templates/new/` (dotfiles without their dot) and are embedded with `#embed`. The skills are not among them: a project gets smith's skills from the smith plugin, which the `.claude/settings.json` that `smith new` writes enables. The generated project is C2y (`-std=gnu2y`), so `build.c` tidies the templates with that standard; smith itself stays C23. A test keeps the template's rules equal to smith's `.clang-format` and `.clang-tidy`; `tests/new.sh` runs with `smith build test`.
- `include/smith/build.h` and `runtime/build_runtime.c` are embedded in smith with `#embed`; keep `cc -std=gnu23 smith.c -o smith` building all of smith (a test checks it).
- `smith build format` rewrites `smith.c`, `build.c`, `tests/stub.c`, the header and the runtime. `smith build check` runs `check-format`, `check-tidy`, `test` and `compare`, and must exit 0 before you finish. It needs Homebrew LLVM 23 (`check-llvm`). `build.c` is formatted and tidied like the rest, so each of its functions is at most 70 lines.
- Before and after a change, run `smith --smith-which -c a.c`, `--smith-which a.metal`, `a.metal b.c` and `--smith-version`, and compare. A refactor must not change a route, a message, an exit code or an argv.
- `smith build install` runs the tests, then replaces `~/.local/bin/smith`, which every project's build uses (`-DPREFIX=...` installs elsewhere). Do it only when the owner asks.
- If the installed smith is broken, or in a fresh clone, a C23 compiler and a clang for build.c are all it takes: `mkdir -p build && cc -std=gnu23 smith.c -o build/stage0 && build/stage0 build install -DCC=cc` installs the stage0; add the `build/stage0 build llvm` step before it for the full smith.
- `smith build clean` removes `build/`. smith keeps its own files in `.smith/`, outside `build/`, so a project's clean cannot delete them while ninja runs.

## Write C

The rules are game's, in https://github.com/adam-r-kowalski/game/blob/main/knowledge/c-language.md and https://github.com/adam-r-kowalski/game/blob/main/knowledge/c-naming.md, and `.clang-format` and `.clang-tidy` are copies of game's.

- Everything has the module prefix: `smith_route_metal`, `SmithRoute`, `SmithRoute_Clang`. Functions and variables are snake_case, types PascalCase, enum values `Type_Value`, macros UPPER_SNAKE.
- Functions are at most 70 lines, nothing recurses, every loop has a bound, and there is no malloc. That is smith's C: the one C++ file, `shim/clang.cpp`, is a thin C interface to LLVM and stays as small as it can; smith's C code allocates nothing, the LLVM linked into it does.
- Assert with `SMITH_ASSERT`, on in every build: arguments, buffer capacities and `snprintf` results where they must hold. An expected failure, such as a bad `$SMITH_CLANG`, is a `smith_die` message, not an assertion.
- smith's exceptions to game's rules: libc's types stay where libc takes them (`int`, `size_t`, `ssize_t`, `char *`, `uint32_t` for `_NSGetExecutablePath`), not `u32` and `u64`, so there are no casts; exit statuses are `constexpr int`, not an enum; and `SMITH_ASSERT` replaces `BASE_ASSERT`, still on in every build. smith calls libc freely, which game's base layer may not.
- Fix what clang-tidy reports in the code. Do not weaken `.clang-tidy`.

## Where docs go

- Decisions and rules, one line each: `AGENTS.md`. Planned work: GitHub issues, nowhere in the repository. Procedures: skills in `.agents/skills`. `.claude/skills` links to it.
- The `smith` and `smith-build` skills ship in the smith plugin, straight from `.agents/skills`. No project carries a copy, so change them here only.
- Code and the docs that describe it go in the same commit. Small Conventional Commits.
