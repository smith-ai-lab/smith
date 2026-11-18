---
type: specification
title: Accumulation
description: A for whose block only accumulates into a var gives nothing and runs as a reduction, and an accumulation may be indexed, which is how a scatter is written.
status: planned
---

# Accumulation

A [`for`](for.md) whose block only accumulates into a [`var`](var.md) gives nothing. It runs for its effect on the `var`.

```
fn total_of(x: [4]f32) -> f32 {
  var total = 0.0
  for i { total += x[i] }
  total
}
```

`total_of([1.0, 2.0, 3.0, 4.0])` is `10.0`. `+=` and `*=` are the one mutation allowed inside a `for`, and it runs in parallel as a reduction, as [`sum`](reductions.md) does. When the sum is all that is wanted, `sum(for i { x[i] })` is the same sum.

## Giving a new value is refused

Giving an outer `var` a new value with `=` inside a `for` is refused, because the result would depend on order, and the iterations are independent.

```
fn last(x: [4]f32) -> f32 {
  var latest = 0.0
  for i { latest = x[i] }
  latest
}
```

The compiler does not build this refusal, so the page quotes no words. A [`loop`](loop.md) runs in order, and is what to write where the order matters.

```
fn last(x: [4]f32) -> f32 {
  var latest = 0.0
  loop i in 0..4 { latest = x[i] }
  latest
}
```

## An indexed accumulation

An accumulation may be indexed, as in `hist[bucket[i]] += 1.0`. This is how a scatter is written, and it is also the gradient of an indexed read.

```
fn histogram(bucket: [8]Fin[4]) -> [4]f32 {
  var hist: [4]f32 = zeros(4)
  for i { hist[bucket[i]] += 1.0 }
  hist
}
```

Each `bucket[i]` is a [`Fin[4]`](proof-fields.md), a number below `4`, so it indexes `hist`. `histogram` counts how many of the eight numbers are each of `0`, `1`, `2`, and `3`.

## Floats

A float accumulation is regrouped for speed, as a `sum` is. [The rules floats keep](float-rules.md) says how.

## The forms

| Written | Is |
| --- | --- |
| `for i { total += e }` | Adds every `e` to `total`, in parallel, as a reduction |
| `for i { total *= e }` | Multiplies `total` by every `e` |
| `for i { total = e }` | Refused: the result would depend on order |
| `for i { hist[k[i]] += e }` | An indexed accumulation, a scatter |

Next: [Linear algebra](linear-algebra.md)
