---
type: specification
title: Slice
description: Slice, a run of elements that cannot change and so is shared freely, made from a List by freeze with nothing copied, and how a function that only reads takes either.
status: partial
built:
  - "`Slice[T]`, a type written like `List[T]`, which has `Copy` and `Discard`, so it is bound, stored in a struct, a tuple, or an enum, passed, and returned freely, and `freeze(xs)`, which takes a list and gives its memory as a `Slice` with nothing copied: the address and the size, and no instruction"
  - "`s[a..b]`, `s[..k]`, and `s[k..]` on a `Slice`, a `Slice` that shares the memory of `s`, which compiles when `arith` proves `a <= b` and `b <= s.size`, and has `b - a` elements, with `size`, an index, and `loop` on a slice as on a list"
  - "a slice given to a parameter of type `List[T]` that is not `take`, as a list and a text are, with no call and no copy, and refused where the parameter takes a list, with the fix `copy`; `copy(s)` gives a new `List`, carrying `alloc`"
  - "a slice written out of whole numbers where a `Slice` is expected, which is data in the executable with no `alloc` and no request for memory"
  - "no `push` on a slice, and no assignment to an element, which no list has yet either"
  - "not a piece of a `List`, a slice written out of anything but whole numbers written out, `Eq`, `Fmt`, `Hash`, `Repr`, or `Serialize` of a slice, or a `str` made of one"
---

# Slice

A `Slice[T]` is a run of elements that cannot change. A [`List`](list.md) is what a program builds, and a `Slice` is what it has when the building is done.

```
use std.bits (u64)
use std.int (wrapping_mul)
use std.list (push)

fn evens(n: u64) -> Slice[u64] ! alloc {
  var built: List[u64] = []
  loop i in 0..n { built = push(built, wrapping_mul(i, 2)) }
  freeze(built)
}
```

`evens(3)` is the slice `[0, 2, 4]`. `freeze` takes the list and gives the same memory back as a `Slice`, so nothing is copied, and `built` is gone, so nothing can grow it or assign into it afterwards.

```
fn freeze[T](list: take List[T]) -> Slice[T]
```

## A slice written out

A literal in brackets becomes a `Slice` where one is expected, as it becomes a `List` where a list is, since the type that is expected picks what a [literal](../language/literals.md) makes.

```
primes: Slice[u8] = [2, 3, 5, 7]
```

A slice cannot change, so its elements are data in the executable, as the bytes of a string literal are. Nothing is asked of the system for it, and it carries no `alloc`. A `List` written the same way allocates, as it may grow. With nothing expected, `[2, 3, 5, 7]` is an array of four elements, as it always was.

## Memory is owned by one name, or it cannot change

That is the whole rule, and it is why nothing has to follow which name can see which memory. A `List` is owned: one name holds it, it is [never copied unasked](../language/ownership.md#a-list-is-never-copied-unasked), and `push` grows it where it is. A `Slice` cannot change: it has `Copy`, so any number of names may hold it, and none of them can see it change.

## A piece of a slice is a slice

`s[a..b]` is the elements from `a` up to `b` as a `Slice`, which shares the memory of `s`. It is kept, stored, and returned like any value.

```
use std.bits (u64)

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
| `s: Slice[u8] = [2, 3]` | A slice whose elements are data in the executable, with no `alloc` |
| `s[a..b]` on a `Slice` | A `Slice` that shares the memory of `s` |
| `xs[a..b]` on a `List` | A lent piece of `xs`, for a function that reads |
| `copy(s)` | A new `List` with the elements of `s` |
