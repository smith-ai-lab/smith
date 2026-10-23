---
type: contract
title: The idiom checks
description: clang-tidy checks smith-sized-types, smith-module-library, smith-module, smith-bounded-loop, smith-c23 and smith-constexpr tell an agent what idiomatic Smith and C23 look like.
tags: [lint, idiom]
---
# The idiom checks

**The idiom checks of issue #47 are clang-tidy checks, on in smith's default `.clang-tidy` and its own** (`shim/idiom.h`,
registered in `shim/tidy.cpp` beside the two above; the editor runs them; skill smith-check, smith-idiom). Each message
names the fix. `.smith` only: `smith-sized-types` (`uint32_t`, `size_t`, `double`, `unsigned`, `long`, `int`, in `main`'s signature too, which is `i32 main(io::Io *io)` or `i32 main(i32 argc, char *argv[])`: write `u32`, `usize`, `f64`), `smith-module-library` (a library that is a module used through its C header: `import
smith::arena;`) and `smith-module` (issue #60: a file with no `module` line is told the one its path implies, `tests/` being a root of its own; the compiler never refuses such a file; `smith new`'s files are modules). `.smith` and C, not C++: `smith-bounded-loop` (no `for (;;)`, `while (true)`, `do ... while (1)` outside `main`),
`smith-c23` (`NULL`, `_Bool`, `_Static_assert`, `<stdbool.h>`, `{0}`, `__attribute__((noreturn))` and kin: `nullptr`, `bool`, `static_assert`, `{}`,
`[[noreturn]]`) and `smith-constexpr` (a file-scope `const` with a constant initializer, of arithmetic, enum, array or
record type, internal linkage in C: `constexpr`). smith's own C passes the C-side ones.
