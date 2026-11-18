---
type: specification
title: Ranges and slices
description: A range a..b runs from a up to but not including b, and x[a..b] is the part of x between them, with x[..k] and x[k..] as short forms.
status: partial
built:
  - "`a..b` as the range of a `loop`, which runs in order and not at all when `a >= b`"
  - "not a range outside a `loop`, or a slice"
---

# Ranges and slices

A range `a..b` is the numbers from `a` up to `b`, and it excludes `b`. A slice `x[a..b]` is the part of `x` that a range picks out.

## A range

`0..3` is `0`, `1`, and `2`. It has three numbers, and `3` is not one of them. A range that begins where it ends, as `3..3`, is empty.

A [`loop`](loop.md) runs over a range.

```
fn main() ! console, alloc {
  loop i in 0..3 { print("{i}") }
}
```

This prints `0`, `1`, and `2`. A range whose first end is not below its second, as `3..3` or `5..2`, runs the body no times.

## A slice

`x[a..b]` is the elements of `x` from place `a` up to place `b`, so its size is `b - a`.

```
fn middle(a: [4]u8) -> [2]u8 { a[1..3] }
```

`middle([10, 20, 30, 40])` is `[20, 30]`. Its size is `3 - 1`, which is `2`, so it is a `[2]u8`.

A slice is read through [`Index`](indexing.md), with a range as the index. The slice of a text is a `str`: [Text](text.md) shows `s[a..b]`.

## A slice must be proven inside

A slice compiles only when [`arith`](arith.md) proves that the range lies inside: that its first end is not above its second, and that its second is not above the size. This is the `valid` of `Index` for a range, as [Indexing](indexing.md#indexing-is-a-trait) says of one index, so nothing is checked when the program runs.

```
fn word(src: List[u8], from: u64, to: u64) -> List[u8]
  requires from <= to and to <= src.size
{
  src[from..to]
}
```

`word("let x", 4, 5)` is the one byte of `x`. Without the `requires`, `src[from..to]` is refused, and the error states what is needed, as it does for an index. The three ways to supply it are those of an index: the range of a `loop`, an `if`, or a `requires`.

## A slice of a list

A slice of a [`List`](../library/list.md) is a `List` of the same elements, and it shares the memory of the list it was cut from, so nothing is copied. Values never change, so nothing can be seen through the shared memory that was not there when the slice was made.

```
fn first_two(xs: List[u32]) -> List[u32]
  requires xs.size >= 2
{
  xs[0..2]
}
```

`first_two([4, 9, 6])` is `[4, 9]`. A slice has no room of its own, so a `push` to it copies its elements first, as a `push` to any list held inside another value does.

## Leaving an end out

Where the range begins at the start, the first end is left out, and where it ends at the end, the second is. `x[..k]` is `x[0..k]`, and `x[k..]` is `x[k..x.size]`. The short form is the one that is written: the formatter rewrites `x[0..k]` as `x[..k]` and `x[k..x.size]` as `x[k..]`.

```
fn drop_one(a: [4]u8) -> [3]u8 { a[1..] }

fn take_two(a: [4]u8) -> [2]u8 { a[..2] }
```

`drop_one([10, 20, 30, 40])` is `[20, 30, 40]`, and `take_two` of it is `[10, 20]`.

## The forms

| Written | Is |
| --- | --- |
| `a..b` | The numbers from `a` up to `b`, excluding `b` |
| `x[a..b]` | The elements of `x` from `a` up to `b`, of size `b - a`, compiled when `a <= b` and `b <= x.size` are proven |
| `xs[a..b]` on a `List` | A `List` that shares the memory of `xs` |
| `x[..k]` | `x[0..k]`, which the formatter writes this way |
| `x[k..]` | `x[k..x.size]`, which the formatter writes this way |

Next: [Patterns in brackets](array-patterns.md)
