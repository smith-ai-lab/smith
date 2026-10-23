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

Typedefs of the standard types: `u64` is `typedef __UINT64_TYPE__ u64;`, the compiler's own `uint64_t` (`unsigned long long` on macOS), the same type, so `%llu`, `_Generic` and a C header's `uint64_t` agree. The typedef is made where the file first uses the name (and declares nothing of it), is in no header, and needs no `<stdint.h>` or `<stddef.h>`. Generated C headers say `uint64_t` and include `<stdint.h>`, so a C file that includes one does not get `u64`.

## When a name is yours

A file that declares its own `u32` (a typedef, a variable, a field named `u8`) keeps it: plain C that already uses these names compiles as it did. A name after `.` or `->` is a field. A declaration such as `int u8 = 4;` declares a variable. A C header with `typedef uint32_t u32;` is fine: it is the same type. It conflicts only where the types differ (`typedef long u32;`, after the file has used `u32`), and clang says `typedef redefinition with different types`.

## Errors

A diagnostic names the type as written, with what it is: ``initializing 'i32' (aka 'int') with an expression of type 'char[6]'``, with no "expanded from macro" notes.

A C keyword used as a name after a sized type, `i32 double(i32 x)`, is ``double` is a C keyword; it cannot name a function`` (likewise a variable, parameter, field or type), at the name.

`main` is spelled with them too: `i32 main(i32 argc, char *argv[])`, `i32 main(void)`, or, to have smith make the io, `i32 main(io::Io *io)` (smith-io). `i32` is a typedef of `int`, so the compiler takes both spellings and a `.c` file renamed to `.smith` still builds; `smith lint` reports the `int`s (`smith-sized-types`).
