---
type: specification
title: Indexing
description: x[i] reads one element, an index is proven to be in range when the program is built, and indexing is the Index trait, whose valid says which indices a container has.
status: partial
built: "indexing a `List` of whole numbers or `bool`s as `xs[i]`, which compiles when `arith` proves `i < xs.size` from the size of a list written out or given to a name, the range of a `loop`, an `if`, or a `requires` over a list's `size`, where `i` is a whole number of an unsigned type; not an array whose size is in its type, `Index` as a trait a program implements, or an element assigned"
---

# Indexing

`x[i]` reads the element of `x` at the place `i`, and the first element is at place `0`. An index is proven to be in range when the program is built, so reading an element needs no bounds check.

## Reading an element

```
fn main() ! console, alloc {
  xs: List[u8] = [10, 20, 30]
  print("{xs[0]}")
  print("{xs[2]}")
}
```

This prints `10` and `30`. The same is written on an array, as `a[0]` for a `[3]f32`, and with one index for each size of a matrix, as `a[r, c]` for an `[m, k]f32`.

## An index must be proven in range

An index is a whole number, and it must be below the size of what it reads. The compiler proves that, with the procedure [`arith`](arith.md), from what is in scope. When it cannot, the program is refused.

```
fn main() ! console, alloc {
  xs: List[u8] = [10, 20, 30]
  print("{xs[3]}")
}
```

This is refused: "cannot prove that `3 < xs.size`, which indexing `xs` needs", with the fix "put `3` in the range of a `loop` over `0..xs.size`, check `3 < xs.size` with an `if`, or state it in a `requires`". The compiler states what it needs, such as `3 < xs.size`, and the author supplies it in one of the three ways the fix names. A literal index on an array whose size is in its type, as `a[3]` on a `[3]f32`, is a compile error in the same way.

The size of a list that is written out, or that is given to a name, is known, so `xs[0]` above compiles. An empty list has no index that compiles: `xs[0]` on `xs: List[u8] = []` is refused with the same words, "cannot prove that `0 < xs.size`, which indexing `xs` needs".

### From a `loop`

Inside `loop i in 0..xs.size`, `i` is below `xs.size`.

```
fn largest(xs: List[u32]) -> u32 {
  var best: u32 = 0
  loop i in 0..xs.size {
    if xs[i] > best { best = xs[i] }
  }
  best
}
```

`largest` of `[4, 9, 6]` is `9`. A range that is not the size is not enough: `loop i in 0..4` over a list of three gives "cannot prove that `i < xs.size`, which indexing `xs` needs". [`loop`](loop.md) says more about its range.

### From an `if`

Inside the block of `if i < xs.size`, the index is proven.

```
fn at_or_zero(xs: List[u8], i: u64) -> u8 {
  if i < xs.size { xs[i] } else { 0 }
}
```

`at_or_zero` of `[5, 6, 7]` and `1` is `6`, and with `5` it is `0`. Without the `if`, `fn third(xs: List[u8], i: u64) -> u8 { xs[i] }` is refused: "cannot prove that `i < xs.size`, which indexing `xs` needs".

### From a `requires`

A function can state what it needs of its arguments with `requires`, and then the callers prove it.

```
fn at(xs: List[u8], i: u64) -> u8
  requires i < xs.size
{
  xs[i]
}
```

`at(xs, 1)` on `[5, 6, 7]` is `6`. A call whose index is not proven is refused where it is called: `at(xs, 3)` gives "cannot prove that `3 < xs.size`, which `at` requires of its arguments", with the fix "check the arguments first with an `if`, or state it in a `requires`". A matrix is read the same way, with a `requires` for each size.

```
fn entry[m: Nat, k: Nat](a: [m, k]f32, r: Nat, c: Nat) -> f32
  requires r < m and c < k
{
  a[r, c]
}
```

## What an index is

An index is a whole number of an unsigned type. A `u8` is one. An `i32` is not, and `xs[i]` with `i: i32` is refused: "an index is a whole number of an unsigned type". A `size` is a `u64`, as [Arrays and sizes](arrays.md) says, so an `i32` also does not compare with it.

Only a type that implements `Index` can be indexed. With `n: u8`, `n[0]` is refused: "an index is read from a list, and this is a `u8`".

## Indexing is a trait

Indexing is a trait function, as an operator is. `x[i]` means `at(x, i)` from the `Index` trait, so any type may implement it, and nothing about the brackets of an array is special.

```
trait Index[C, I, out T] {
  fn valid(c: C, i: I) -> bool
  fn at(c: C, i: I) -> T
    requires valid(c, i)
}
```

`valid` says which indices a container has, and `at` requires it. For an array, `valid` is that the index is below the size. That is why `x[i]` is written with nothing beside it, and compiles when `arith` proves `valid(x, i)`. `valid` is used to prove, and it is never run for an index.

A [`List`](../library/list.md) implements `Index` with the index below `size`, and a text implements it for a range, as [Ranges and slices](ranges.md) shows. A type whose `at` answers for every index, as one that returns an `Option` does, has a `valid` that is `true`. A [`Map`](../library/map.md) is one, so `m[key]` is `.some` of the value or `.none`, and is never refused.

## Assigning an element

An element of a `var` array is assigned as a field is, and only that `var` sees the change.

```
fn bump(a: [3]f32) -> [3]f32 {
  var b = a
  b[0] = 1.0
  b
}
```

`bump` gives `a` with its first element `1.0`, and the `a` that was passed is unchanged.

## The forms

| Written | Means |
| --- | --- |
| `x[i]` | `at(x, i)`, the element at place `i`, compiled when `valid(x, i)` is proven |
| `a[r, c]` | The element of a matrix at row `r` and column `c` |
| `a[0] = e` | Assigns an element of a `var` array |
| `valid(x, i)` | Whether `x` has the index `i`, used to prove and never run |
| `loop i in 0..xs.size`, `if i < xs.size`, `requires i < xs.size` | The three ways to prove an index |

Next: [Ranges and slices](ranges.md)
