---
type: specification
title: Number types
description: The whole number and float types, what each holds, why a number has exactly one type, and what the operators do with two of them.
status: partial
built:
  - "`i8` to `i64` and `u8` to `u64`, `==`, `!=`, `<`, `<=`, `>`, and `>=` between two of one type, and an error where two types meet in an operator"
---

# Number types

Every number has one type, which says what it holds and how much room it takes. The types are `i8` to `i64`, `u8` to `u64`, `f16`, `bf16`, `f32`, and `f64`.

```
fn main() ! console, alloc {
  small: u8 = 200
  wide: i64 = -3
  print("{small} {wide}")
}
```

This prints `200 -3`. A type is written after a colon, as a parameter's is, and the number in its name is its size in bits.

## Whole numbers

An `i` type is signed and holds negative numbers too. A `u` type is unsigned and holds none.

| Type | Holds |
| --- | --- |
| `u8` | 0 to 255 |
| `u16` | 0 to 65535 |
| `u32` | 0 to 4294967295 |
| `u64` | 0 to 18446744073709551615 |
| `i8` | -128 to 127 |
| `i16` | -32768 to 32767 |
| `i32` | -2147483648 to 2147483647 |
| `i64` | -9223372036854775808 to 9223372036854775807 |

A number outside the range is refused where it is written, with the range in the message, as [Literals](literals.md) shows. What the arithmetic of two numbers in range does when the result is outside it is the subject of [Integer arithmetic](arithmetic.md).

## Floats

`f16`, `bf16`, `f32`, and `f64` hold numbers with a point. A float is written as [Literals](literals.md) says.

## Comparing two numbers

`==` and `!=` ask whether two numbers are equal, and `<`, `<=`, `>`, and `>=` how they stand. Each gives a `bool`, and is a trait function, as [Comparisons](comparisons.md) says.

```
fn main() ! console, alloc {
  x: u8 = 200
  y: u8 = x
  print("{x == 200} {x < 201} {x != y}")
}
```

This prints `true true false`. The `200` and the `201` are `u8`, as the literal beside another value is.

## One type to a number

A number never changes type by itself. An operator takes two numbers of one type, and two of different types are refused unless one converts to the other, as [Conversion](conversion.md) says. Neither `u32` nor `i32` holds every value of the other, so these are refused.

```
fn main() ! console, alloc {
  a: u32 = 1
  b: i32 = 2
  print("{a + b}")
}
```

This gives "`+` needs both sides to be of one type, and the left is `u32` where the right is `i32`". A comparison is refused the same way.

```
fn main() ! console, alloc {
  a: u32 = 1
  b: i32 = 2
  print("{a == b}")
}
```

This gives "this gives a `i32`, where a `u32` is needed".

Only a literal is flexible, as [Literals](literals.md) says, because it takes the type that is asked of it. A value of one numeric type becomes another only through a call: `convert`, under [Conversion](conversion.md), where nothing can be lost, and otherwise a function such as `float(n)`.

```
fn widen(small: u8) -> u32 { convert(small) }
```

## The forms

| Written | Is |
| --- | --- |
| `u8`, `u16`, `u32`, `u64` | An unsigned whole number of 8, 16, 32, or 64 bits |
| `i8`, `i16`, `i32`, `i64` | A signed whole number of 8, 16, 32, or 64 bits |
| `f16`, `bf16`, `f32`, `f64` | A float of 16, 16, 32, or 64 bits |
| `a + b` with two types | Refused, unless one converts to the other |
| `a == b`, `a < b`, and the rest | A `bool`, for two numbers of one type |

Next: [Integer arithmetic](arithmetic.md)
