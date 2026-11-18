---
type: specification
title: Whole number functions
description: The functions of std.int, which do arithmetic that wraps, saturates, or is checked, and take a whole number to a narrower type.
status: planned
---

# Whole number functions

The module `std.int` holds the functions that say by name what happens when a whole number does not fit. A plain `+` compiles only when the result is proven to fit, as [Integer arithmetic](../language/arithmetic.md) says, and these are what a program writes where it is not.

```
use std.int (checked_add, narrow, wrapping_add)

fn next(i: u8) -> u8 { wrapping_add(i, 1) }

fn next_or_none(i: u8) -> Option[u8] { checked_add(i, 1) }

fn last_digit(n: u64) -> u8 { narrow(n % 10) }
```

`next(255)` is `0`, `next_or_none(255)` is `.none`, and `last_digit(1234)` is `4`.

## What it holds

Each row is a family of names, one for each way of not fitting.

| Does | Wraps | Stops at the end of the range | Gives an `Option` |
| --- | --- | --- | --- |
| Adds | `wrapping_add` | `saturating_add` | `checked_add` |
| Subtracts | `wrapping_sub` | `saturating_sub` | `checked_sub` |
| Multiplies | `wrapping_mul` | `saturating_mul` | `checked_mul` |
| Divides | `wrapping_div` | | `checked_div` |
| Gives what is left over | `wrapping_rem` | | `checked_rem` |
| Narrows | `wrapping_narrow` | `saturating_narrow` | `checked_narrow` |

`narrow` is the plain form of the last row: it compiles only when `arith` proves that the value fits, as [Conversion](../language/conversion.md) says. What each function gives is on those two pages.

A `Nat` has the saturating and the checked functions of the table, which give what a `u64` gives, and none of the wrapping ones. The functions on bits are in [`std.bits`](bits.md).
