---
type: specification
title: Conversion
description: The Convert trait, which turns a value of one type into another where nothing is lost, how it lets two types meet in an operator, and narrowing a whole number.
status: partial
built:
  - "`narrow`, `wrapping_narrow`, `saturating_narrow`, and `checked_narrow` between whole number types, the type that is expected picking the result, `narrow` compiling when `arith` proves that the value fits, and an error that says to use `convert` when the second type holds every value of the first"
---

# Conversion

`convert` turns a value of one type into a value of another that can hold it. The type that is expected picks which.

```
fn widen(small: u8) -> u32 { convert(small) }
```

It is the one function of a trait, and a conversion is an implementation of it.

```
trait Convert[A, B] {
  fn convert(a: A) -> B
}
```

- A conversion loses nothing and cannot fail. What can fail returns an `Option` or a `Result` under a name of its own.
- Each integer type converts to every integer type that holds all of its values, so a `u8` converts to a `u16` and to an `i16`, and a `u32` does not convert to an `i32`. Each float type converts to every wider one.
- No type converts to itself, and conversions do not chain: `convert` takes one step.

A program's own types join by adding an `impl`.

```
struct Feet {
  value: f64
}

struct Meters {
  value: f64
}

impl Convert[Feet, Meters] {
  fn convert(a: Feet) -> Meters { Meters(value: a.value * 0.3048) }
}
```

An enum is given a conversion from the type of each of its cases, as it is given `Eq`. So a value becomes the case that holds it:

```
s: Shape = convert(Rect(w: 1.0, h: 1.0))
```

`s` is `.rect(Rect(w: 1.0, h: 1.0))`. A type that two cases hold has no conversion, since it would not say which case, and `without Convert for Shape` takes them all away.

Conversion is how two types meet in an operator. Each number type implements `Add` once, for itself, and one implementation for each side adds any type that converts to it:

```
impl[A, B] Add[A, B, B]
  requires Convert[A, B]
{
  fn add(a: A, b: B) -> B { convert(a) + b }
}

impl[A, B] Add[B, A, B]
  requires Convert[A, B]
{
  fn add(b: B, a: A) -> B { b + convert(a) }
}
```

So an `i32` plus an `i64` is an `i64`, and a `u32` plus an `i32` is a compile error, because neither holds every value of the other. The result must fit like any other, as [Integer arithmetic](arithmetic.md) says. The other arithmetic operators, `Eq`, and `Ord` have the same two implementations, so an `i32` is compared with an `i64` too. They are written for every type and not for numbers alone, so `Feet` is added to `Meters` once `Meters` implements `Add`.

A whole number goes to a type that does not hold all of its values with `narrow`, which follows the pattern of [integer arithmetic](arithmetic.md). The plain form compiles only when `arith` proves that the value fits, and costs nothing. Where that cannot be proven, the author picks what happens by name.

```
fn digit(n: u64) -> u8
  requires n < 10
{
  narrow(n)
}
```

| Function | When the value does not fit |
| --- | --- |
| `narrow(x)` | Does not compile |
| `wrapping_narrow(x)` | Keeps the low bits |
| `saturating_narrow(x)` | Gives the nearest value the type holds |
| `checked_narrow(x)` | Gives `.none` |

The type that is expected picks the result, as for `convert`. Going between a signed and an unsigned type of one size is narrowing too, since neither holds every value of the other.

Nothing is converted without a call. The operators call `convert` in these implementations, [`try`](try.md) calls it on a failure, and everywhere else it is written.

Next: [Lambdas](lambdas.md)
