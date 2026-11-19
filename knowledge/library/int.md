---
type: specification
title: Whole number functions
description: The functions of std.int, which do arithmetic that wraps for the ranges that fill a size.
status: planned
---

# Whole number functions

The module `std.int` holds the functions that wrap. A plain `+` gives the exact range of its result and cannot fail, as [Integer arithmetic](../language/arithmetic.md) says, and nothing wraps unless one of these is called.

```
use std.int (wrapping_add)

fn next(i: Byte) -> Byte { wrapping_add(i, 1) }
```

`next(255)` is `0`.

## What it holds

| Does | Wraps |
| --- | --- |
| Adds | `wrapping_add` |
| Subtracts | `wrapping_sub` |
| Multiplies | `wrapping_mul` |
| Divides | `wrapping_div` |
| Gives what is left over | `wrapping_rem` |

Each is a function of the ranges that fill a size exactly, `0..256`, `0..65536`, and the others that [Bit operations](../language/bits.md#the-ranges-that-fill-a-size) lists, and of no other range. A `Nat` and an `Int` fill a size, so each has them. The functions on bits are in [`std.bits`](bits.md).
