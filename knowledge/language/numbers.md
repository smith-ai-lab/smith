---
type: specification
title: Literals and numbers
description: Literals backed by traits, the numeric types, and integer arithmetic that never overflows silently.
status: partial
built: "string literals that become `str`, `bool` with `true` and `false`, whole number literals in decimal, hexadecimal, and binary, with a `-` before one, that become `i8` to `i64` and `u8` to `u64`, as a binding's uses decide or `i64` when nothing does, and `wrapping_`, `saturating_`, and `checked_` `add`, `sub`, and `mul` on those types, and a plain `+`, `-`, and `*` on them, which compiles when `arith` proves that the result fits, with a literal operand taking the type of the other"
---

# Literals and numbers

Every literal form is backed by a trait, and the expected type picks which implementation runs. That is why the `2.0` in [`double`](functions.md) became an `f32`.

| Literal | Trait | Can become |
| --- | --- | --- |
| `5` | `FromInt[T]` | `i32`, `u8`, `f32` |
| `2.5` | `FromFloat[T]` | `f16`, `bf16`, `f32`, `f64` |
| `"hi"` | `FromString[T]` | `str` |
| `[1, 2, 3]` | `FromArray[T]` | An array of three elements |

- A whole number may be written in decimal, hexadecimal, or binary, with `_` between digits, as `1_000_000`, `0xff`, and `0b1010`. A float has a digit on both sides of its point and may have an exponent, as `2.5` and `1e-5`. A `-` directly before a number is part of the literal, so `-128` is an `i8`.
- A whole number may be written as a character between single quotes, and it is the character's code: `'a'` is `97`. It is a whole number like any other, so it takes its type from its use, and `b - '0'` is arithmetic. The escapes are `\'`, `\\`, `\n`, and `\t`.

  ```
  fn is_digit(b: u8) -> bool { b >= '0' and b <= '9' }
  ```

  A character outside ASCII takes more than one byte in a `str`, so as a `u8` it is a compile error, and as a wider type it is its code point.
- Conversion runs at compile time, so `300` as a `u8` is a compile error.
- A literal bound to a name takes its type from how the name is used in the rest of the function. After `x = 10`, a call `double(x)` of a function that takes a `u8` makes `x` a `u8`, and a second use that needs another type is an ordinary type error at that use. A `var` is decided the same way. A binding may also state its type, as in `limit: i32 = 100`.
- When nothing in the function decides it, a whole number is an `i64` and a float is an `f64`.
- A literal that is an operand takes the type of the other operand, when it can become it. With `x: f32`, the `2` in `x * 2` is an `f32`. When it cannot, it takes the one type that fits: a type it can become, for which the operator has an implementation with the other operand's type. When more than one type fits, it is a compile error, and a binding that states its type settles it. When both operands are literals, they are decided as a literal bound to a name is.
- Only literals are flexible. A value of one numeric type becomes another only through a call: `convert`, under [Conversion](conversion.md), where nothing can be lost, and otherwise a function such as `float(n)`.
- The numeric types are `i8` to `i64`, `u8` to `u64`, `f16`, `bf16`, `f32`, and `f64`. `bool` is an enum the prelude defines, with the cases `false` and `true`, which are written as bare names. They are its cases wherever they are written, in an expression and in a pattern alike, and no other definition may be named `true` or `false`. A `str` is bytes known to be valid UTF-8.


Integer arithmetic never overflows silently. A plain `+`, `-`, or `*` compiles only when the result provably fits the type of its operands, each operation by itself, and no check is ever inserted behind the author's back. When the compiler cannot prove it, the author picks a behaviour by name. The proof machinery appears under [Contracts and proofs](contracts.md).

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

`wrapping_`, `saturating_`, and `checked_` each exist for `add`, `sub`, and `mul`, as the functions of the traits `Wrapping`, `Saturating`, and `Checked`, which every integer type implements.

The bits of an integer are worked on by named functions, those of the `Bits` trait, which every integer type implements. There are no operators for them.

```
trait Bits[T] {
  fn bit_and(a: T, b: T) -> T
  fn bit_or(a: T, b: T) -> T
  fn bit_xor(a: T, b: T) -> T
  fn bit_not(a: T) -> T
  fn shift_left(a: T, by: u8) -> T
  fn shift_right(a: T, by: u8) -> T
}

fn low_byte(x: u32) -> u32 { bit_and(x, 0xff) }

fn second_byte(x: u32) -> u32 { bit_and(shift_right(x, 8), 0xff) }
```

- A shift compiles only when `by` is proven to be below the number of bits in the type, as an index is proven below a size. Nothing tests it when the program runs.
- `shift_left` drops the bits that leave the top. `shift_right` keeps the sign of a signed number and fills an unsigned one with zeros.
- Small numbers are packed into a larger one with `+` and `*`, as in `reg * 8 + rm`, where `arith` proves that they do not run into each other. It compiles to the same shifts.

Most integer arithmetic is on indices and sizes, whose limits are already in their types, so the proof is usually automatic.

Next: [Conversion](conversion.md)
