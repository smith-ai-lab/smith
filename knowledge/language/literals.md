---
type: specification
title: Literals
description: How a number, a character, a text, true, false, or an array is written in code, which trait gives each its value, and how the type of a literal is decided.
status: partial
built:
  - "string literals that become `str`, `bool` with `true` and `false`, whole number literals in decimal, hexadecimal, and binary, with a `-` before one, that become `i8` to `i64` and `u8` to `u64`, as a binding's uses decide or `i64` when nothing does, including a literal given for a type parameter, a literal operand taking the type of the other, and a character between single quotes, with the escapes `\'`, `\\`, `\n`, and `\t`, as a whole number, an error as a `u8` when it is not ASCII"
---

# Literals

A literal is a value written where it is used. Every literal form is backed by a trait, and the type that is expected picks which implementation runs.

| Literal | Trait | Can become |
| --- | --- | --- |
| `5` | `FromInt[T]` | `i32`, `u8`, `f32` |
| `2.5` | `FromFloat[T]` | `f16`, `bf16`, `f32`, `f64` |
| `"hi"` | `FromString[T]` | `str` |
| `[1, 2, 3]` | `FromArray[T]` | An array of three elements |

That is why the `2.0` in the default of [`scale`](functions.md#defaults) is an `f32`: `scale` asked for one. The types a number can become are on [Number types](number-types.md).

## A whole number

A whole number is written in digits. The type that is expected picks which type it is.

```
fn main() ! console, alloc {
  small: u8 = 200
  wide: i32 = 70000
  print("{small} {wide}")
}
```

This prints `200 70000`.

A whole number may also be written in hexadecimal, with `0x`, and in binary, with `0b`. An `_` between two digits is ignored, so a long number can be read.

```
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

A `-` directly before a number is part of the literal, so `-128` is an `i8`, though `128` is not.

```
fn main() ! console, alloc {
  low: i8 = -128
  print("{low}")
}
```

This prints `-128`.

## A number that does not fit

The conversion from a literal runs when the program is built, so a number the type cannot hold is an error, and no program is built that holds it.

```
fn main() ! console {
  small: u8 = 256
  print("done")
}
```

This gives "`256` does not fit `u8`, which holds 0 to 255". The words name the type's range, as [Number types](number-types.md) lists. A negative number is refused by an unsigned type the same way: "`-5` does not fit `u8`, which holds 0 to 255".

## Where the type comes from

A literal bound to a name takes its type from how the name is used in the rest of the function. After `x = 10`, a call `byte(x)` of a function that takes a `u8` makes `x` a `u8`.

```
fn byte(n: u8) -> u8 { n }

fn main() ! console, alloc {
  x = 10
  print("{byte(x)}")
}
```

This prints `10`. A `var` is decided the same way. The number must fit the type that its use gives it.

```
fn byte(n: u8) -> u8 { n }

fn main() ! console {
  x = 300
  byte(x)
}
```

This gives "`300` does not fit `u8`, as the use at 5:8 needs, which holds 0 to 255".

A second use that needs another type is an ordinary type error at that use.

```
fn byte(n: u8) -> u8 { n }

fn word(n: i32) -> i32 { n }

fn main() ! console {
  x = 10
  byte(x)
  word(x)
}
```

This gives "`x` is `u8`, as the use at 7:8 needs, where a `i32` is needed". A binding that states its type settles it, as in `limit: i32 = 100`, as [Bindings](bindings.md) says.

When nothing in the function decides the type, a whole number is an `i64` and a float is an `f64`.

```
fn main() ! console, alloc {
  x = 5_000_000_000
  print("{x}")
}
```

This prints `5000000000`, which an `i32` could not hold.

A whole number given to a parameter whose type is a type parameter is decided the same way. When nothing else decides the type parameter, the number is an `i64` and so is the type parameter.

```
fn first[A, B](a: A, b: B) -> A { a }

fn main() ! console, alloc {
  print("{first(1, 2)}")
}
```

This prints `1`, with `A` and `B` both `i64`. When the function needs a trait of the type parameter, `i64` must have it, and if it does not the error names the trait and `i64`, as for any other `i64`.

## A literal beside another value

A literal that is an operand takes the type of the other operand, when it can become it. With `x: u8`, the `300` in `x == 300` is a `u8`, and so the line is refused.

```
fn main() ! console, alloc {
  x: u8 = 7
  print("{x == 5} {x < 'a'}")
}
```

This prints `false true`. With `300` in place of `5`, it gives "`300` does not fit `u8`, which holds 0 to 255".

When both operands are literals, they are decided as a literal bound to a name is.

```
fn main() ! console, alloc {
  print("{1 + 2} {3 < 4}")
}
```

This prints `3 true`, and both numbers are `i64`, as nothing decides otherwise.

When the other operand is a value the literal cannot become, the literal takes the one type that fits: a type it can become, for which the operator has an implementation with the other operand's type. When more than one type fits, it is a compile error, and a binding that states its type settles it.

```
fn scaled(w: Weights) -> Weights { w * 2 }
```

When `Weights` is a struct of the program that has one `Mul` with an `i32` and no other, the `2` is an `i32`. When it has one with an `i32` and one with an `i64`, the line is refused, and a binding that states the type settles it.

```
fn scaled(w: Weights) -> Weights {
  two: i32 = 2
  w * two
}
```

## A character

A whole number may be written as a character between single quotes, and it is the character's code: `'a'` is `97`. It is a whole number like any other, so it takes its type from its use, and `b - '0'` is arithmetic. The escapes are `\'`, `\\`, `\n`, and `\t`.

```
fn is_digit(b: u8) -> bool { b >= '0' and b <= '9' }

fn main() ! console, alloc {
  print("{is_digit('7')} {is_digit('x')}")
  print("{'a'} {'\''} {'\\'} {'\n'} {'\t'}")
}
```

This prints `true false` and then `97 39 92 10 9`. A character holds one character, so `'ab'` is refused: "a character has no closing `'`, and holds one character only".

A character outside ASCII takes more than one byte in a `str`, so as a `u8` it is a compile error, and as a wider type it is its code point.

```
x: u8 = 'é'
```

This gives "`'é'` is not ASCII, so it takes more than one byte and is not a `u8`".

```
fn main() ! console, alloc {
  x: u32 = 'é'
  print("{x}")
}
```

This prints `233`.

## Text, true, and false

A string in double quotes is a `str`, which [Text and bytes](text.md) describes. A string may hold expressions in braces, as [Strings that hold expressions](interpolation.md) says.

```
fn main() ! console {
  print("hi")
}
```

`bool` is an enum the prelude defines, with the cases `false` and `true`, which are written as bare names. They are its cases wherever they are written, in an expression and in a pattern alike.

```
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

A float has a digit on both sides of its point and may have an exponent, as `2.5` and `1e-5`. It follows the rules of a whole number above: its type comes from its use, and it is an `f64` when nothing decides.

```
fn half(x: f32) -> f32 { x * 0.5 }
```

Here the `0.5` is an `f32`, as the type of `x` is. A whole number may become a float, as the table says, so `y: f32 = 5` is `5.0`.

An array written in brackets is backed by `FromArray`, and its elements are literals that follow these rules. [Arrays](arrays.md) says what an array is.

## The forms

| Written | Is | Type when nothing decides |
| --- | --- | --- |
| `255`, `1_000` | A whole number in decimal | `i64` |
| `0xff` | A whole number in hexadecimal | `i64` |
| `0b1010` | A whole number in binary | `i64` |
| `-5` | A negative whole number | `i64` |
| `'a'` | The code of a character | `i64` |
| `2.5`, `1e-5` | A float | `f64` |
| `"hi"` | A text | `str` |
| `true`, `false` | The cases of `bool` | `bool` |
| `[1, 2, 3]` | An array | An array of three elements |

Next: [Number types](number-types.md)
