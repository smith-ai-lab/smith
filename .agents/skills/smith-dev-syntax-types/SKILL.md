---
name: smith-dev-syntax-types
description: "New syntax (#1428): the types (`i32`, `str`, `*T`, `*mut T`, `?T`, `[5]T`, `[]T`, `[5,3]T`, `[,3]T`, tuples `(i32, f64)`, `fn(A) -> R`, `fn_once`, `type`, `vec(T, N)` and swizzles). Load when writing a type in the new syntax."
---

# Types

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Types

| Type | Written |
|---|---|
| primitives | `i8 i16 i32 i64 u8 u16 u32 u64 isize usize f16 bf16 f32 f64 bool char str void` |
| pointer | `*T` read-only, `*mut T` writable |
| option | `?T` (`.some(x)`, `.none`, or a plain value where `?T` is expected) |
| fixed array | `[5]T` |
| slice | `[]T` |
| contiguous block | `[5,3]T`, `[,3]T`, `[,]T` (an empty dimension is known at run time); `[5][3]T` is the same type and `smith fmt` writes `[5,3]T` |
| nesting | `[][]T` a slice of slices (jagged), `[5][]T` five slices |
| tuple | `(i32, f64)`, `(T,)` one element, `()` empty; elements `t.0`, `t.1` |
| function | `fn(A, B) -> R` (any callable), `fn_once(A) -> R` (called exactly once), `fn()` returns nothing |
| type | `type`: a value of type `type` is a type |
| vector | `vec(T, N)` for a number type `T` |

C's `int x`, `unsigned n`, `char *s` are errors whose fix names the sized type. In smith's position (`x: int`) `int`, `float`, `unsigned` are concepts ([smith-dev-syntax-generics](../smith-dev-syntax-generics/SKILL.md)). There are no anonymous structs and no named tuples.

```smith
v := vec(f32, 4)(1, 2, 3, 4)
w := v.xy * 2.0              // swizzles .x .y .z .w, any order and count, for N <= 4
mut u := v
u.xy = w                     // write lanes through a mut
lane := v[2]
```
