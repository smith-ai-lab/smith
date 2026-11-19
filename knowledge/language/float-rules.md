---
type: specification
title: The rules floats keep
description: Float code is fast and repeatable, a float reduction is regrouped in a shape that depends only on the source, sum_ordered adds in order, and a product is fused with one add or subtract.
status: planned
---

# The rules floats keep

Float code is fast by default and repeatable: the same code, built by the same compiler, on the same computer, always gives the same bits, whatever the thread count. Different hardware or a newer compiler may change the last bits.

## A reduction is regrouped

A float reduction is regrouped for speed, in a shape that depends only on the source and the target. Float addition does not give the same bits in every order, so which terms are added first is fixed by the rules below and not by how the work is shared.

An outermost `sum` is split into several running totals that are combined in pairs.

```
fn total(x: [1000]f32) -> f32 { sum(for i { x[i] }) }
```

A `sum` that produces one element of an array adds its terms in order, because the parallelism comes from the outer indices.

```
fn rows(w: [2, 3]f32, x: [3]f32) -> [2]f32 {
  for r { sum(for c { w[r, c] * x[c] }) }
}
```

Each element of `rows` is a `sum` over `c`, which adds `w[r, 0] * x[0]` first, then the next term, and so on. The two elements are computed in parallel. The `+=` of an [accumulation](accumulation.md) is regrouped as an outermost `sum` is.

## Adding in order

`sum_ordered(x)` adds strictly left to right, for when exactness matters more than speed.

```
fn exact(x: [1000]f32) -> f32 { sum_ordered(x) }
```

`exact` adds `x[0]` and `x[1]`, then `x[2]`, and so on to the end, where `total` above adds in the shape of its several running totals.

## A product is fused

A product whose only use is one add or subtract is fused with it into a single instruction with one rounding. So `a * b + c` always fuses, whether or not the product is named.

```
fn fused(a: f32, b: f32, c: f32) -> f32 { a * b + c }

fn named(a: f32, b: f32, c: f32) -> f32 {
  p = a * b
  p + c
}
```

Both are one instruction. A product that is used twice is not fused, since its only use is not one add or subtract. Every chip a program is built for has the instruction, as [The instructions a chip must have](../compiler.md#the-instructions-a-chip-must-have) says, so a fused product is never worked out by a function.

```
fn both(a: f32, b: f32, c: f32) -> (f32, f32) {
  p = a * b
  (p + c, p - c)
}
```

## No other rewrite

No other float rewrite is allowed unless it is exact. So `a * b * c` is not rewritten as `a * (b * c)`, which can give other bits.

## The forms

| Written | Is |
| --- | --- |
| The same code, compiler, and computer | The same bits, whatever the thread count |
| An outermost `sum` | Several running totals combined in pairs |
| A `sum` that gives one element of an array | Its terms added in order |
| `sum_ordered(x)` | The elements added strictly left to right |
| `a * b + c`, `p = a * b` then `p + c` | One instruction, one rounding |
| A product used twice | Not fused |
| Any other rewrite | Not allowed unless it is exact |

Next: [Random arrays](random-arrays.md)
