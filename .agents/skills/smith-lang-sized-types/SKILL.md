---
name: smith-lang-sized-types
description: "The built-in sized types of .smith files: u8 u16 u32 u64, i8 i16 i32 i64, usize, isize, f16 f32 f64, and bool. Load when you see these names in .smith source, a type error that names __UINT32_TYPE__ or unsigned long long for a u64, or a variable that is named like a sized type."
---

# Sized types

This skill ships in the smith plugin, from `.agents/skills/smith-lang-sized-types/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. Overview: `smith-lang`.

They work in any `.smith` file, with no import and no module line:

| name | is | name | is |
|---|---|---|---|
| `u8` `u16` `u32` `u64` | `uint8_t` … `uint64_t` | `i8` `i16` `i32` `i64` | `int8_t` … `int64_t` |
| `usize` | `size_t` | `isize` | `ptrdiff_t` |
| `f16` | `_Float16` | `f32` `f64` | `float`, `double` |

`bool` is C23's already.

## What they are

Spellings of the standard types, never typedefs: `u64` is the compiler's own `uint64_t` (`unsigned long long` on macOS), the same type, so `%llu`, `_Generic` and a C header's `uint64_t` agree. No `<stdint.h>` or `<stddef.h>` is needed to use them. Generated C headers say `uint64_t` and include `<stdint.h>`.

## When a name is yours

A file that declares its own `u32` (a typedef, a variable, a field named `u8`) keeps it: plain C that already uses these names compiles as it did. A name after `.` or `->` is a field. A declaration such as `int u8 = 4;` declares a variable.

## Errors

A diagnostic prints the compiler's spelling (`unsigned int`), not `u32`.
