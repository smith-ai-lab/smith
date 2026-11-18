---
type: specification
title: Linear algebra
description: The product of linear algebra is @, which means matmul, contracts the last axis of the left array with the first axis of the right one, and checks sizes by the types.
status: planned
---

# Linear algebra

The product of linear algebra is `@`: `a @ b` means `matmul(a, b)`, which contracts the last axis of `a` with the first axis of `b`.

```
fn dot(a: [3]f32, b: [3]f32) -> f32 { a @ b }
```

`dot([1.0, 2.0, 3.0], [4.0, 5.0, 6.0])` is `32.0`, the sum of the products of the elements that meet. The last axis of `a` has three elements and so has the first axis of `b`, and they are contracted.

## A matrix and a vector

With a matrix on the left and a vector on the right, `@` gives the vector of the sums of each row's products.

```
fn times_vector(w: [2, 3]f32, x: [3]f32) -> [2]f32 { w @ x }
```

This is `for r { sum(for c { w[r, c] * x[c] }) }`, the [reduction](reductions.md) that gives each element. The last axis of `w` is `3`, and so is the first axis of `x`.

## Two matrices

```
fn compose(a: [2, 3]f32, b: [3, 4]f32) -> [2, 4]f32 { a @ b }
```

This is `for i, j { sum(for l { a[i, l] * b[l, j] }) }`. The result has the rows of `a` and the columns of `b`.

## How `@` is defined

`@` calls `matmul`, a function of the trait `MatMul`, as an arithmetic operator calls a trait function, as [Operators](operators.md) says. `a @ b` is what a program writes. `matmul(a, b)` is written only to pass `matmul` as a value, and nothing rewrites one into the other. These are the three implementations that `@` has for floats.

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

`C` is marked `out`, so the types of the two arrays decide the type of the result, as [`out` parameters](out-parameters.md) says. `@` binds like `*`, so `l.w @ x + l.b` is `(l.w @ x) + l.b`.

## The sizes are checked by the types

The size the two arrays share is the same name in the `impl`, so `@` on arrays whose sizes do not meet is refused.

```
fn wrong(a: [2, 3]f32, b: [4, 5]f32) -> [2, 5]f32 { a @ b }
```

This is refused, because the last axis of `a` is `3` and the first axis of `b` is `4`, and the compiler does not build this refusal, so the page quotes no words.

## A batch is a `for`

`@` does not treat leading axes as batches. A batch is written as a [`for`](for.md), with an index for the batch.

```
fn batch(xs: [5, 3]f32, w: [2, 3]f32) -> [5, 2]f32 {
  for k, r { sum(for c { w[r, c] * xs[k, c] }) }
}
```

## A linear layer

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

The sizes of the layer are in its type, so the types check how layers are joined. The output of a `Linear[784, 256]` is a `[256]f32`, and a `Linear[128, 10]` takes a `[128]f32`.

```
fn two(a: Linear[784, 256], b: Linear[128, 10], x: [784]f32) -> [10]f32 {
  apply(b, apply(a, x))
}
```

This is refused, because `256` and `128` do not match, and the compiler does not build this refusal, so the page quotes no words. With `b: Linear[256, 10]` it is accepted.

## The forms

| Written | Is |
| --- | --- |
| `a @ b` | The form that is written: `matmul(a, b)`, the last axis of `a` contracted with the first axis of `b` |
| `matmul(a, b)` | Written only to pass `matmul` as a value |
| `[n]f32 @ [n]f32` | A float, the sum of the products |
| `[m, n]f32 @ [n]f32` | An `[m]f32` |
| `[m, k]f32 @ [k, n]f32` | An `[m, n]f32` |
| `@` where the sizes do not meet | Refused |
| A batch | A `for` with an index for the batch |

Next: [The rules floats keep](float-rules.md)
