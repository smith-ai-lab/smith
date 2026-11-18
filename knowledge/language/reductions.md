---
type: specification
title: Reductions
description: sum, max, and mean reduce a view to one value, and a reduction inside a for gives each element of an array.
status: planned
---

# Reductions

`sum`, `max`, and `mean` reduce a view to a value.

```
fn total(a: [3]f32) -> f32 { sum(for i { a[i] }) }
```

`total([1.0, 2.0, 3.0])` is `6.0`. The `for` is a [view](for.md), and `sum` adds its elements. Nothing is allocated for it.

## The three reductions

`max` is the largest element, and `mean` is the sum divided by the number of elements.

```
fn largest(a: [3]f32) -> f32 { max(for i { a[i] }) }

fn average(a: [3]f32) -> f32 { mean(for i { a[i] }) }
```

`largest([1.0, 3.0, 2.0])` is `3.0`, and `average([1.0, 2.0, 3.0])` is `2.0`.

## Reducing what is computed

The `for` that is reduced may compute each element. This is the sum of squares.

```
fn energy(a: [3]f32) -> f32 { sum(for i { a[i] * a[i] }) }
```

## A reduction inside a `for`

A reduction gives one value, so it may be the value of a `for`, one for each index. This `for` has a row for each `r`, and each is the sum of the products of that row of `w` with `x`.

```
fn rows(w: [2, 3]f32, x: [3]f32) -> [2]f32 {
  for r { sum(for c { w[r, c] * x[c] }) }
}
```

The inner `for` is a view of three products, and `sum` reduces it, once for each `r`. This is the matrix times a vector that [Linear algebra](linear-algebra.md) writes with `@`.

## Floats

A float reduction is regrouped for speed. [The rules floats keep](float-rules.md) says how, and `sum_ordered` is how a sum is not.

## The forms

| Written | Is |
| --- | --- |
| `sum(v)` | The elements of the view `v` added |
| `max(v)` | The largest element of `v` |
| `mean(v)` | The sum of `v` divided by the number of its elements |
| `for r { sum(for c { e }) }` | A reduction for each `r` |

Next: [Accumulation](accumulation.md)
