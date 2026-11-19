---
type: specification
title: Ranges and slices
description: A range a..b runs from a up to but not including b and is also the type of those numbers, and x[a..b] is the part of x between them, with x[..k] and x[k..] as short forms.
status: partial
built:
  - "`a..b` as the range of a `loop`, which runs in order and not at all when `a >= b`"
  - "`x[a..b]`, `x[..k]`, and `x[k..]` on a [`Slice`](../library/slice.md), a `Slice` that shares the memory of `x`, compiled when `arith` proves `a <= b` and `b <= x.size`, and a piece of a piece"
  - "not a range outside a `loop` and an index, a range as a type, or a piece of an array, a text, or a list"
---

# Ranges and slices

A range `a..b` is the numbers from `a` up to `b`, and it excludes `b`. It is also the type of those numbers. A slice `x[a..b]` is the part of `x` that a range picks out.

## A range

`0..3` is `0`, `1`, and `2`. It has three numbers, and `3` is not one of them. A range that begins where it ends, as `3..3`, is empty.

A [`loop`](loop.md) runs over a range.

```
use std.console (console, print)

fn main() ! console, alloc {
  loop i in 0..3 { print("{i}") }
}
```

This prints `0`, `1`, and `2`. A range whose first end is not below its second, as `3..3` or `5..2`, runs the body no times.

## A range as a type

The numbers of `0..3` are the values of the type `0..3`, as [Number types](number-types.md) says. A bound may be a name that cannot change, as a size may, as in `0..n`.

```
fn pick[n: Nat](i: 0..n) -> Nat { i }
```

`pick` takes a number below `n`, whatever `n` is, and gives it back as a `Nat`. A `0..n` has no value when `n` is `0`, since the range has no numbers, and it is a type all the same, as [Number types](number-types.md#a-range-as-a-type) says.

## A slice

`x[a..b]` is the elements of `x` from place `a` up to place `b`, so its size is `b - a`.

```
fn middle(a: [4]Byte) -> [2]Byte { a[1..3] }
```

`middle([10, 20, 30, 40])` is `[20, 30]`. Its size is `3 - 1`, which is `2`, so it is a `[2]u8`.

A slice is read through [`Index`](indexing.md), with a range as the index. The slice of a text is a `str`: [Text](text.md) shows `s[a..b]`.

## A slice must be proven inside

A slice compiles only when [`arith`](arith.md) proves that the range lies inside: that its first end is not above its second, and that its second is not above the size. This is the `valid` of `Index` for a range, as [Indexing](indexing.md#indexing-is-a-trait) says of one index, so nothing is checked when the program runs.

```
fn word(src: List[Byte], from: u64, to: u64) -> List[Byte]
  requires from <= to and to <= src.size
{
  src[from..to]
}
```

`word("let x", 4, 5)` is the one byte of `x`. Without the `requires`, `src[from..to]` is refused, and the error states what is needed, as it does for an index. The three ways to supply it are those of an index: the range of a `loop`, an `if`, or a `requires`.

## A slice of a list

A [`List`](../library/list.md) is owned and may still grow, so a piece of it is lent, as the list itself is when it is passed: it is given to a function that reads, or looped over, and it is not given a name or stored. Nothing is copied.

```
fn first_two_total(xs: List[u32]) -> u32
  requires xs.size >= 2
{
  total(xs[..2])
}
```

`total` is a function that takes a `List[u32]` and adds its elements, so `first_two_total([4, 9, 6])` is `13`. A piece that is kept is a copy, written `copy(xs[..2])`.

A [`Slice`](../library/slice.md) cannot change, so a piece of it is a `Slice` that shares its memory and is kept like any value, as a piece of a text is.

## Leaving an end out

Where the range begins at the start, the first end is left out, and where it ends at the end, the second is. `x[..k]` is `x[0..k]`, and `x[k..]` is `x[k..x.size]`. The short form is the one that is written: the formatter rewrites `x[0..k]` as `x[..k]` and `x[k..x.size]` as `x[k..]`.

```
fn drop_one(a: [4]Byte) -> [3]Byte { a[1..] }

fn take_two(a: [4]Byte) -> [2]Byte { a[..2] }
```

`drop_one([10, 20, 30, 40])` is `[20, 30, 40]`, and `take_two` of it is `[10, 20]`.

## The forms

| Written | Is |
| --- | --- |
| `a..b` | The numbers from `a` up to `b`, excluding `b`, and the type that holds them |
| `x[a..b]` | The elements of `x` from `a` up to `b`, of size `b - a`, compiled when `a <= b` and `b <= x.size` are proven |
| `xs[a..b]` on a `List` | A lent piece of `xs`, for a function that reads |
| `s[a..b]` on a `Slice` | A `Slice` that shares the memory of `s` |
| `x[..k]` | `x[0..k]`, which the formatter writes this way |
| `x[k..]` | `x[k..x.size]`, which the formatter writes this way |

Next: [Patterns in brackets](array-patterns.md)
