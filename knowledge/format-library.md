---
type: architecture
title: smith::format is a type-checked formatter, a library of its own
description: Formatted text is a generic over its arguments with no format string, each type picks its rule at compile time, numbers go through smith::text, and C gets only the writer it is built from.
tags: [libraries, format, generics]
---
# smith::format

`format::to(buffer, capacity, pieces...)` and `format::push(arena, pieces...)` replace `snprintf` in `.smith` (issue #35). Smith is C plus compile time, so the formatter is an ordinary library generic on `auto... args` with a `for constexpr` over them (knowledge/smith-generics.md "Lists and for constexpr"): no format string to mismatch, no `[[gnu::format]]`, and no user-defined dispatch — the choice is `if constexpr` on `typeof(args[i])`, the same chain C programmers write by hand.

**Decisions.**
- *Its own module, not part of `smith::text`.* `text` is the pure, C-callable parse and write of numbers and lines (its header is C's); the formatter imports `arena` and is generic, so it has no C spelling. Putting it in `text` would make every user of `text` link `arena` and move `text`'s contracts under a second author. `format` imports both.
- *Pieces, not wrappers around a string.* The pieces are the values; width, hex and places are typed values (`format::hex(x, 16)`, `format::fixed(x, 3)`, `format::bytes(p, n)`), not letters in a string. An enum or pointer is a compile error: converting is the caller's one cast, and an unknown type never prints something surprising.
- *Never stops, says so as a value.* `Written{length, fits}`: the text is cut at the capacity and terminated, `length` is the whole length. Only a full arena stops (the arena's own rule).
- *Numbers are `text`'s*: integers through `write_u64`/`write_i64`, floats through `write_f64` (shortest round trip, so a printed value reads back exactly, unlike `%g`). `fixed` is the one rule of its own: exact rounding of the binary value on 128-bit integers (value < 2^63, at most 20 places), the same digits as libc's `%.Nf` (the tests compare, ties included).
- *C gets the writer.* The generics cannot be called from C, so `<smith/format.h>` also exports what `to` is made of (`smith_format_make`, `put_*`, `finish`), which are plain functions; C keeps `snprintf` for the rest.
- *Not in the bootstrap*: it is a generic module, which the lowering without a compiler cannot write.

The cap on one call is the generic parameter limit (`smith_lang_call_params_max`, 32). Tests: `tests/format_test.smith` (units runner). Skill: smith-format.
