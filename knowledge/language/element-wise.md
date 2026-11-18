---
type: specification
title: Operators on arrays
description: An operator on arrays means what it means in linear algebra, each result is a view, and arrays of different shapes are combined with a for.
status: planned
---

# Operators on arrays

An operator on arrays means what it means in linear algebra, and each result is a view like a [`for`](for.md).

## Adding and subtracting

Two arrays of the same shape add and subtract.

```
fn added(x: [3]f32, y: [3]f32) -> [3]f32 { x + y }
```

`x + y` is `for i { x[i] + y[i] }`, and `x - y` is `for i { x[i] - y[i] }`. `-x` negates each element. `x + y` is what a program writes, and the `for` is written only where no operator says the same. Nothing rewrites one into the other.

```
fn flipped(x: [3]f32) -> [3]f32 { -x }
```

## Scaling

An array is scaled by a number with `*` and `/`.

```
fn scaled(x: [3]f32, k: f32) -> [3]f32 { x * k }

fn shrunk(x: [3]f32, k: f32) -> [3]f32 { x / k }
```

`x * k` is `for i { x[i] * k }`.

## Each result is a view

Each of these returns a view, so nothing is allocated by writing one, and `x + y` is read where it is used.

## What two arrays do not have

Two arrays have no `*`, `/`, or `^`. A product of elements is written with a `for`, where the indices say which elements meet.

```
fn times(x: [3]f32, y: [3]f32) -> [3]f32 { x * y }
```

This is refused, and the compiler does not build this refusal, so the page quotes no words. The product of two arrays in linear algebra is not one product of elements, so it is written as the `for` that says which elements meet.

```
fn times(x: [3]f32, y: [3]f32) -> [3]f32 { for i { x[i] * y[i] } }
```

The product that linear algebra gives two arrays is `@`, which [Linear algebra](linear-algebra.md) shows.

## Arrays of different shapes

There is no broadcasting. Arrays of different shapes are combined with a `for`.

```
fn shifted(w: [2, 3]f32, b: [2]f32) -> [2, 3]f32 { for i, j { w[i, j] + b[i] } }
```

`w + b` is refused, since the shapes differ, and the compiler does not build this refusal either. A `for` says which element of `b` meets which element of `w`.

## Applying a function to each element

`map(f, v)` applies a function to each element of `v`. It is shorthand for `for i { f(v[i]) }`, and `map(f, v)` is what a program writes. Nothing rewrites one into the other.

```
fn half(x: f32) -> f32 { x * 0.5 }

fn halved(v: [3]f32) -> [3]f32 { map(half, v) }
```

`half` is passed by name, as [Function values](function-values.md) says.

## The forms

| Written | Is |
| --- | --- |
| `x + y`, `x - y` | The form that is written: `for i { x[i] + y[i] }` and `for i { x[i] - y[i] }`, for arrays of one shape |
| `-x` | `for i { -x[i] }` |
| `x * k`, `x / k` | Each element of `x` times, or divided by, the number `k` |
| `x * y`, `x / y`, `x ^ y` for arrays | Refused: written with a `for` |
| `w + b` for shapes that differ | Refused: there is no broadcasting, written with a `for` |
| `map(f, v)` | The form that is written: `for i { f(v[i]) }` |

Next: [Reductions](reductions.md)
