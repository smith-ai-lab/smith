---
type: specification
title: for
description: A for describes an array by the value at every index, and each index gets its size from an array it reads or from the type the for is written for.
status: planned
---

# `for`

`for i { e }` describes an array: the value `e` at every index `i`.

```
fn ramp() -> [4]f32 { for i { float(i) } }
```

`ramp()` is `[0.0, 1.0, 2.0, 3.0]`. The index `i` counts from `0`, as [Indexing](indexing.md) says of a place, and `float(i)` is the float with the value of `i`, as [Number types](number-types.md) says.

A `for` is a view, a rule for computing each element, so it allocates nothing by being written. [Array buffers](buffers.md) says when an array does get a buffer.

## Reading an array

The value may read an array at the index. This `for` is the array whose element `i` is twice element `i` of `a`.

```
fn doubled(a: [3]f32) -> [3]f32 { for i { a[i] * 2.0 } }
```

## Several indices

Several indices give several axes. `for i, j { e }` is a matrix, and `e` is its element at row `i` and column `j`.

```
fn grid() -> [2, 3]f32 { for i, j { float(i) * 3.0 + float(j) } }
```

`grid()` has two rows of three floats, and its element at `i, j` is `3i + j`, so the first row is `0.0, 1.0, 2.0` and the second is `3.0, 4.0, 5.0`.

## Where an index gets its size

An index gets its size from one of two places, and both are in the source.

**From the arrays it reads.** Where the index alone picks an element along an axis, as in `a[i]`, it has that axis's size.

```
fn halves(a: [3]f32) -> [3]f32 {
  b = for i { a[i] * 0.5 }
  b
}
```

The `i` of `b` is `3`, because it picks along the one axis of `a`, so `b` is a `[3]f32`. When an index is used this way on several arrays, their sizes must be equal.

```
fn product[n: Nat](a: [n]f32, b: [n]f32) -> [n]f32 { for i { a[i] * b[i] } }
```

With `a: [3]f32` and `b: [4]f32` the index would have two sizes, so it is refused.

```
fn mismatch(a: [3]f32, b: [4]f32) -> [3]f32 { for i { a[i] * b[i] } }
```

The compiler does not build this refusal, so the page quotes no words. `i` would be `3` on `a` and `4` on `b`.

**From the type it is written for.** Where the `for` is written in a place that expects an array, such as a function's result, an argument, or a binding that states its type, its indices take their sizes from that type. `ramp` above takes its `4` from its result. Here a binding states it.

```
evens: [4]f32 = for i { float(i) * 2.0 }
```

When both places give a size, the two must agree. The `i` below is `3` from `a` and `4` from the result, so it is refused, and the compiler does not build this refusal either.

```
fn widen(a: [3]f32) -> [4]f32 { for i { a[i] } }
```

An index that gets a size from neither is refused, and the compiler names the index. Nothing gives the `i` below a size, since no array is read at it and no type is stated.

```
fn lost() {
  x = for i { 1.0 }
}
```

## Arithmetic on an index

Arithmetic on an index, as in `x[i + 1]`, never gives it a size. The compiler does not solve for one. The access is instead proven to be in range, as [What arith proves](arith.md) describes. So the size comes from the type.

```
fn shifted(x: [5]f32) -> [4]f32 { for i { x[i + 1] } }
```

`i` is `4`, from the result, and `arith` proves that `i + 1` is below `5`. Without a type stated, the `i` of `y = for i { x[i + 1] }` has no size, and it is refused as `lost` is.

## No order

The iterations of a `for` are independent, so every `for` may run across SIMD lanes, threads, or a GPU. The keyword states that order does not matter, where a [`loop`](loop.md) runs in order. What follows from this for a block that changes a `var` is in [Accumulation](accumulation.md).

## The forms

| Written | Is |
| --- | --- |
| `for i { e }` | The array whose element at `i` is `e` |
| `for i, j { e }` | The matrix whose element at `i, j` is `e` |
| `a[i]` in the block | Gives `i` the size of the axis of `a` |
| A stated type for the `for` | Gives each index the size of its axis |
| `x[i + 1]` in the block | Gives `i` no size |

Next: [Operators on arrays](element-wise.md)
