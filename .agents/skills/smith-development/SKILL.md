---
name: smith-development
description: "Start here for any change to smith itself: building, testing, installing and recovering it, the C rules, and where docs go. Load before editing smith.c, build.c, the tests or the docs."
---

# Develop smith

[AGENTS.md](../../../AGENTS.md) holds the rules and decisions, [ROADMAP.md](../../../ROADMAP.md) holds where smith is going. This skill holds the procedures every change shares. To use smith as a compiler, load [smith](../smith/SKILL.md) instead. To edit smith's own `build.c`, load [smith-build](../smith-build/SKILL.md) as well.

## Build, test, install

smith builds itself with its own `build.c`, so the installed smith must have `smith build`. Pass `-DCC=/usr/bin/clang` to any of these to build smith with Apple's clang instead of the installed smith.

- `smith build` builds `build/<OS>-<arch>/smith` and the `smith++` link. `CC` is the installed smith, so smith builds smith; `-DCC=/usr/bin/clang` compiles it with Apple's clang instead. If the installed smith is broken, use the recovery line below.
- `smith build test` runs the 22 routing tests against stub compilers, then the 230 `smith build` tests in `tests/build.sh`, the 122 `smith new` tests in `tests/new.sh` (the three builds, `assert(false)`, the tools on the generated project, and `smith_library` under the template's flags) and the 21 arena tests in `tests/arena.sh` (`<smith/arena.h>` alone, its capacity stop, ASan, and a project using `smith_library`), and the random tests in `tests/random.sh` (`<smith/random.h>`: Philox known answers, the distributions, digests across build configurations including x86_64 under Rosetta when present, the refusals, and a project using both libraries), which need ninja, a real clang, clangd and git. `smith build compare -DREAL=/usr/bin/clang` runs 10 invocations through clang and through smith; `REAL` defaults to that on macOS.
- The files `smith new` writes live in `templates/new/` (dotfiles without their dot) and are embedded with `#embed`. The skills are not among them: a project gets smith's skills from the smith plugin, which the `.claude/settings.json` that `smith new` writes enables. The generated project is C2y (`-std=gnu2y`), so `build.c` tidies the templates with that standard; smith itself stays C23. A test keeps the template's rules equal to smith's `.clang-format` and `.clang-tidy`; `tests/new.sh` runs with `smith build test`.
- `include/smith/build.h` and `runtime/build_runtime.c` are embedded in smith with `#embed`; keep `cc -std=gnu23 smith.c -o smith` building all of smith (a test checks it).
- `smith build format` rewrites `smith.c`, `build.c`, `tests/stub.c`, the header and the runtime. `smith build check` runs `check-format`, `check-tidy`, `test` and `compare`, and must exit 0 before you finish. It needs Homebrew LLVM 23 (`check-llvm`). `build.c` is formatted and tidied like the rest, so each of its functions is at most 70 lines.
- Before and after a change, run `smith --smith-which -c a.c`, `--smith-which a.metal`, `a.metal b.c` and `--smith-version`, and compare. A refactor must not change a route, a message, an exit code or an argv.
- `smith build install` runs the tests, then replaces `~/.local/bin/smith`, which every project's build uses (`-DPREFIX=...` installs elsewhere). Do it only when the owner asks.
- If the installed smith is broken, or in a fresh clone, a C23 compiler is all it takes: `mkdir -p build && cc -std=gnu23 smith.c -o build/stage0 && build/stage0 build install -DCC=cc`.
- `smith build clean` removes `build/`. smith keeps its own files in `.smith/`, outside `build/`, so a project's clean cannot delete them while ninja runs.

## Write C

The rules are game's, in https://github.com/adam-r-kowalski/game/blob/main/knowledge/c-language.md and https://github.com/adam-r-kowalski/game/blob/main/knowledge/c-naming.md, and `.clang-format` and `.clang-tidy` are copies of game's.

- Everything has the module prefix: `smith_route_metal`, `SmithRoute`, `SmithRoute_Clang`. Functions and variables are snake_case, types PascalCase, enum values `Type_Value`, macros UPPER_SNAKE.
- Functions are at most 70 lines, nothing recurses, every loop has a bound, and there is no malloc.
- Assert with `SMITH_ASSERT`, on in every build: arguments, buffer capacities and `snprintf` results where they must hold. An expected failure, such as a bad `$SMITH_CLANG`, is a `smith_die` message, not an assertion.
- smith's exceptions to game's rules: libc's types stay where libc takes them (`int`, `size_t`, `ssize_t`, `char *`, `uint32_t` for `_NSGetExecutablePath`), not `u32` and `u64`, so there are no casts; exit statuses are `constexpr int`, not an enum; and `SMITH_ASSERT` replaces `BASE_ASSERT`, still on in every build. smith calls libc freely, which game's base layer may not.
- Fix what clang-tidy reports in the code. Do not weaken `.clang-tidy`.

## Where docs go

- Decisions and rules, one line each: `AGENTS.md`. Plans and order of work: `ROADMAP.md`. Procedures: skills in `.agents/skills`. `.claude/skills` links to it.
- The `smith` and `smith-build` skills ship in the smith plugin, straight from `.agents/skills`. No project carries a copy, so change them here only.
- Code and the docs that describe it go in the same commit. Small Conventional Commits.
