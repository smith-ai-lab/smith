---
type: specification
title: Whole number functions
description: The functions of std.int: narrow and clamp, which take a value into the range expected, and the arithmetic that wraps for the ranges that fill a size.
status: planned
---

# Whole number functions

The module `std.int` holds `narrow` and `clamp`, which take a value into the range expected, and the functions that wrap. A plain `+` gives the exact range of its result and cannot fail, as [Integer arithmetic](../language/arithmetic.md) says, and nothing wraps unless one of the functions that wrap is called.

```
use std.int (clamp, narrow, wrapping_add)

fn next(i: Byte) -> Byte { wrapping_add(i, 1) }

fn next_or_none(i: Byte) -> Option[Byte] { narrow(i + 1) }

fn next_stopping(i: Byte) -> Byte { clamp(i + 1) }
```

`next(255)` is `0`, `next_or_none(255)` is `.none`, and `next_stopping(255)` is `255`.

## What it holds

| Name | Gives |
| --- | --- |
| `narrow` | An `Option` of the range expected: `.some` of the value when it is in it, `.none` when it is not |
| `clamp` | The value of the range expected that is nearest the value |

Both take a value of any range, or a float, as [A float entering a range](../language/arithmetic.md#a-float-entering-a-range) says. [Integer arithmetic](../language/arithmetic.md#entering-a-range-with-no-proof) says more.

| Does | Wraps |
| --- | --- |
| Adds | `wrapping_add` |
| Subtracts | `wrapping_sub` |
| Multiplies | `wrapping_mul` |
| Divides | `wrapping_div` |
| Gives what is left over | `wrapping_rem` |

Each function that wraps is a function of the ranges that fill a size exactly, `0..256`, `0..65536`, and the others that [Bit operations](../language/bits.md#the-ranges-that-fill-a-size) lists, and of no other range. A `Nat` and an `Int` fill a size, so each has them. The functions on bits are in [`std.bits`](bits.md).
