---
type: specification
title: Bit functions
description: The functions of std.bits, which work on the bits of a whole number through the Bits trait.
status: planned
---

# Bit functions

The module `std.bits` holds the `Bits` trait and its six functions, which work on a whole number as a row of bits. [Bits](../language/bits.md) says what each gives.

```
use std.bits (bit_and, shift_right)

fn second_byte(x: u32) -> u32 { bit_and(shift_right(x, 8), 0xff) }
```

`second_byte(0x1234)` is `0x12`.

## What it holds

| Name | Gives |
| --- | --- |
| `Bits` | The trait, which every whole number type has |
| `bit_and`, `bit_or`, `bit_xor` | The bits of two numbers joined, place by place |
| `bit_not` | Every bit turned over |
| `shift_left`, `shift_right` | The bits moved by a number of places |

A function is imported by its own name, as any function of a trait is, so a file that only calls `bit_and` lists `bit_and` alone.

Arithmetic that wraps, saturates, or is checked is in [`std.int`](int.md).
