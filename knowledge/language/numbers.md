---
type: specification
title: Literals and numbers
description: Literals backed by traits, the numeric types, and integer arithmetic that never overflows silently.
---

# Literals and numbers

Every literal form is backed by a trait, and the expected type picks which implementation runs. That is why the `2.0` in [`double`](functions.md) became an `f32`.

| Literal | Trait | Can become |
| --- | --- | --- |
| `5` | `FromInt[T]` | `i32`, `u8`, `f32` |
| `2.5` | `FromFloat[T]` | `f16`, `bf16`, `f32`, `f64` |
| `"hi"` | `FromString[T]` | `str` |
| `[1, 2, 3]` | `FromArray[T]` | An array of three elements |

- Conversion runs at compile time, so `300` as a `u8` is a compile error.
- A literal with nothing to determine its type is an error. A binding gives it one by stating its type, as in `limit: i32 = 100`.
- A literal that is an operand takes the one type that fits: a type it can become, for which the operator has an implementation with the other operand's type. With `x: f32`, the `2` in `x * 2` is an `f32`. When more than one type fits, or both operands are literals, it is a compile error, and a binding that states its type settles it.
- Only literals are flexible. A value of one numeric type becomes another only through an explicit call such as `float(n)`.
- The numeric types are `i8` to `i64`, `u8` to `u64`, `f16`, `bf16`, `f32`, and `f64`, plus `bool`. A `str` is bytes known to be valid UTF-8.

  Integers also have bitwise operations and shifts, as named functions.

Integer arithmetic never overflows silently. A plain `+`, `-`, or `*` compiles only when the result provably fits, and no check is ever inserted behind the author's back. When the compiler cannot prove it, the author picks a behaviour by name. The proof machinery appears under [Contracts and proofs](contracts.md).

```
// Does not compile: i may be 255.
fn next(i: u8) -> u8 { i + 1 }

// 255 becomes 0.
fn next_wrapping(i: u8) -> u8 { wrapping_add(i, 1) }

// 255 stays 255.
fn next_saturating(i: u8) -> u8 { saturating_add(i, 1) }

// 255 gives .none.
fn next_checked(i: u8) -> Option[u8] { checked_add(i, 1) }
```

Most integer arithmetic is on indices and sizes, whose limits are already in their types, so the proof is usually automatic.

Next: [Lambdas, `with`, and `try`](lambdas.md)
