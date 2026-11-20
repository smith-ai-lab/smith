---
type: specification
title: Conversion
description: The Convert trait, which turns a value of one type into another where nothing is lost, how it lets two types meet in an operator, and why two whole numbers need none.
status: partial
built:
  - "`convert` between whole number types, an `impl Convert` of a program's own, and the conversion an enum is given from the type of exactly one of its cases, the type that is expected picking which, with `without Convert` taking an enum's away"
  - "`narrow`, `wrapping_narrow`, `clamp` (also written `saturating_narrow` until that is retired), and `checked_narrow` between whole number types, the type that is expected picking the result, `narrow` compiling when `arith` proves that the value fits, and an error that says to use `convert` when the second type holds every value of the first"
---

# Conversion

`convert` turns a value of one type into a value of another that can hold it. The type that is expected picks which.

```
fn widen(x: f32) -> f64 { convert(x) }
```

It is the one function of a trait, and a conversion is an implementation of it.

```
trait Convert[A, B] {
  fn convert(a: A) -> B
}
```

- A conversion loses nothing and cannot fail. What can fail returns an `Option` or a `Result` under a name of its own.
- Each float type converts to every wider one.
- No type converts to itself, and conversions do not chain: `convert` takes one step.

## Whole numbers need none

Two whole numbers are two ranges, and a wider range takes a narrower one with nothing written, as [Number types](number-types.md#a-range-as-a-type) says. So there is no `Convert` between them, and a call that asks for one is refused.

```
fn widen(small: 0..256) -> 0..65536 { convert(small) }
```

This is refused, as `0..256` has no `Convert` to `0..65536`. `fn widen(small: 0..256) -> 0..65536 { small }` is the way it is written. A value goes to a narrower range where it is shown to fit, as [Integer arithmetic](arithmetic.md#where-a-value-enters-a-narrower-range) says, and with `narrow` or `clamp` where nothing shows it, as [Integer arithmetic](arithmetic.md#entering-a-range-with-no-proof) says. A whole number becomes a float with `float(n)`, as [Number types](number-types.md#floats) says.

## Types of a program's own

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

## Two types in an operator

Conversion is how two types that are not whole numbers meet in an operator. Each float type implements `Add` once, for itself, and one implementation for each side adds any type that converts to it:

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

So an `f32` plus an `f64` is an `f64`. The other arithmetic operators, `Eq`, and `Ord` have the same two implementations, so an `f32` is compared with an `f64` too. They are written for every type and not for floats alone, so `Feet` is added to `Meters` once `Meters` implements `Add`. A whole number has its own `Add`, whose result is the range that [Integer arithmetic](arithmetic.md) gives, and it is not one of these.

Nothing is converted without a call. The operators call `convert` in these implementations, [`try`](try.md) calls it on a failure, and everywhere else it is written.

Next: [Lambdas](lambdas.md)
