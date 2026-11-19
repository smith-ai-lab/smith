---
type: specification
title: Literals
description: How a number, a character, a text, true, false, or an array is written in code, which trait gives each its value, and how the type of a literal is decided.
status: partial
built:
  - "string literals that become `str`, `bool` with `true` and `false`, whole number literals in decimal, hexadecimal, and binary, with a `-` before one, that become `i8` to `i64` and `u8` to `u64`, as a binding's uses decide or `i64` when nothing does, including a literal given for a type parameter, which the place of the call's result decides when it names the parameter, a literal operand taking the type of the other, and a character between single quotes, with the escapes `\'`, `\\`, `\n`, and `\t`, as a whole number, an error as a `u8` when it is not ASCII"
  - "a whole number that ends a block of an `if` or a `match` that gives a value, which takes the type of the other blocks, or of the place the value is used when every block ends in one"
  - "not a whole number as the range that holds it alone"
---

# Literals

A literal is a value written where it is used. Every literal form is backed by a trait, and the type that is expected picks which implementation runs. A whole number that becomes a range has no choice to make, as it is the range that holds it alone.

| Literal | Trait | Can become |
| --- | --- | --- |
| `5` | `FromInt[T]` | The range `5..6`, `f32` |
| `2.5` | `FromFloat[T]` | `f16`, `bf16`, `f32`, `f64` |
| `"hi"` | `FromString[T]` | `str` |
| `[1, 2, 3]` | `FromArray[T]` | An array of three elements |

That is why the `2.0` in the default of [`scale`](functions.md#defaults) is an `f32`: `scale` asked for one. The ranges and floats a number can become are on [Number types](number-types.md).

## A whole number

A whole number is written in digits. It is the range that holds it alone, so `5` is the range from 5 up to but not including 6, written `5..6`, as [Number types](number-types.md) says. It goes wherever a range that holds it is expected.

```
use std.console (console, print)

fn main() ! console, alloc {
  small: 0..256 = 200
  wide: 0..100000 = 70000
  print("{small} {wide}")
}
```

This prints `200 70000`.

A whole number may also be written in hexadecimal, with `0x`, and in binary, with `0b`. An `_` between two digits is ignored, so a long number can be read.

```
use std.console (console, print)

fn main() ! console, alloc {
  a = 1_000_000
  b = 0xff
  c = 0b1010
  d = 0b1111_0000
  print("{a} {b} {c} {d}")
}
```

This prints `1000000 255 10 240`. An `_` anywhere else is refused, and so is a digit the base does not have.

```
x = 1__0
```

This gives "`1__0` is not a number: an `_` stands only between two digits".

```
y = 0xZZ
```

This gives "`0xZZ` is not a number: `Z` is not a hexadecimal digit".

A `-` directly before a number is part of the literal, so `-128` is the range `-128..-127`, which fits `-128..128`. `128` does not.

```
use std.console (console, print)

fn main() ! console, alloc {
  low: -128..128 = -128
  print("{low}")
}
```

This prints `-128`.

## A number that does not fit

A literal is a range of its own, so a number that a stated range cannot hold is refused where it enters that range, and no program is built that holds it.

```
use std.console (console, print)

fn main() ! console {
  small: 0..256 = 256
  print("done")
}
```

This is refused, as `256` is not in `0..256`. A negative number is refused by a range that starts at zero the same way, as `-5` is not in `0..256`.

## Where the type comes from

A literal bound to a name keeps its own range, so the name goes wherever a range that holds it is expected. After `x = 10`, a call `byte(x)` of a function that takes a `0..256` is accepted, and so is a call of one that takes a `-5..6000`.

```
use std.console (console, print)

fn byte(n: 0..256) -> 0..256 { n }

fn word(n: 0..65536) -> 0..65536 { n }

fn main() ! console, alloc {
  x = 10
  print("{byte(x)} {word(x)}")
}
```

This prints `10 10`. A number that the range of a use cannot hold is refused at that use.

```
use std.console (console)

fn byte(n: 0..256) -> 0..256 { n }

fn main() ! console {
  x = 300
  byte(x)
}
```

This is refused, as `300` is not in `0..256`. A binding that states its range, as in `limit: 0..1000 = 100`, has that range, as [Bindings](bindings.md) says.

A whole number given for a type parameter is the range that holds it alone, and so is the type parameter.

```
use std.console (console, print)

fn first[A, B](a: A, b: B) -> A { a }

fn main() ! console, alloc {
  print("{first(1, 2)}")
}
```

This prints `1`, with `A` the range `1..2` and `B` the range `2..3`.

## A literal beside another value

A literal beside another number is a range of its own, and the operator gives the exact range of the two, as [Integer arithmetic](arithmetic.md) says. Any two ranges are compared, so a literal that the other side cannot hold is a comparison that gives `false`.

```
use std.console (console, print)

fn main() ! console, alloc {
  x: 0..256 = 7
  print("{x == 5} {x < 'a'} {x == 300}")
}
```

This prints `false true false`. A whole number may become a float where a float is expected, as the table says.

## A literal beside a value of a program's own type

When the other operand is a value that is not a number, the literal takes the one type that fits: a range that holds it, for which the operator has an implementation with the other operand's type. When more than one type fits, it is a compile error, and a binding that states its type settles it.

```
fn scaled(w: Weights) -> Weights { w * 2 }
```

When `Weights` is a struct of the program that has one `Mul` with an `Int` and no other, the `2` is an `Int`. When it has one with an `Int` and one with a `Byte`, both hold the `2`, and the line is refused. A binding that states the type settles it.

```
fn scaled(w: Weights) -> Weights {
  two: Int = 2
  w * two
}
```

## A character

A whole number may be written as a character between single quotes, and it is the character's code: `'a'` is `97`, the range `97..98`. It is a whole number like any other, and `b - '0'` is arithmetic. The escapes are `\'`, `\\`, `\n`, and `\t`.

```
use std.console (console, print)

fn is_digit(b: Byte) -> bool { b >= '0' and b <= '9' }

fn main() ! console, alloc {
  print("{is_digit('7')} {is_digit('x')}")
  print("{'a'} {'\''} {'\\'} {'\n'} {'\t'}")
}
```

This prints `true false` and then `97 39 92 10 9`. A character holds one character, so `'ab'` is refused: "a character has no closing `'`, and holds one character only".

A character outside ASCII takes more than one byte in a `str`, so as a `Byte` it is refused, and in a wider range it is its code point.

```
x: Byte = 'é'
```

This is refused, as `'é'` is not ASCII, so it takes more than one byte and is not a `Byte`.

```
use std.console (console, print)

fn main() ! console, alloc {
  x: 0..1114112 = 'é'
  print("{x}")
}
```

This prints `233`.

## Text, true, and false

A string in double quotes is a `str`, which [Text and bytes](text.md) describes. A string may hold expressions in braces, as [Strings that hold expressions](interpolation.md) says.

```
use std.console (console, print)

fn main() ! console {
  print("hi")
}
```

`bool` is an enum the prelude defines, with the cases `false` and `true`, which are written as bare names. They are its cases wherever they are written, in an expression and in a pattern alike.

```
use std.console (console, print)

fn flip(b: bool) -> bool {
  match b {
    true { false }
    false { true }
  }
}

fn main() ! console, alloc {
  print("{flip(true)} {flip(false)}")
}
```

This prints `false true`. No other definition may be named `true` or `false`.

```
fn true() -> bool { false }
```

This gives "`true` is a case of `bool`, so nothing else can be named `true`". A number is not a `bool`: `y: bool = 1` gives "this is a number, where a `bool` is needed".

## A float and an array

A float has a digit on both sides of its point and may have an exponent, as `2.5` and `1e-5`. Its type comes from its use, and it is an `f64` when nothing decides.

```
fn half(x: f32) -> f32 { x * 0.5 }
```

Here the `0.5` is an `f32`, as the type of `x` is. A whole number may become a float, as the table says, so `y: f32 = 5` is `5.0`.

An array written in brackets is backed by `FromArray`, and its elements are literals that follow these rules. [Arrays](arrays.md) says what an array is.

## The forms

| Written | Is | Its type |
| --- | --- | --- |
| `255`, `1_000` | A whole number in decimal | The range that holds it alone, `255..256` |
| `0xff` | A whole number in hexadecimal | `255..256` |
| `0b1010` | A whole number in binary | `10..11` |
| `-5` | A negative whole number | `-5..-4` |
| `'a'` | The code of a character | `97..98` |
| `2.5`, `1e-5` | A float | `f64` when nothing decides |
| `"hi"` | A text | `str` |
| `true`, `false` | The cases of `bool` | `bool` |
| `[1, 2, 3]` | An array | An array of three elements |

Next: [Number types](number-types.md)
