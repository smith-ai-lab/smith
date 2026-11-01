---
name: smith-check-lints
description: "smith's own clang-tidy checks and what each reports: smith-preprocessor, smith-sized-types, smith-module-library, smith-module, smith-bounded-loop, smith-c23, smith-constexpr, smith-unsafe. Load on a finding named smith-* from smith lint or the editor."
---

# The smith checks

## smith-preprocessor

smith's default `.clang-tidy` also lists `smith-preprocessor`: **a `.smith` file has no preprocessor.** In a `.smith` file it reports every directive (`#define`, `#undef`, `#include`, `#if` and its kin, `#pragma`, `#error`, `#warning`, `#line`, `#embed`) and `_Pragma`, and every `c::` name that is a C macro, each with what to write instead: `#include` becomes `import`, `#define` of a constant `constexpr`, `#if` `if constexpr` with `target::`, `#pragma` a flag the library declares, `#error` `static_assert(false, ...)`; `c::assert` becomes `contract::assert`, `c::isfinite` `math::is_finite`, `c::SIZE_MAX` `math::max(Usize)`, `c::INFINITY` and `c::NAN` `math::infinity(F64)` and `math::nan(F32)`; any other macro is the module that wraps the C API's to provide (`smith-lang-c-interop`). A `.c` file is left alone. The directives are read from the file's text, so those in a branch the compiler did not take count, and the `c::` names are the macros the file's imports defined (the lowering's own test, so `c::memset` is a macro in a fortified SDK: write `mem::zero` or `mem::fill`, below). The editor shows it too. A line that must keep one says `// NOLINT(smith-preprocessor)` (or `NOLINTBEGIN`/`NOLINTEND`); deleting `smith-preprocessor` from `Checks` opts a project out.

## smith-mem

`smith-mem` (in the default `.clang-tidy`; `.smith` files with a `module` line only): **a module does not call C's raw memory functions.** `c::memcpy`, `c::memmove`, `c::memset`, `c::memcmp`, `__builtin_memcpy` and the others, and `c::malloc`, `c::calloc`, `c::aligned_alloc`, `c::posix_memalign`, `c::realloc` and `c::free`, are reported with the typed replacement of `std::mem`: ``` `memcpy` is unchecked ...: use `mem::copy(dst, src)` (import std::mem;), or mark the function `contract::trusted()` if it implements a wrapper ```. `smith lint --fix` rewrites `memset(&x, 0, sizeof x)` and `memset(p, 0, sizeof *p)` to `mem::zero(&x)` and `mem::zero(p)`, `malloc(sizeof(T))` to `mem::alloc(T)` and `free(p)` to `mem::free(p)`; the others turn their pointer and count into slices by hand (`dst[0:n]`, `mem::copy(dst[0:n], src[0:n])`). A function that calls `contract::trusted()` (`smith_trusted()` in C) is skipped: it is the edge that implements a wrapper. The list is one table in `shim/mem.h`, a row for each wrapped C function, so a later wrapper joins by a row. A `.c` file and a `.smith` file with no `module` line are left alone; `// NOLINT(smith-mem)` opts a line out ([smith-mem](../smith-mem/SKILL.md)).

## The idiom checks

smith's default `.clang-tidy` lists six more `smith-*` checks (issues #47 and #60), each message naming the fix; the editor shows them too. They are how `smith check` steers a first draft to idiomatic Smith ([smith-idiom](../smith-idiom/SKILL.md) has the rules and a port):

| Check | Files | Reports | Says |
|---|---|---|---|
| `smith-sized-types` | `.smith` | `uint8_t` to `uint64_t`, `int8_t` to `int64_t`, `size_t`, `ptrdiff_t`, `float`, `double`, `unsigned`, `long`, `short`, `signed` and `int` (not `char`; `main` too: `I32 main(I32 argc, char *argv[])`; a renamed `int main(int argc, char **argv)` compiles and is reported) | the sized type: ``write `U64` `` |
| `smith-module` | `.smith` | a file with no `module` line (build.smith and the tests included; a leading comment is fine) | the line its path implies, from the project's root with `tests/` a root of its own: ``write `module primes_test;` `` (`main.smith` is `module main;`, `build.smith` `module build;`), and `export` what other files use. The compiler never refuses such a file; `// NOLINT(smith-module)` on line 1 opts out |
| `smith-module-library` | `.smith` | a library that is a module, used through its C header: `import <smith/arena.h>;`, `c::smith_arena_push(...)`, `c::SmithArena` (the generated header's `// smith-module:` line says it is one) | `import std::arena;` and the module's name, `arena::push_raw`, `arena::Arena` |
| `smith-bounded-loop` | `.smith` and C | a loop with no condition or a constant one (`for (;;)`, `while (true)`, `do ... while (1)`) outside `main` | bound it by a count; only `main` runs forever |
| `smith-c23` | `.smith` and C | `NULL`, `_Bool`, `_Noreturn`, `_Static_assert`, `_Alignas`, `_Alignof`, `<stdbool.h>`, `{0}` as an initializer of a struct or array, `(void)` as the parameter list of a function or function type in a module (a fix deletes `void`; `(void)x;` casts and `.c` files are left alone), and `__attribute__((noreturn))`, `unused`, `warn_unused_result`, `deprecated`, `fallthrough` | `nullptr`, `bool`, `{}`, `[[noreturn]]`, `[[maybe_unused]]`, `[[nodiscard]]` |
| `smith-constexpr` | `.smith` and C | a file-scope `const` object (arithmetic, enum, or an array or struct of them; not a float, a pointer or an external one in C) with a constant initializer | `constexpr` |

`smith-unsafe` (`.smith` files with a `module` line only; a `.c` file and a module-less `.smith` file are C as it is, issue #422) reports three things with no safe use, each message naming the replacement:

| Reports | Says |
|---|---|
| `setjmp`, `longjmp`, `sigsetjmp`, `siglongjmp` (a function that calls `smith_trusted()` may, until #324) | the jump skips every `defer` and `errdefer`: return a `result`, or let the task fail |
| an array parameter that is not a checked one: a bound that is no constant (`I32 a[n]`), `I32 a[const 4]`, a typedef'd array (`Row a`), `I32 a[restrict 4]` | take `I32 a[:]`, or write `I32 a[4]` |
| a stack array sized at run time (`I32 xs[n]`) and `alloca` | `arena::push_slice(&a, I32, n)`, or a fixed array when `n` has a maximum |

`I32 xs[4]` is no finding: in a module it is a checked parameter (`smith-lang-array-params`). `// NOLINT(smith-unsafe)` opts a line out.

One more is off by default, because it is a style a project chooses (`Checks: smith-designated-init` turns it on): `smith-designated-init` (`.smith`) reports a positional initializer of two or more values for a struct that another file defines (`(text::Text){bytes, length}`, `{"src/base", fast}` for a `build::Folder`), which breaks, or shifts its values, when that struct gains a field. It says to name the fields: ``{.path = "src/base", .fast = fast}``. A struct the file defines itself, a system header's and a one-value initializer are left alone.

The naming check (`readability-identifier-naming`, which judges `.smith` names as written) skips every declaration whose name is not a simple identifier (C++ operators and conversions) and every Objective-C declaration (classes, ivars, properties, methods): Objective-C naming is Apple's, and no smith rule applies to it.

`// NOLINT(smith-c23)` (or `NOLINTBEGIN`/`NOLINTEND`) opts a line out, and deleting a check from `Checks` opts a project out. The C checks read C files and `.smith`, never C++.

## Warnings in a module

Not a clang-tidy check but the compile: in a `.smith` file with a `module` line, a lossy implicit conversion, a missing `[[fallthrough]]`, `if (x = y)`, a cast that drops `const` and the other baseline diagnostics are errors, each with `[-Wname]` in the message. Fix the code (a written cast, matching types, parentheses); a `-Wno-` in `.flags` does not turn one off. The list and the opt-in `.warnings` and `.errors` are in [smith-build-warnings](../smith-build-warnings/SKILL.md).

Also in this family: [smith-check](../smith-check/SKILL.md).
