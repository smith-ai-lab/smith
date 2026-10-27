---
type: contract
title: The idiom checks
description: clang-tidy checks smith-sized-types, smith-module-library, smith-module, smith-bounded-loop, smith-c23 and smith-constexpr tell an agent what idiomatic Smith and C23 look like.
tags: [lint, idiom]
---
# The idiom checks

**The idiom checks of issue #47 are clang-tidy checks, on in smith's default `.clang-tidy` and its own** (`shim/idiom.h`,
registered in `shim/tidy.cpp` beside the two above; the editor runs them; skill smith-check, smith-idiom). Each message
names the fix. `.smith` only: `smith-sized-types` (`uint32_t`, `size_t`, `double`, `unsigned`, `long`, `int`, in `main`'s signature too, which is `i32 main(io::Io *io)` or `i32 main(i32 argc, char **argv)`: write `u32`, `usize`, `f64`), `smith-module-library` (a library that is a module used through its C header: `import
smith::arena;`) and `smith-module` (issue #60: a file with no `module` line is told the one its path implies, `tests/` being a root of its own; the compiler never refuses such a file; `smith new`'s files are modules). `.smith` and C, not C++: `smith-bounded-loop` (no `for (;;)`, `while (true)`, `do ... while (1)` outside `main`),
`smith-c23` (`NULL`, `_Bool`, `_Static_assert`, `<stdbool.h>`, `{0}`, `__attribute__((noreturn))`, `(void)` parameter lists of a module (a fix deletes `void`) and kin: `nullptr`, `bool`, `static_assert`, `{}`,
`[[noreturn]]`) and `smith-constexpr` (a file-scope `const` with a constant initializer, of arithmetic, enum, array or
record type, internal linkage in C: `constexpr`). smith's own C passes the C-side ones.

**The words and tables are `.smith`, the edge is C++** (issue #540). `shim/idiom.h` and `shim/tidy.cpp` keep what clang-tidy needs (the registered checks, the matchers over the AST, the raw lexer, the fix-its) and hand plain text to `runtime/idiom.smith`, which says what each finding says, which C spelling has which sized type, which `__attribute__` has a standard spelling, how a module's generated header pairs C names with the module's, which names the lowering made, and the module a path implies; `shim/idiom_words.h` is the call, and a message reaches `diag` as `"%0"`. A new finding's text or table row goes in the module, with a test in `tests/idiom_words_test.smith`; `tests/lint_smith_test.smith` and its golden files check what `smith lint` prints.

**`smith-unsafe`** (issue #422, `shim/unsafe.h`): in a `.smith` file with a `module` line (a `.c` file and a module-less `.smith` file are C as it is; a module may reject valid C), `setjmp`/`longjmp` and kin (the jump skips every `defer`; a function that calls `smith_trusted()` is exempt, which the test runner is until #324), a run-time-sized stack array and `alloca` (unchecked stack growth, breaks compile-time stack sizes), and an array parameter the lowering does not check. `i32 xs[4]` is checked in a module (`xs[static 4]`, [smith-language](smith-language.md)), so it is no finding; the check reads the lowered type: a bound that is no constant expression (`i32 a[n]`), and the forms left a plain pointer (`i32 a[const 4]`, `i32 a[restrict 4]`, a typedef'd array, a function type's parameter) are reported. Skill `smith-check-lints`.
