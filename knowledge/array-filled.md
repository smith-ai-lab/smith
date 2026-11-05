---
type: design
title: array.filled is a form the checker and emitter know
description: std.array's first function builds an array of n copies of a value; the compiler folds n and emits file-scope results as constant data, because neither syntax can evaluate a function at compile time yet.
tags: [libraries, arrays, front-end]
---
# array.filled

Issue #2339, a slice of #1823; the lead's decision on #1406 and #2339 (2026-11-05).

**The problem.** C zeroes a file-level or `static` array; smith's own syntax has no zero values, so the fixer (#1901) needs an explicit fill. `= undefined` would change what a program does when a buffer is read before it is written.

**Decision.** `array.filled(T, n, value) -> [n]T` (after `import std.array`) is a form the checker and the emitter know, as `countof` and `sizeof` are. Neither syntax can evaluate a function at compile time: today's generics cannot return an array (C cannot), and the new front end has no general compile-time evaluator.

- *Checker* (`front/types_builtin.smith`, shape `filled`): the callee is the name `filled` read through a module that is `std.array`; `T` is the first argument read as a type, `n` is folded by `types::fold_length` (the rule of an array length), `value` is typed with `T` expected. The result is `[n]T`; the checker records `n` on the call, which is how the emitter tells the call apart.
- *Emitter* (`front/emit_builtin.smith`, `front/emit_memory.smith`): at file scope (`filled_constant`) the null constant when the value is zero, else a constant array of the value; in a function a constant value is a constant array and any other a fill loop.
- *Header* (`front/compile.smith`, `variable_line`): a `pub` global with a written type is an `extern T name[N]` line with its symbol in `__asm__`.
- *Library*: `lib/std/array.smith` holds the documentation and the registry row (`std::array`); it has no body for `filled`.

**What waits.** When the compiler can evaluate a function at compile time, the body moves into `lib/std/array.smith` with no change to callers and `types_builtin`'s `filled` is deleted. The rest of #1823 (shape, indexing, operations) is not built.
