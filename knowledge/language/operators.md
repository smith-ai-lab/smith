---
type: specification
title: Operators
description: An arithmetic operator as a call of a trait function, operands of different types, and derive, which writes an impl field by field.
status: planned
---

# Operators

An operator is a call of a trait function. `a + b` means `Add.add(a, b)`, so a type has `+` when it has an `impl` of `Add`. The arithmetic operators are `+`, `-`, `*`, `/`, and `^` for a power.

## An operator on a type of your own

`Rect` has `+` once `Add` is implemented for it. `Add[Rect]` means `Add[Rect, Rect, Rect]`, as [`out` parameters](out-parameters.md) says.

```
struct Rect {
  w: i64
  h: i64
}

impl Add[Rect] {
  fn add(a: Rect, b: Rect) -> Rect { Rect(w: a.w + b.w, h: a.h + b.h) }
}
```

Now `unit + unit` is a `Rect`, and it means `add(unit, unit)`. The operator is the form that is written, and the call of `add` is accepted because `add` is an ordinary trait function.

```
fn twice[T: Add](x: T) -> T { x + x }

big = twice(unit)
```

A type with no `impl` of `Add` has no `+`. `x + x` in `twice` is allowed only because `T` is bound by `Add`, as [Bounds and `requires`](bounds.md) says.

## Operands of different types

The operands of an arithmetic operator may differ in type, as `Add` has a `B` that is not always `A`. So a number times an array, or a number times a struct of weights, is one more `impl` of `Mul`.

```
struct Weights {
  a: f32
  b: f32
}

impl Mul[f32, Weights, Weights] {
  fn mul(k: f32, w: Weights) -> Weights { Weights(a: k * w.a, b: k * w.b) }
}
```

`2.0 * w` is a `Weights`. The type of the result is the `out` parameter, so it is fixed by the types of the operands.

## derive

`derive Add for Circle` writes the `impl` that adds each field to the same field of the other.

```
struct Circle {
  r: i64
}

derive Add for Circle
```

This is the `impl` written out field by field, and it is the short way to write it.

## The forms

| Written | Is |
| --- | --- |
| `a + b` | `Add.add(a, b)` |
| `+`, `-`, `*`, `/`, `^` | The arithmetic operators, each a trait function |
| `impl Add[Rect] { ... }` | `+` for two `Rect`s |
| `impl Mul[f32, Weights, Weights] { ... }` | `*` for a number and a `Weights` |
| `derive Add for Circle` | An `Add` that works field by field |

Next: [Comparisons](comparisons.md)
