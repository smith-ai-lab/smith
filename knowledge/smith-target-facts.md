---
type: contract
title: No preprocessor, and the target's facts
description: A .smith file has no preprocessor, import target gives the facts of the target as literals, and if constexpr picks a branch before the parser.
tags: [preprocessor, target]
---
# No preprocessor, and the target's facts

**A `.smith` file has no preprocessor, and the facts of the target are `target::`** (issue #46; `runtime/lang_target.c`,
[the language](smith-language.md)). `import target;` is a module that is no file: `target::arch` (`aarch64`,
`x86_64`), `os` (`macos`, `linux`), `address_sanitizer`, `undefined_sanitizer`, `thread_sanitizer`, `assertions`,
`float_radix`, `f32_mantissa_bits`, `f64_mantissa_bits`, `float_eval_method`, `static_analyzer`. A compile asks clang's preprocessor from
the token hook (`SmithHost.target_fact`, no patch) and writes literals, so they work in `static_assert`, array sizes
and `constexpr` initializers; the raw lowering (`--smith-lower`, `--smith-header`, which are the header pass and what `smith cc --smith-lower` writes) has no preprocessor and writes the equivalent macro (defined at the top by an `#if` chain) and
`#if`/`#else`/`#endif`. **`if constexpr` works in any function body** (blocks; the condition is `target::` facts,
`constexpr` constants and literals): the branch not taken is dropped before the parser. A file that contains `if
constexpr` is offered every token of a body by the hook (keywords are not identifiers), others are untouched. **An
exported `inline` function's `if constexpr` is written to the header as `#if`** (with the `SMITH_TARGET_*` definitions
at its top), which is how `smith::contract` keeps `NDEBUG` and the analyzer in a C header. smith's own `.smith`
libraries have no directive and no `c::` name that is a C macro: `#pragma STDC FP_CONTRACT OFF` is a flag the library
declares (`SMITH_LIBRARY_FLAGS` in smith.c; the build adds it after the project's, from `.smith/lib/smith/<name>.flags`),
`c::assert` is `contract::assert`, `c::isfinite`, `sqrt`, `fabs`, `SIZE_MAX`, `INFINITY` and `NAN` are `smith::math`'s
generics (`math::is_finite(x)`, `math::max(usize)`; not constant expressions, since a generic call is a call), and
the arena's stop is `smith_failure_format` of trace (so arena, and everything that uses it, links contract and trace).

The goal is no preprocessor in `.smith` source (issue #46 is the first step): what a library asked the preprocessor, the
file asks the language. `import target;` brings a module that is no file: its names are constants of the target the file
is compiled for.

| Name | Value |
|---|---|
| `target::arch` | `target::aarch64` (1), `target::x86_64` (2), or 0 |
| `target::os` | `target::macos` (1), `target::linux` (2), or 0 |
| `target::address_sanitizer`, `target::undefined_sanitizer`, `target::thread_sanitizer` | `true` when the compile has it (`__has_feature(address_sanitizer)`, `undefined_behavior_sanitizer`, `thread_sanitizer`) |
| `target::assertions` | `true` unless `NDEBUG` is defined |
| `target::float_radix`, `target::f32_mantissa_bits`, `target::f64_mantissa_bits`, `target::float_eval_method` | `__FLT_RADIX__`, `__FLT_MANT_DIG__`, `__DBL_MANT_DIG__`, `__FLT_EVAL_METHOD__` |
| `target::static_analyzer` | `true` only while clang-tidy's analyzer runs (`__clang_analyzer__`); `smith::contract` takes a contract as an assumption there |

- **A compile asks the preprocessor** (`SmithHost.target_fact`, `shim/hook.cpp`: `defined(__aarch64__)`, the language
  options' sanitizers, `NDEBUG`, the predefined float macros), and `target::arch` is then the literal `1` (`true` or `false`
  for the sanitizers and `assertions`). So a fact is a constant expression anywhere C wants one: `static_assert`, an array
  size, a `constexpr` initializer. `target::float_eval_method` must not be negative (indeterminate). A name that is not a
  fact is an error that lists them. The name `target` is reserved for this import, whatever its alias (`import target as t;`).
- **The raw lowering has no preprocessor** (`smith cc --smith-lower` and `--smith-header`, which are the header pass and what `smith cc --smith-lower` writes). It writes the equivalent into its output: a macro for each fact used
  (`SMITH_TARGET_ARCH`), defined at the top of the output by an `#if` chain (`#if defined(__aarch64__)`, `#if
  __has_feature(address_sanitizer)`, `#define SMITH_TARGET_F32_MANTISSA_BITS __FLT_MANT_DIG__`), and for an `if
  constexpr` the branches between `#if`, `#else` and `#endif`. The preprocessor lives only in generated output. A fact
  cannot be in an exported declaration (the header is written without a compiler, for every target). The body of an
  exported `inline` function is the exception: its `if constexpr` is written to the header as the `#if`, `#elif`, `#else`
  and `#endif` the raw lowering writes, and the `SMITH_TARGET_*` definitions it needs go once at the top of the header
  (and, in the interface read in memory, on the blank lines before the first declaration), so a C includer's compiler
  answers it, as a `.smith` importer's does. `smith::contract` is built on this: `smith_requires` and the others are
  `static inline` in `<smith/contract.h>`, choosing between `__builtin_assume` (`target::static_analyzer`), a call of
  the library, in every build (`NDEBUG` does not change it).
- **`if constexpr (condition) { ... } else if constexpr (...) { ... } else { ... }` in a function body.** The branches
  are blocks. The condition is a constant expression of integers and `true` and `false`, the operators of C,
  `target::` facts, and the `constexpr` constants of the file or the function (`constexpr bool sanitized =
  target::address_sanitizer;`, then `if constexpr (sanitized)`). A compile asks the parser for a constant's value; the
  raw lowering records the text of its initializer (the latest `constexpr` declaration of the name). The branch not taken
  is read as tokens and dropped, never given to the parser: no diagnostic, no hover and no code, so it may be
  an inline-asm constraint of another architecture. The branch taken is an ordinary block (it keeps its scope). Anything
  else in a condition is an error at the offending name: ``in `if constexpr`: `n` is not a constant: ...``. The
  `else` of a branch taken is dropped without being evaluated.
- **At file scope, around declarations** (issue #335, owner: "if constexpr at filescope is definitely the answer"): in
  a module, `if constexpr` / `else if constexpr` / `else` may stand between declarations and around any of them,
  imports, functions, globals, types and exports. The branch taken is at file scope: its braces are dropped
  (`SmithLangIfFrame.file_scope`, runtime/lang_target.c) and open no scope. The branch not taken is dropped unread, so
  its imports' headers are never loaded. A compile is offered the `if` and every token of the branch only in a file
  whose text has `if constexpr` (`smith_lang_fast`). Imports keep their order: a branch of only imports may sit among
  the import lines, and the hook counts an indented `import` line as one (`countImportLines`), the lowering counting a
  dropped branch's as seen (after its `}` is taken, so what the last import queues is not dropped with it), so the last import is still where the imports' type functions and bodies are used. The raw
  lowering writes `#if`/`#elif`/`#else`/`#endif` with the braces dropped, and the module's header gets the same
  directives around its exports (with the `SMITH_TARGET_*` definitions), so a C includer's preprocessor chooses as
  the module's compile did; the `#if` chains also stand around the imports they hold in the header's include lines
  (with the definition of the facts they ask), so a header never includes the header of another target (#305). A function before a file-scope `if` is measured by `readability-function-size` with the branch after it (`// NOLINTNEXTLINE(readability-function-size)`, lib/smith/net.smith). An `if` at file scope is invalid C, so no file without a `module` line changes meaning.
  clang-format reads `import <a/b.h>;` inside a branch as code (`<a / b.h>`) until its patch learns the form (#305).
- **Files that contain `if constexpr`** are offered every token of a function body by the compiler (a keyword is not an
  identifier, which is all a sparse host is offered), so such a file lowers a little slower; others are not changed.
- **Limits:** a `target::` fact in the `if constexpr` condition of a *generic* works in a compile (it is a literal in the
  recorded body) and is an error in the raw lowering, which cannot evaluate a macro; an `if constexpr` condition about a
  type (`sizeof(int) == 4`, `is_integer(T)`) is allowed in a compile and not in the raw lowering, outside a generic it is
  better written with a `static_assert`; the branches must be blocks; `else if` after `if constexpr` must be `else if
  constexpr`.

## Decisions

- `if constexpr` works at file scope around any declarations, imports included; the branch not taken is never read (owner, 2026-10-25).
