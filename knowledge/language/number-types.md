---
type: specification
title: Number types
description: Every whole number is a range written a..b, the floats, the names Int, Nat, and Byte, and how two numbers meet.
status: partial
built:
  - "`i8` to `i64` and `u8` to `u64`, `==`, `!=`, `<`, `<=`, `>`, and `>=` between two of one type, and an error where two types meet in an operator"
  - "`Int`, `Nat`, and `Byte`, in every file with no import, the same types as `i64`, `u64`, and `u8`, `Nat` the type of the `size` of a `List` and of a `str` and of a `loop` variable over `0..xs.size`, and `use std.bits (u8, u16, u32, u64, i8, i16, i32, i64)`, or any of them, the names of sizes, which a file that writes one with no import is refused"
  - "`a..b` written as a type, with whole numbers for its bounds, held by a parameter, a result, a field, a list, an `Option`, and a tuple, an empty range as a type, a range of more than 64 bits refused, a literal that must be in the range, and `==`, `<`, and the rest between two of one range, and the text of one. A range is one type of its bounds, so one range meets another only where their bounds are the same"
  - "not a bound of a range that is a name, a wider range taking a narrower one, and two different ranges compared"
---

# Number types

Every whole number is a range: the whole numbers from one number up to but not including another, written `a..b`. The compiler has one kind of whole number, the range, and the floats `f16`, `bf16`, `f32`, and `f64`, which are not ranges.

```
use std.console (console, print)

fn main() ! console, alloc {
  small: 0..256 = 200
  low: -5..6 = -3
  print("{small} {low}")
}
```

This prints `200 -3`. `0..256` holds 0 to 255, and `-5..6` holds -5 to 5. A type is written after a colon, as a parameter's is.

## A range as a type

`a..b` holds `a` and every whole number above it up to `b`, and it excludes `b`, as a range is written in [Ranges and slices](ranges.md). Its least value is `a` and its greatest is `b - 1`.

```
fn brightness(level: 0..101) -> 0..256 { level }
```

`brightness(100)` is `100`. A value of a range is accepted wherever a range that holds every value of it is expected, with nothing written, so the `level` of `0..101` is a `0..256` where the result needs one. There is no `convert` between whole numbers.

A range with no number, as `5..5` or `0..n` where `n` is `0`, is a type too. No value can be given to it, so a function that takes one cannot be called.

```
fn never(x: 5..5) -> Nat { 0 }
```

`never` is accepted, and no call of it is, as no value is a `5..5`.

A value goes the other way only where it is shown to fit, or taken in by `narrow` or `clamp`, as [Integer arithmetic](arithmetic.md#where-a-value-enters-a-narrower-range) says.

```
fn dim(level: 0..256) -> 0..101 { level }
```

This is refused, as `0..256` holds 101 to 255 and `0..101` does not.

## Three names

A range is a type, and a name for one is only a way to write it. The prelude has three, for what a number is for.

| Name | Is | For |
| --- | --- | --- |
| `Int` | `-9223372036854775808..9223372036854775808`, the 64-bit signed range | A number |
| `Nat` | `0..18446744073709551616`, the 64-bit range from 0 | A count or a size |
| `Byte` | `0..256` | A byte |

```
fn longer(a: List[Byte], b: List[Byte]) -> Nat {
  if a.size > b.size { a.size } else { b.size }
}
```

No name of a whole number type is in the compiler. The names of sizes are in the module [`std.bits`](../library/bits.md), and a file that works with sizes imports them.

| Name | Is |
| --- | --- |
| `u8`, `u16`, `u32`, `u64` | `0..256`, `0..65536`, `0..4294967296`, `0..18446744073709551616` |
| `i8`, `i16`, `i32`, `i64` | `-128..128`, `-32768..32768`, `-2147483648..2147483648`, `-9223372036854775808..9223372036854775808` |

```
use std.bits (u64)

fn count(n: Nat) -> u64 { n }
```

`Nat` and `u64` are one type with two names, so `count` converts nothing. So are `Int` and `i64`, and `Byte` and `u8`.

## Floats

`f16`, `bf16`, `f32`, and `f64` hold numbers with a point. A float is written as [Literals](literals.md) says. A float is not a range, and a range is not a float: one becomes the other only through a call, as `float(n)` makes a float of a whole number.

```
fn half(n: Int) -> f64 { float(n) / 2.0 }
```

`half(5)` is `2.5`. `n / 2.0` with an `Int` and an `f64` is refused, as an operator takes two ranges or two floats.

## Comparing two numbers

`==` and `!=` ask whether two numbers are equal, and `<`, `<=`, `>`, and `>=` how they stand. Each gives a `bool`, and is a trait function, as [Comparisons](comparisons.md) says. Any two ranges are compared, whatever their bounds.

```
use std.console (console, print)

fn main() ! console, alloc {
  x: 0..256 = 200
  y: -5..6 = 3
  print("{x == 200} {x < y} {x != y}")
}
```

This prints `true false true`. A range is not compared with a float, as it is not added to one.

`Fmt`, `Eq`, `Ord`, `Serialize`, and the like are written once, for every range.

## How a range is kept

How many bytes a range takes, and in what form, is the compiler's choice, and a program cannot observe it. [How a whole number is kept](../compiler.md#how-a-whole-number-is-kept) says what it is. A range whose number of values is small is small, whatever its bounds, so `1900..2100` is one byte.

## The forms

| Written | Is |
| --- | --- |
| `a..b` | The whole numbers from `a` up to but not including `b` |
| `Int`, `Nat`, `Byte` | The 64-bit signed range, the 64-bit range from 0, and `0..256`, with no import |
| `u8` to `u64`, `i8` to `i64` | The ranges that fill 8 to 64 bits, imported from `std.bits` |
| `f16`, `bf16`, `f32`, `f64` | A float of 16, 16, 32, or 64 bits |
| A range where a wider one is expected | Accepted, with nothing written |
| A range where a narrower one is expected | Accepted where it is shown to fit, or taken in by `narrow` or `clamp`, refused otherwise |
| `a == b`, `a < b`, and the rest | A `bool`, for any two ranges |
| A range and a float in an operator | Refused: `float(n)` makes one of the other |

Next: [Integer arithmetic](arithmetic.md)
