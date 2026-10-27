---
type: architecture
title: smith::format is a type-checked formatter, a library of its own
description: Formatted text is a generic over its arguments with no format string, each type picks its rule at compile time, numbers are parsed and written in it, and C gets only the writer it is built from.
tags: [libraries, format, generics]
---
# smith::format

`format::to(buffer, capacity, pieces...)` and `format::push(arena, pieces...)` replace `snprintf` in `.smith` (issue #35). Smith is C plus compile time, so the formatter is an ordinary library generic on `auto... args` with a `for constexpr` over them (knowledge/smith-generics.md "Lists and for constexpr"): no format string to mismatch, no `[[gnu::format]]`, and no user-defined dispatch — the choice is `if constexpr` on `typeof(args[i])`, the same chain C programmers write by hand.

**Decisions.**
- *One module for numbers and text.* `text` is deleted (#199): its slice operations are `smith::slice`, its number parsing and writing are `format`'s. `format::parse(word, T)` is generic (a `result(T, format::Error)`, no out parameter) and so has no C spelling; C gets per-type functions, `smith_format_parse_i64`, `_u64`, `_f64` and `_f32`, each returning a result, and `smith_format_write_*`, the way arena gives C its own functions, plus `smith_format_string` and `smith_format_is` for a terminated string. The JSON, protocol and mcp code call those.
- *Pieces, not wrappers around a string.* The pieces are the values; width, hex and places are typed values (`format::hex(x, 16)`, `format::fixed(x, 3)`, `format::bytes(p, n)`), not letters in a string. An enum or pointer is a compile error: converting is the caller's one cast, and an unknown type never prints something surprising.
- *Never stops, says so as a value.* `Written{length, fits}`: the text is cut at the capacity and terminated, `length` is the whole length. Only a full arena stops (the arena's own rule).
- *Numbers are its own*, moved from `text` (#199): integers through `write_u64`/`write_i64`, floats through `write_f32` and `write_f64` (shortest round trip, so a printed value reads back exactly, unlike `%g`). `fixed` is the one rule of its own: exact rounding of the binary value on 128-bit integers (value < 2^63, at most 20 places), the same digits as libc's `%.Nf` (the tests compare, ties included).
- *Width and the `%g`/`%e` styles are wrappers too* (#76): `pad`/`left` hold the already-written piece (a string or a `char[:]` by reference, anything else in 64 inline characters), `general` and `scientific` round the exact decimal expansion of the binary value (a base-10^9 integer, no malloc), so any `digits` up to 40 match libc. A type with no rule names its position in the error (`no_rule(char[i + 1])`, a ladder of 30 literal messages in a function of its own: a `static_assert` message is a literal, a compile-time number cannot be a parameter, so the number is the size of a type, and the ladder inside `to` made the instance exceed `readability-function-size`).
- *The console is `format::print`, not an `io` function*: `io` is linked into smith itself (the lag rule: its generics could not be called from smith.c, which is C), so the generic lives here and calls `io::console_print`; a program prints typed pieces with no format string.
- *C gets the writer.* The generics cannot be called from C, so `<smith/format.h>` also exports what `to` is made of (`smith_format_make`, `put_*`, `finish`), which are plain functions; C keeps `snprintf` for the rest.
- *Not linked into smith*: it is a generic module that only `.smith` programs call, so it may use a feature as soon as the smith under test has it.

The cap on one call is the generic parameter limit (`smith_lang_call_params_max`, 32). Tests: `lib/smith/format_test.smith` (units runner). Skill: smith-format.

macOS's printf leaves a trailing zero after a `%g` tie that rounds down (`%.2g` of 10500 is `1.0e+04`); the standard says to remove it, and `format::general` does (the tests compare against libc with those zeros removed).
