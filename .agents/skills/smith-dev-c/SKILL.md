---
name: smith-dev-c
description: "Writing smith's own C: the prefix, casing, 70-line limit, no recursion or malloc, SMITH_ASSERT, the exceptions to game's rules and clang-tidy. Load before writing or reviewing C in smith's runtime, smith.c or shim/."
---

# smith-dev-c

What smith's own code is, is in [smith-own-code](../../../knowledge/smith-own-code.md).

The rules are game's, in https://github.com/adam-r-kowalski/game/blob/main/knowledge/c-language.md and https://github.com/adam-r-kowalski/game/blob/main/knowledge/c-naming.md, and `.clang-format` and `.clang-tidy` are copies of game's.
- Everything has the module prefix: `smith_route_metal`, `SmithRoute`, `SmithRoute_Clang`. Functions and variables are snake_case, types PascalCase, enum values `Type_Value`, macros UPPER_SNAKE.
- Functions are at most 70 lines, nothing recurses, every loop has a bound, and there is no malloc. That is smith's C: the C++ files in `shim/` (`clang.cpp`, `lld.cpp`, `symbolize.cpp`) are thin C interfaces to LLVM and stay as small as they can; smith's C code allocates nothing, the LLVM linked into it does.
- Assert with `SMITH_ASSERT`, on in every build: arguments, buffer capacities and `snprintf` results where they must hold. An expected failure, such as a bad `$SMITH_CLANG`, is a `smith_die` message, not an assertion.
- smith's exceptions to game's rules: libc's types stay where libc takes them (`int`, `size_t`, `ssize_t`, `char *`, `uint32_t` for `_NSGetExecutablePath`), not `U32` and `U64`, so there are no casts; exit statuses are `constexpr int`, not an enum; and `SMITH_ASSERT` replaces `BASE_ASSERT`, still on in every build. smith calls libc freely, which game's base layer may not.
- Fix what clang-tidy reports in the code. Do not weaken `.clang-tidy`.
- `runtime/verbs.c` is included by `smith.c` after its tool code (`smith.c`, `#include "runtime/verbs.c"`), so a `smith_*` helper defined there and used earlier in `smith.c` needs a forward declaration first, as `smith_lint_expand_dirs` has.
