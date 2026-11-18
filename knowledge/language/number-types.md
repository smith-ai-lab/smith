---
type: specification
title: Number types
description: The whole number and float types and Nat, what each holds, why a number has exactly one type, and what the operators do with two of them.
status: partial
built:
  - "`i8` to `i64` and `u8` to `u64`, `==`, `!=`, `<`, `<=`, `>`, and `>=` between two of one type, and an error where two types meet in an operator"
  - "`Nat`, written for a parameter, a result, a field, and inside another type, the type of the `size` of a `List` and of a `str` and of a `loop` variable over `0..xs.size`, a literal where one is needed, an error where a `Nat` meets a `u64` or any other type in an operator or is bound to one, `convert` from `u8`, `u16`, `u32`, and `u64` to a `Nat` and from a `Nat` to a `u64`, and neither a wrapping function nor a bit operation"
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

## A count is a `Nat`

`Nat` is the type of a count: the size of a list, of a text, and of an array, a place in one, and how many times something happened. It holds 0 and up, in 64 bits.

```
fn longer(a: List[u8], b: List[u8]) -> Nat {
  if a.size > b.size { a.size } else { b.size }
}
```

`Nat` is a type of its own, and not another name for `u64`. A `u64` is a word of the machine: it has the [wrapping functions](../library/int.md) and the [bit operations](bits.md), which mean nothing for a count, and a `Nat` has neither. So a hash or a mask is not used as a place in a list by accident.

- A whole number literal is a `Nat` where one is needed, as it is any whole number type.
- `Nat` and `u64` hold the same numbers, so each [converts](conversion.md) to the other, and the conversion is written. A `u8`, a `u16`, and a `u32` convert to a `Nat` as they do to any type that holds every value of theirs.
- An operator between a `Nat` and a `u64` is refused, as between any two number types.

```
fn width(xs: List[u8]) -> u64 { xs.size }
```

This is refused: "this gives a `Nat`, where a `u64` is needed". A `u64` is made from it by writing `convert(xs.size)`.

```
fn wrap(n: Nat) -> Nat { wrapping_add(n, 1) }
```

This is refused too, since a `Nat` has no wrapping function. A `Nat` that is worked out is shown to fit, as [What arith proves](arith.md) says.

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
| `Nat` | A count, 0 and up, in 64 bits: a type of its own |
| `f16`, `bf16`, `f32`, `f64` | A float of 16, 16, 32, or 64 bits |
| `a + b` with two types | Refused, unless one converts to the other |
| `wrapping_add(n, 1)` or `bit_and(n, m)` on a `Nat` | Refused: a `Nat` has neither |
| `a == b`, `a < b`, and the rest | A `bool`, for two numbers of one type |

Next: [Integer arithmetic](arithmetic.md)
