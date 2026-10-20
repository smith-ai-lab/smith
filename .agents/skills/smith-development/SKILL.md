---
name: smith-development
description: "Start here for any change to smith itself: building, testing, installing and recovering it, the C rules, and where docs go. Load before editing smith.c, the tests, the Makefile or the docs."
---

# Develop smith

[AGENTS.md](../../../AGENTS.md) holds the rules and decisions, [ROADMAP.md](../../../ROADMAP.md) holds where smith is going. This skill holds the procedures every change shares. To use smith as a compiler, load [smith](../smith/SKILL.md) instead.

## Build, test, install

- `make` builds `build/<OS>-<arch>/smith` and the `smith++` link. `CC` is the installed smith, so smith builds smith. If that is broken, `make CC=/usr/bin/clang`.
- `make test` runs the 22 routing tests against stub compilers. `make compare REAL=/usr/bin/clang` runs 10 invocations through clang and through smith; `REAL` defaults to that on macOS.
- `make format` rewrites `smith.c` and `tests/stub.c`. `make check` runs `check-format`, `check-tidy`, `test` and `compare`, and must exit 0 before you finish. It needs Homebrew LLVM 23 (`make check-llvm`).
- Also build with `make STD=gnu23 BUILD=build/c23`: smith must compile as C17 and C23, with `-Werror`.
- Before and after a change, run `smith --smith-which -c a.c`, `--smith-which a.metal`, `a.metal b.c`, `a.smith` and `--smith-version`, and compare. A refactor must not change a route, a message, an exit code or an argv.
- `make install` runs the tests, then replaces `~/.local/bin/smith`, which every project's build uses. Do it only when the owner asks. If the installed smith is broken, `make install CC=/usr/bin/clang`.

## Write C

The rules are game's, in `~/Documents/game/knowledge/c-language.md` and `c-naming.md`, and `.clang-format` and `.clang-tidy` are copies of game's.

- Everything has the module prefix: `smith_route_metal`, `SmithRoute`, `SmithRoute_Clang`. Functions and variables are snake_case, types PascalCase, enum values `Type_Value`, macros UPPER_SNAKE.
- Functions are at most 70 lines, nothing recurses, every loop has a bound, and there is no malloc.
- Assert with `SMITH_ASSERT`, on in every build: arguments, buffer capacities and `snprintf` results where they must hold. An expected failure, such as a bad `$SMITH_CLANG`, is a `smith_die` message, not an assertion.
- smith's exceptions, until it can compile itself: C17 and C23 both build, so `NULL`, `<stdbool.h>`, `__attribute__((noreturn))` and enum constants instead of `constexpr`, `nullptr`, `[[noreturn]]`. Plain `int`, `size_t` and `char` where libc wants them, not `u32`.
- Fix what clang-tidy reports in the code. Do not weaken `.clang-tidy`.

## Where docs go

- Decisions and rules, one line each: `AGENTS.md`. Plans and order of work: `ROADMAP.md`. Procedures: skills in `.agents/skills`. `.claude/skills` links to it.
- `.agents/skills/smith` is the source of truth for the copies in game and virtual-cell. Change it here, then copy it to both.
- Code and the docs that describe it go in the same commit. Small Conventional Commits.
