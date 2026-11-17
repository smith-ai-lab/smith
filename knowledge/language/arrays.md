---
type: specification
title: Arrays and sizes
description: Arrays whose sizes are part of their types, indices proven to be in range when the program is built, indexing as the Index trait, and ranges.
status: partial
built: "indexing a `List` of whole numbers or `bool`s as `xs[i]`, which compiles when `arith` proves `i < xs.size` from the size of a list written out or given to a name, the range of a `loop`, an `if`, or a `requires` over a list's `size`, where `i` is a whole number of an unsigned type; `size` is a `u64`, as `Nat` is not built; not arrays whose size is in their type, `Index` as a trait a program implements, slices, ranges outside a `loop`, or an element assigned"
---

# Arrays and sizes

An array's size is part of its type: `[3]f32` is three floats and `[m, k]f32` is an `m` by `k` matrix. A size is a `Nat`, and it may be any name that cannot change: one in brackets, which the compiler works out from the arguments, an ordinary parameter, a binding, or an earlier field of the same struct. An index is a whole number that is proven, when the program is built, to be below the size, so indexing needs no bounds check.

```
fn first_of_three(a: [3]f32) -> f32 { a[0] }

fn entry[m: Nat, k: Nat](a: [m, k]f32, r: Nat, c: Nat) -> f32
  requires r < m and c < k
{
  a[r, c]
}

fn zeros(n: Nat) -> [n]f32 { for i { 0.0 } }

trait Index[C, I, out T] {
  fn valid(c: C, i: I) -> bool
  fn at(c: C, i: I) -> T
    requires valid(c, i)
}
```

- Indexing is a trait function, as an operator is: `x[i]` means `at(x, i)` from the `Index` trait above. Any type may implement it, so nothing about an array's brackets is special.
- `valid` says which indices a container has, and `at` requires it. For an array it is that the index is below the size. So `x[i]` is written with nothing beside it, and it compiles when [`arith`](contracts.md) proves `valid(x, i)` from what is in scope: the array's size, an `if`, the range of a `loop`, or a `requires`, as in `entry`. When it cannot, the compiler states what it needs, such as `i < n`, and the author supplies it. `valid` is used to prove and is never run for an index.
- A type whose `at` answers for every index, as one that returns an `Option` does, has a `valid` that is `true`.
- An element of a `var` array is assigned as a field is, as in `a[0] = 1.0`, and only that `var` sees the change.
- A literal index is checked against the size at compile time, so `a[3]` on a `[3]f32` is a compile error.
- Arithmetic on sizes inside a type, such as `[n + w - 1]f32`, is exact and has no limit, because it is reasoned about and never computed.
- A `Nat` that the program holds when it runs is 64 bits on every target. The size of an array that exists always fits. A `Nat` worked out from others, such as `n * 2`, must be shown to fit, like any other integer.
- A size may be known only when the program runs, as the `n` of `zeros` is. An array that is kept together with its size is a [`List`](../library/list.md).
- A range is `a..b` and excludes `b`. `x[a..b]` has size `b - a`, with `x[..k]` and `x[k..]` as short forms.

Next: [`for` expressions](for.md)
