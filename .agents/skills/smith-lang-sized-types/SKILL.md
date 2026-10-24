---
name: smith-lang-sized-types
description: "The built-in sized types of .smith files: u8 u16 u32 u64, i8 i16 i32 i64, usize, isize, f16 f32 f64, and bool. Load when you see these names in .smith source, a type error that names __UINT32_TYPE__ or unsigned long long for a u64, or a variable that is named like a sized type."
---

# Sized types

They work in any `.smith` file, with no import and no module line, and no `<stdint.h>`:

```smith
module sizes;

export u64 widen(u8 low, u16 mid, u32 high) {   // C writes each with a stdint name
    return (u64)low + mid + high;
}

export f32 half(f32 x) {                         // C: float, the same type
    return x / 2;
}

export usize bytes(const f64 xs[:]) {            // C: the size type of <stddef.h>
    return countof(xs) * sizeof(f64);
}
```

| name | is | name | is |
|---|---|---|---|
| `u8` `u16` `u32` `u64` | `uint8_t` ... `uint64_t` | `i8` `i16` `i32` `i64` | `int8_t` ... `int64_t` |
| `usize` | `size_t` | `isize` | `ptrdiff_t` |
| `f16` | `_Float16` | `f32` `f64` | `float`, `double` |

`bool` is C23's already.

## What they are

Typedefs of the standard types: `u64` is `typedef __UINT64_TYPE__ u64;`, the compiler's own `uint64_t` (`unsigned long long` on macOS), the same type, so `%llu`, `_Generic` and a C header's `uint64_t` agree. The typedef is made where the file first uses the name, is in no header, and needs no include. Generated C headers say `uint64_t` and include `<stdint.h>`, so a C file that includes one does not get `u64`.

## When a name is yours

A file that declares its own `u32` (a typedef, a variable, a field named `u8`) keeps it: plain C that uses these names compiles as it did. A name after `.` or `->` is a field. `int u8 = 4;` declares a variable. A C header's `typedef uint32_t u32;` is the same type; only `typedef long u32;` after the file used `u32` conflicts (`typedef redefinition with different types`).

## Errors

- A diagnostic names the type as written, with what it is: ``initializing 'i32' (aka 'int') with an expression of type 'char[6]'``, with no "expanded from macro" notes.
- `i32 double(i32 x)` is ``double` is a C keyword; it cannot name a function`` (likewise a variable, parameter, field or type), at the name.
- `main` takes them too: `i32 main(i32 argc, char **argv)`, `i32 main(void)`, or `i32 main(io::Io *io)` (`smith-io`). `i32` is `int`, so a renamed `.c` file builds; `smith lint` reports the `int`s (`smith-sized-types`).
- **`str` is the type `const char[:]`** in a module (a file with a `module` line), as `u8` is a type: no import, a rewrite of the name (`str s = "abc";` count 3, no copy; `str *p` is `const char (*p)[:]`, `smith-lang-slice-pointers`; a parameter, result, field, cut, `countof`, `.name = "x"` in an initializer list). A C caller sees `struct smith_array_const_char_n`. In a file with no `module` line `str` is a plain name; a module that declares its own typedef `str` keeps it. As the name of a variable, field or function in a module it is an error, as `i32` is. Not yet: `str` in a generic's parameter list or body (write `const char xs[:]` there), and an array of `str` with braces (an error: assign the elements).
