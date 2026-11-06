---
name: smith-array
description: "Using std.array (import std.array; array.filled): an array built whole, n copies of a value, as a global or a local, in place of C's zeroed `static char buf[n];`. Load when code imports std.array, on `array.filled`, or when a fixer or a port needs an array that starts with a value."
---

# Arrays built whole: smith's array

This skill ships in the smith plugin, from `.agents/skills/smith-array/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there.

C zeroes a file-level or `static` array. smith's own syntax has no zero values, so an array that must start with a value says which: `array.filled(T, n, value)` is `[n]T` with `n` copies of `value`, and `array.filled(T, [a, b], value)` is `[a, b]T`, every element `value`.

```smith fragment
import std.array

const named_length: usize = 64

mut buf: [named_length]char = array.filled(char, named_length, 0)   # a global: zero data in the object
mut table: [256]u32 = array.filled(u32, 256, 7)                     # a global: constant data

pub fn sum() -> u32:
    local := array.filled(u32, 4, 2)                                 # a local: [4]u32
    local[0] + local[3]

mut names: [5, 64]char = array.filled(char, [5, 64], 0)             # two dimensions, one contiguous block
```

- **The call is known to the compiler**, like `countof` and `sizeof`, and has no body. `T` is a type (`u8`, `char`, `f64`, a struct), `n` a constant (an integer literal, a `const`, or `+ - * / % << >>` of those; a run-time `n` is the error ``the length of `array.filled` is a constant``) and `value` has the type `T`: a number, a character, `true`, `null`, or a construction of constants (`Cell(hits = 4)`, a field left out takes its default).
- **The second argument is a length or a shape** `[a, b, ...]`, written as the type writes it: `[5, 64]char` is the same type as `[5][64]char`, and `value` has the type `T`, not a row. Each dimension is a constant, with the same error. The nested form `array.filled([64]char, 5, array.filled(char, 64, 0))` means the same and stays valid, but the shape is the one way. `C`'s `static char names[5][64];` is `array.filled(char, [5, 64], 0)` (the fixer writes it).
- **At file scope the result is data**: all zero is `.bss`, any other value is `.data`, and the object has no initializer function and runs nothing at startup. In a function a constant value is a constant array and a run-time value is a fill loop.
- **Never `= undefined` for a buffer C zeroed**: it changes what the program does when the buffer is read before it is written.
- A `pub` global with a written type appears in the generated C header as `extern T name[N]` (`extern T name[a][b]` for a shape; `geometry_shapes_buf`, bound to the symbol by `__asm__`); the length of a header array is a number or a constant of a C header, so write the number for a `pub` array.
- Not built: `zeros`, indexing and operations on arrays (the design ticket #1823), and a global whose value is itself an array.
