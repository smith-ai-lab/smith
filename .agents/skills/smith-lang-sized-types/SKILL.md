---
name: smith-lang-sized-types
description: "The built-in sized types of .smith files: U8 U16 U32 U64, I8 I16 I32 I64, Usize, Isize, F16 F32 F64, and bool. Load when you see these names in .smith source, a type error that names __UINT32_TYPE__ or unsigned long long for a U64, or a variable that is named like a sized type."
---

# Sized types

They work in any `.smith` file, with no import and no module line, and no `<stdint.h>`:

```smith
module sizes;

export U64 widen(U8 low, U16 mid, U32 high) {   // C writes each with a stdint name
    return (U64)low + mid + high;
}

export F32 half(F32 x) {                         // C: float, the same type
    return x / 2;
}

export Usize bytes(const F64 xs[:]) {            // C: the size type of <stddef.h>
    return countof(xs) * sizeof(F64);
}
```

| name | is | name | is |
|---|---|---|---|
| `U8` `U16` `U32` `U64` | `uint8_t` ... `uint64_t` | `I8` `I16` `I32` `I64` | `int8_t` ... `int64_t` |
| `Usize` | `size_t` | `Isize` | `ptrdiff_t` |
| `F16` | `_Float16` | `F32` `F64` | `float`, `double` |

`Bool` is C23's already.

## What they are

Typedefs of the standard types: `U64` is `typedef __UINT64_TYPE__ U64;`, the compiler's own `uint64_t` (`unsigned long long` on macOS), the same type, so `%llu`, `_Generic` and a C header's `uint64_t` agree. The typedef is made where the file first uses the name, is in no header, and needs no include. Generated C headers say `uint64_t` and include `<stdint.h>`, so a C file that includes one does not get `U64`.

## When a name is yours

A file that declares its own `U32` (a typedef, a variable, a field named `U8`) keeps it: plain C that uses these names compiles as it did. A name after `.` or `->` is a field. `int U8 = 4;` declares a variable. A C header's `typedef uint32_t U32;` is the same type; only `typedef long U32;` after the file used `U32` conflicts (`typedef redefinition with different types`).

## Errors

- A diagnostic names the type as written, with what it is: ``initializing 'i32' (aka 'int') with an expression of type 'char[6]'``, with no "expanded from macro" notes.
- `I32 double(I32 x)` is ``double` is a C keyword; it cannot name a function`` (likewise a variable, parameter, field or type), at the name.
- `main` takes them too: `I32 main(I32 argc, char **argv)`, `I32 main(void)`, or `I32 main(io::Io *io)` (`smith-io`). `I32` is `int`, so a renamed `.c` file builds; `smith lint` reports the `int`s (`smith-sized-types`).
- **`Str` is the type `const char[:]`** in a module (a file with a `module` line), as `U8` is a type: no import, a rewrite of the name (`Str s = "abc";` count 3, no copy; `Str *p` is `const char (*p)[:]`, `smith-lang-slice-pointers`; a parameter, result, field, cut, `countof`, `.name = "x"` in an initializer list). A C caller sees `struct smith_array_const_char_n`. In a file with no `module` line `Str` is a plain name; a module that declares its own typedef `Str` keeps it. As the name of a variable, field or function in a module it is an error, as `I32` is. Not yet: `Str` in a generic's parameter list or body (write `const char xs[:]` there), and an array of `Str` with braces (an error: assign the elements).
- **Types are capitalized**: `I8`..`I64`, `U8`..`U64`, `F16`..`F64`, `Usize`, `Isize`, `Bool`, `Str`, `Type` (`constexpr Type T`), `Option(T)`, `Result(T, E)` and the concepts `Int`, `Float`, `Num`, `Pointer`, `Enum`, `Struct`, `Array`, `Slice`, `Signed`, `Unsigned` are the lowercase names above and in `smith-lang-generics` (`integer`, `floating`, `arithmetic`, ...), written with a capital so a type reads as a type. Both spellings work; `true`, `false`, C's keywords and `c::` names stay as they are, and a name after `.`, `->` or `::` is not changed.
