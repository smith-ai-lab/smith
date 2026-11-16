---
type: specification
title: for expressions
description: for as an unordered description of an array, reductions, element-wise operators, @, and the rules for float arithmetic.
status: planned
---

# `for` expressions

`for i { e }` describes an array: the value `e` at every index `i`. Several indices give several axes. A `for` is a view, a rule for computing each element, so it allocates nothing by being written.

```
trait MatMul[A, B, out C] {
  fn matmul(a: A, b: B) -> C
}

// vector @ vector
impl[n: Nat] MatMul[[n]f32, [n]f32, f32] {
  fn matmul(a: [n]f32, b: [n]f32) -> f32 {
    sum(for i { a[i] * b[i] })
  }
}

// matrix @ vector
impl[m: Nat, n: Nat] MatMul[[m, n]f32, [n]f32, [m]f32] {
  fn matmul(w: [m, n]f32, x: [n]f32) -> [m]f32 {
    for r { sum(for c { w[r, c] * x[c] }) }
  }
}

// matrix @ matrix
impl[m: Nat, k: Nat, n: Nat] MatMul[[m, k]f32, [k, n]f32, [m, n]f32] {
  fn matmul(a: [m, k]f32, b: [k, n]f32) -> [m, n]f32 {
    for i, j { sum(for l { a[i, l] * b[l, j] }) }
  }
}
```

An index gets its size from one of two places, and both are in the source.

- Where the index alone picks an element along an axis, as in `a[i]`, it has that axis's size. When it is used this way on several arrays, their sizes must be equal.
- Where the `for` is written in a place that expects an array, such as a function's result, an argument, or a binding that states its type, its indices take their sizes from that type.

```
fn ramp() -> [4]f32 { for i { float(i) } }

evens: [4]f32 = for i { float(i) * 2.0 }
```

- When both places give a size, the two must agree. An index with neither is a compile error that names the index.
- Arithmetic on an index, as in `x[i + 1]`, never gives it a size. The compiler does not solve for one. The access is instead proven to be in range, as [Contracts and proofs](contracts.md) describes.

- `sum`, `max`, and `mean` reduce a view to a value, and `map(f, v)` is shorthand for `for i { f(v[i]) }`.
- Arithmetic operators on arrays are element-wise and return views, so `x + y` is `for i { x[i] + y[i] }`. The one product that is not element-wise is `@`: `a @ b` means `matmul(a, b)`, which contracts the last axis of `a` with the first axis of `b`, and it binds like `*`. The sizes are checked by the types. `@` does not treat leading axes as batches, so a batch is written as a `for`.
- Iterations are independent, so every `for` may run across SIMD lanes, threads, or a GPU.
- A `for` whose block only accumulates into a `var` yields nothing. Accumulation with `+=` or `*=` is the one mutation allowed inside it, and it runs in parallel as a reduction. Giving an outer variable a new value with `=` is rejected, because the result would depend on order.
- Float code is fast by default and repeatable: the same code, built by the same compiler, on the same computer, always gives the same bits, whatever the thread count. Different hardware or a newer compiler may change the last bits.
- An accumulation may be indexed, as in `hist[bucket[i]] += 1`. This is how a scatter is written, and it is also the gradient of an indexed read.
- A float reduction is regrouped for speed, in a shape that depends only on the source and the target. An outermost `sum` is split into several running totals that are combined in pairs. A `sum` that produces one element of an array adds its terms in order, because the parallelism comes from the outer indices.
- `sum_ordered(x)` adds strictly left to right, for when exactness matters more than speed.
- A product whose only use is one add or subtract is fused with it into a single instruction with one rounding, so `a * b + c` always fuses, whether or not the product is named.
- No other float rewrite is allowed unless it is exact.

These are enough for the first piece of a neural network. A linear layer is a struct with shape-typed fields and one line of arithmetic.

```
struct Linear[i: Nat, o: Nat] {
  w: [o, i]f32
  b: [o]f32
}

fn apply[i: Nat, o: Nat](l: Linear[i, o], x: [i]f32) -> [o]f32 {
  l.w @ x + l.b
}
```

Passing the output of a `Linear[784, 256]` to a `Linear[128, 10]` is a compile error, because `256` and `128` do not match.

Random arrays follow the key rule from [Ownership](ownership.md). A random function fills a whole array from one key, and the expected type gives the shape, as it does for a literal. When each iteration needs its own key, `split_n` turns one key into an array of keys.

```
fn init_linear[i: Nat, o: Nat](key: take Key) -> Linear[i, o] {
  (kw, kb) = split(key)
  Linear(w: normal(kw), b: uniform(kb))
}

fn dropout[n: Nat](x: [n]f32, rate: f32, key: take Key) -> [n]f32 {
  keys = split_n(key)
  for i {
    if uniform(keys[i]) < rate { 0.0 } else { x[i] }
  }
}
```

Reading `keys[i]` uses up that key. It is allowed inside a `for` when the body takes `keys[i]` at most once and `i` is the loop's own index, so every iteration touches a different key. Key `i` is computed directly from the parent key and `i`, so the array of keys never exists in memory and the loop still runs in parallel.

Next: [`loop` and iteration](loop.md)
