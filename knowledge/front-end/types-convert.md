---
type: architecture
title: "Front end: front::types_convert"
description: "The front::types_convert part of smith's own front end: front::types_convert."
tags: [front-end]
---
# front::types_convert (`front/types_convert.smith`, #1220 J1c)

The conversion rules (#414's baseline, #398) as pure functions over `types::Ty`; the rules are the top comment of the file.
- `convert(table, from, to, lit)` says whether a value may stand where `to` is expected; `lit` (`no_literal()`, `constant(n)`, `constant_unsigned(n)`) lets `U8 small = 200;` pass.
- `cast_ok(table, from, to, lit)` says whether `(to)x` may be written. Both return a `Verdict {ok, kind, message}`; `kind` is the `How` of the rule (`widening`, `constant_fits`, `to_some`, ...).
- Implicit conversions never lose a value (owner, 2026-11-01, #1328): same-sign widening, unsigned to wider signed, `F32` to `F64`, an integer whose bits fit the mantissa.
- Every conversion that could round, every float to integer, `Bool` to integer and enumeration to integer needs a cast; const is added, never dropped.
- A fixed array never converts to a bare `T *`; it converts to a slice `T[:]` keeping its count. Only `Char[:]` converts to `Str`; `U8[:]` and `I8[:]` convert by the cast `(Str)xs`. A string literal, and only a literal (it is NUL-terminated), also converts to a `const Char *` (`literal_to_pointer`, #2941): `stdio.printf("%d\n", x)`, and a literal passed to a Smith function's `const char *` parameter, a generic instance from another module included. A `Str` value is refused with the fix `cstr.of(s)`, and a `Char *` target refuses because the literal's characters are const. `types_decl::literal_of` marks the literal (`Literal.is_string`).
- A value converts to `Option(T)`'s `.some` and `Result(T, E)`'s `.ok`; `Option`, `Result` and `null` do not convert to a `T`.
- A written cast that can lose a value asserts that it fits, checked in every build (`Verdict.checked`; `smith prove` removes the check when it proves it).
- That covers integer narrowing, a sign change out of range and float to integer; widening, same width and sign, constants that fit, float casts, enumerations, `Bool` and pointers have none.
- `types::cast_checked(r, expr)` returns `{needed, from, to}` for the emitter (K2). A constant cast that can never fit is a compile error that suggests `wrapping::to(T, x)`.
- Casts between enumerations, `Bool` and integers, and pointers are explicit; a pointer and an integer cast only at pointer width; dropping const and casts to `Void` are refused.
- Tests: `tests/front_types_convert_test.smith`, one per message.
