---
type: specification
title: Arrays and sizes
description: Arrays whose sizes are part of their types, Fin indices that need no bounds check, indexing as the Index trait, and ranges.
---

# Arrays and sizes

An array's size is part of its type: `[3]f32` is three floats and `[m, k]f32` is an `m` by `k` matrix. A size is a `Nat`, and it may be any name that cannot change: one in brackets, which the compiler works out from the arguments, an ordinary parameter, a binding, or an earlier field of the same struct. An index into an axis of size `n` has type `Fin[n]`, a number known to be below `n`, so indexing needs no bounds check.

```
fn first_of_three(a: [3]f32) -> f32 { a[0] }

fn entry[m: Nat, k: Nat](a: [m, k]f32, r: Fin[m], c: Fin[k]) -> f32 {
  a[r, c]
}

fn zeros(n: Nat) -> [n]f32 { for i { 0.0 } }

trait Index[C, I, out T] {
  fn at(c: C, i: I) -> T
}
```

- Indexing is a trait function, as an operator is: `x[i]` means `at(x, i)` from the `Index` trait below. An array implements it for `Fin` indices, and any other type may implement it, so nothing about an array's brackets is special.
- An element of a `var` array is assigned as a field is, as in `a[0] = 1.0`, and only that `var` sees the change.
- A literal index is checked against the size at compile time, so `a[3]` on a `[3]f32` is a compile error.
- Arithmetic on sizes inside a type, such as `[n + w - 1]f32`, is exact and has no limit, because it is reasoned about and never computed.
- A `Nat` that the program holds when it runs is 64 bits on every target. The size of an array that exists always fits. A `Nat` worked out from others, such as `n * 2`, must be shown to fit, like any other integer.
- A size may be known only when the program runs, as the `n` of `zeros` is. An array that is kept together with its size is a [`List`](../library/list.md).
- A range is `a..b` and excludes `b`. `x[a..b]` has size `b - a`, with `x[..k]` and `x[k..]` as short forms.

Next: [`for` expressions](for.md)
