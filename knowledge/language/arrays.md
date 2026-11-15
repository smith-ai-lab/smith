---
type: specification
title: Arrays and sizes
description: Arrays whose sizes are part of their types, Fin indices that need no bounds check, and ranges.
---

# Arrays and sizes

An array's size is part of its type: `[3]f32` is three floats and `[m, k]f32` is an `m` by `k` matrix. Size parameters are written in brackets with type `Nat`. An index into an axis of size `n` has type `Fin[n]`, a number known to be below `n`, so indexing needs no bounds check.

```
fn first_of_three(a: [3]f32) -> f32 { a[0] }

fn entry[m: Nat, k: Nat](a: [m, k]f32, r: Fin[m], c: Fin[k]) -> f32 {
  a[r, c]
}
```

- A literal index is checked against the size at compile time, so `a[3]` on a `[3]f32` is a compile error.
- A size known only at run time is written `[some n]T`.
- A range is `a..b` and excludes `b`. `x[a..b]` has size `b - a`, with `x[..k]` and `x[k..]` as short forms.

Next: [`for` expressions](for.md)
