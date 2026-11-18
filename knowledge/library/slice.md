---
type: specification
title: Slice
description: Slice, a run of elements that cannot change and so is shared freely, made from a List by freeze with nothing copied, and how a function that only reads takes either.
status: planned
---

# Slice

A `Slice[T]` is a run of elements that cannot change. A [`List`](list.md) is what a program builds, and a `Slice` is what it has when the building is done.

```
fn evens(n: u64) -> Slice[u64] ! alloc {
  var out: List[u64] = []
  loop i in 0..n { out = push(out, wrapping_mul(i, 2)) }
  freeze(out)
}
```

`evens(3)` is the slice `[0, 2, 4]`. `freeze` takes the list and gives the same memory back as a `Slice`, so nothing is copied, and `out` is gone, so nothing can grow it or assign into it afterwards.

```
fn freeze[T](list: take List[T]) -> Slice[T]
```

## Memory is owned by one name, or it cannot change

That is the whole rule, and it is why nothing has to follow which name can see which memory. A `List` is owned: one name holds it, it is [never copied unasked](../language/ownership.md#a-list-is-never-copied-unasked), and `push` grows it where it is. A `Slice` cannot change: it has `Copy`, so any number of names may hold it, and none of them can see it change.

## A piece of a slice is a slice

`s[a..b]` is the elements from `a` up to `b` as a `Slice`, which shares the memory of `s`. It is kept, stored, and returned like any value.

```
struct Halves {
  left: Slice[u64]
  right: Slice[u64]
}

fn halves(s: Slice[u64]) -> Halves {
  mid = s.size / 2
  Halves(left: s[..mid], right: s[mid..])
}
```

Both fields share the memory of `s`, and nothing is copied.

## A function that only reads takes a List

A function that reads a run of elements and keeps none of it takes a `List[T]`, which lends. It is given a `List`, a piece of a list, or a `Slice`, and each costs nothing.

```
fn total(xs: List[u64]) -> u64 {
  var t: u64 = 0
  loop i in 0..xs.size { t = wrapping_add(t, xs[i]) }
  t
}

fn demo(building: List[u64], done: Slice[u64]) -> u64
  requires building.size >= 5
{
  wrapping_add(wrapping_add(total(building), total(building[2..5])), total(done))
}
```

A lent value is not kept, stored, returned, or grown, as [Lending and take](../language/take.md) says, so it makes no difference to `total` whether the memory it reads is frozen. So the functions that read, such as those of [`std.list`](list-functions.md), are written once. A function that keeps a piece says so by taking a `Slice[T]`.

## From one to the other

| From | To | Written | Costs |
| --- | --- | --- | --- |
| `List[T]` | `Slice[T]` | `freeze(xs)` | Nothing, and `xs` is given up |
| `Slice[T]` | `List[T]` | `copy(s)` | A copy of the elements, and `alloc` |
| A piece of a `List[T]` | `List[T]` | `copy(xs[a..b])` | A copy of the elements, and `alloc` |

A piece of a `List` is lent like the list itself: it is passed, looped over, and read, and it is not given a name or stored. A program that keeps a piece of a list either copies it or freezes the list first.

## The forms

| Written | Is |
| --- | --- |
| `Slice[T]` | A run of `T` that cannot change, with `Copy` |
| `freeze(xs)` | The memory of the list `xs` as a `Slice`, with nothing copied |
| `s[a..b]` on a `Slice` | A `Slice` that shares the memory of `s` |
| `xs[a..b]` on a `List` | A lent piece of `xs`, for a function that reads |
| `copy(s)` | A new `List` with the elements of `s` |
