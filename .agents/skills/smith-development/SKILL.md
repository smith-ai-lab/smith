---
name: smith-development
description: "Start here for any change to smith itself: building, testing, installing and recovering it, the C rules, and where docs go. Load before editing smith.c, the tests, the Makefile or the docs."
---

# Develop smith

[AGENTS.md](../../../AGENTS.md) holds the rules and decisions, [ROADMAP.md](../../../ROADMAP.md) holds where smith is going. This skill holds the procedures every change shares. To use smith as a compiler, load [smith](../smith/SKILL.md) instead.

## Build, test, install

- `make` builds `build/<OS>-<arch>/smith` and the `smith++` link. `CC` is the installed smith, so smith builds smith. If that is broken, `make CC=/usr/bin/clang`.
- `make test` runs the 22 routing tests against stub compilers, then the 50 `smith build` tests in `tests/build.sh`, which need ninja and a real clang. `make compare REAL=/usr/bin/clang` runs 10 invocations through clang and through smith; `REAL` defaults to that on macOS.
- `include/smith/build.h` and `runtime/build_runtime.c` are embedded in smith with `#embed`; keep `cc -std=gnu23 smith.c -o smith` building all of smith (a test checks it).
- `make format` rewrites `smith.c`, `tests/stub.c`, the header and the runtime. `make check` runs `check-format`, `check-tidy`, `test` and `compare`, and must exit 0 before you finish. It needs Homebrew LLVM 23 (`make check-llvm`).
- Before and after a change, run `smith --smith-which -c a.c`, `--smith-which a.metal`, `a.metal b.c` and `--smith-version`, and compare. A refactor must not change a route, a message, an exit code or an argv.
- `make install` runs the tests, then replaces `~/.local/bin/smith`, which every project's build uses. Do it only when the owner asks. If the installed smith is broken, `make install CC=/usr/bin/clang`.

## Write C

The rules are game's, in https://github.com/adam-r-kowalski/game/blob/main/knowledge/c-language.md and https://github.com/adam-r-kowalski/game/blob/main/knowledge/c-naming.md, and `.clang-format` and `.clang-tidy` are copies of game's.

- Everything has the module prefix: `smith_route_metal`, `SmithRoute`, `SmithRoute_Clang`. Functions and variables are snake_case, types PascalCase, enum values `Type_Value`, macros UPPER_SNAKE.
- Functions are at most 70 lines, nothing recurses, every loop has a bound, and there is no malloc.
- Assert with `SMITH_ASSERT`, on in every build: arguments, buffer capacities and `snprintf` results where they must hold. An expected failure, such as a bad `$SMITH_CLANG`, is a `smith_die` message, not an assertion.
- smith's exceptions to game's rules: libc's types stay where libc takes them (`int`, `size_t`, `ssize_t`, `char *`, `uint32_t` for `_NSGetExecutablePath`), not `u32` and `u64`, so there are no casts; exit statuses are `constexpr int`, not an enum; and `SMITH_ASSERT` replaces `BASE_ASSERT`, still on in every build. smith calls libc freely, which game's base layer may not.
- Fix what clang-tidy reports in the code. Do not weaken `.clang-tidy`.

## Where docs go

- Decisions and rules, one line each: `AGENTS.md`. Plans and order of work: `ROADMAP.md`. Procedures: skills in `.agents/skills`. `.claude/skills` links to it.
- `.agents/skills/smith` is the source of truth for the copies in game and virtual-cell. Change it here, then copy it to both.
- Code and the docs that describe it go in the same commit. Small Conventional Commits.
