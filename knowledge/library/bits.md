---
type: specification
title: Bit functions
description: The functions of std.bits, which work on the bits of a range that fills a size through the Bits trait, and the names of those sizes.
status: planned
---

# Bit functions

The module `std.bits` holds the names of sizes, `u8` to `u64` and `i8` to `i64`, and the `Bits` trait with its six functions, which work on a whole number as a row of bits. [Bits](../language/bits.md) says what each gives.

```
use std.bits (u32, bit_and, shift_right)

fn second_byte(x: u32) -> u32 { bit_and(shift_right(x, 8), 0xff) }
```

`second_byte(0x1234)` is `0x12`.

## What it holds

| Name | Gives |
| --- | --- |
| `u8`, `u16`, `u32`, `u64` | The ranges `0..256`, `0..65536`, `0..4294967296`, and `0..18446744073709551616` |
| `i8`, `i16`, `i32`, `i64` | The ranges `-128..128` up to the 64-bit signed range |
| `Bits` | The trait, which each range that fills a size has, and no other |
| `bit_and`, `bit_or`, `bit_xor` | The bits of two numbers joined, place by place |
| `bit_not` | Every bit turned over |
| `shift_left`, `shift_right` | The bits moved by a number of places |

A name of a size is another way to write a range, so `u8` is the `Byte`, `u64` is the `Nat`, and `i64` is the `Int`, as [Number types](../language/number-types.md#three-names) says. A file that works with sizes imports the names it uses. A function is imported by its own name, as any function of a trait is, so a file that only calls `bit_and` lists `bit_and` alone.

Arithmetic that wraps is in [`std.int`](int.md).
