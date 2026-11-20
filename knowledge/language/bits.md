---
type: specification
title: Bit operations
description: The functions that and, or, xor, and shift the bits of a whole number and give the exact range of what they make, as arithmetic does, and what a number that may be negative and bit_not ask of a range.
status: partial
built:
  - "`bit_and`, `bit_or`, `bit_xor`, `bit_not`, `shift_left`, and `shift_right` on `u8` to `u64` and `i8` to `i64`, a shift compiling when `arith` proves that its amount is below the number of bits"
  - "not a range as a type, or a range that fills a size written as a range"
---

# Bit operations

The bits of a whole number are worked on by named functions, those of the `Bits` trait. There are no operators for them.

```
trait Bits[T] {
  fn bit_and(a: T, b: T) -> T
  fn bit_or(a: T, b: T) -> T
  fn bit_xor(a: T, b: T) -> T
  fn bit_not(a: T) -> T
  fn shift_left(a: T, by: Byte) -> T
  fn shift_right(a: T, by: Byte) -> T
}
```

## A number that is not negative

`bit_and`, `bit_or`, `bit_xor`, `shift_left`, and `shift_right` take whole numbers of any range that holds no negative number, and give the exact range of what they make, from the least number they can give to the greatest, as [arithmetic](arithmetic.md) does. So what they give enters a narrower range with nothing written, wherever its range fits.

```
use std.bits (bit_and, bit_or, shift_right)

fn low(x: Nat) -> Byte { bit_and(x, 255) }

fn lead(code: 0..2048) -> Byte { bit_or(192, shift_right(code, 6)) }

fn rest(code: 0..2048) -> Byte { bit_or(128, bit_and(code, 63)) }
```

`bit_and(x, 255)` is a `0..256` whatever `x` is. `shift_right(code, 6)` is a `0..32`, and `bit_or(192, ...)` of it is a `192..224`. Each fits a `Byte`. `lead(1000)` is `207` and `rest(1000)` is `168`.

- `bit_and(a, b)` is never above `a` and never above `b`.
- `bit_or(a, b)` is never below `a` or `b`, and never above `a + b`.
- `bit_xor(a, b)` is never above `a + b`.
- `shift_right(a, by)` is `a` divided by two to the `by`, rounded down.
- `shift_left(a, by)` is `a` times two to the `by`.

The bits of a number that is not negative are the same however wide it is kept, so the two numbers need not be of one range: `bit_and(a, c)` of a `Byte` and a `0..65536` is a `0..256`.

## A number that may be negative, and bit_not

The bits of a negative number depend on how wide it is kept, and so do the bits that `bit_not` turns on. So `bit_not`, and each of the other functions when a number it is given may be negative, takes a range that is all the values of 8, 16, 32, or 64 bits, and gives that range.

| Unsigned | Signed |
| --- | --- |
| `0..256`, the `Byte` | `-128..128` |
| `0..65536` | `-32768..32768` |
| `0..4294967296` | `-2147483648..2147483648` |
| `0..18446744073709551616`, the `Nat` | `-9223372036854775808..9223372036854775808`, the `Int` |

The names of these are the sizes, `u8` to `i64`, in [`std.bits`](../library/bits.md). These are the functions of the `Bits` trait above, which each of these ranges has and no other range has.

```
use std.bits (bit_not)

fn flip(x: 0..101) -> 0..101 { bit_not(x) }
```

This is refused, as `0..101` does not fill a size, and no function can say which bits above it to turn. Here the two numbers are of one range, written the same, as widening a number that may be negative would change what the function means.

`shift_left` of a number that may be negative is refused unless [`arith`](arith.md) proves that no bit is dropped, and `wrapping_shift_left` drops them.

## And, or, xor, and not

`bit_and` keeps the bits that are set in both numbers, `bit_or` those set in either, `bit_xor` those set in one only, and `bit_not` turns every bit around.

```
use std.bits (bit_and, bit_not, bit_or, bit_xor)
use std.console (console, print)

fn main() ! console, alloc {
  a: Byte = 0b1100
  b: Byte = 0b1010
  print("{bit_and(a, b)} {bit_or(a, b)} {bit_xor(a, b)} {bit_not(a)}")
}
```

This prints `8 14 6 243`, which is `0b1000`, `0b1110`, `0b0110`, and `0b11110011`.

A function does the common job of picking a part out of a number.

```
use std.bits (bit_and, u32)
use std.console (console, print)

fn low_byte(x: u32) -> u32 { bit_and(x, 0xff) }

fn main() ! console, alloc {
  x: u32 = 0x12345678
  print("{low_byte(x)}")
}
```

This prints `120`, which is `0x78`.

## Shifting

`shift_left` moves the bits up by `by` places, and `shift_right` moves them down. `shift_left` drops no bit: it is `a` times two to the `by`, and where that may not fit the range it goes to it is refused, as a product is. `shift_right` keeps the sign of a signed number and fills an unsigned one with zeros.

A shift that drops the bits that leave the top is `wrapping_shift_left` of [`std.int`](../library/int.md), a function of the ranges that fill a size, as `wrapping_mul` is. Nothing wraps unless a function that wraps is called.

```
use std.bits (bit_and, i8, shift_left, shift_right, u32)
use std.console (console, print)
use std.int (wrapping_shift_left)

fn second_byte(x: u32) -> u32 { bit_and(shift_right(x, 8), 0xff) }

fn main() ! console, alloc {
  a: Byte = 0b1100
  u: Byte = 0b1111_0000
  s: i8 = -8
  print("{second_byte(0x12345678)}")
  print("{shift_left(a, 2)} {shift_right(a, 2)}")
  print("{wrapping_shift_left(u, 1)} {shift_left(u, 1)} {shift_right(u, 1)}")
  print("{shift_left(s, 1)} {shift_right(s, 1)}")
}
```

This prints `86`, `48 3`, `224 480 120`, and `-16 -4`. The `240` of `u` loses its top bit in `wrapping_shift_left` and is `480` in `shift_left`, whose range is `0..511`, and a zero comes in at the top going right. The `-8` of `s` stays negative going right.

## An amount that must be proven

A shift compiles only when `by` is proven to be below 64 for a number that is not negative, and below the number of bits in the range for `wrapping_shift_left` and for a number that may be negative, as an index is proven below a size. Nothing tests it when the program runs.

```
use std.bits (shift_right, u32)

fn down(x: u32, by: Byte) -> u32 {
  shift_right(x, by)
}
```

This is refused: `by` may be 255, which no shift allows. A condition proves it for the block it guards.

```
use std.bits (shift_right, u32)
use std.console (console, print)

fn down(x: u32, by: Byte) -> u32 {
  if by < 64 { shift_right(x, by) } else { 0 }
}

fn main() ! console, alloc {
  print("{down(256, 4)} {down(256, 70)}")
}
```

This prints `16 0`. An amount that is a number is proven by itself, so `wrapping_shift_left(x, 8)` on a `Byte` is refused, as 8 is not below 8.

## Packing small numbers

Small numbers are packed into a larger one with `+` and `*`, as in `reg * 8 + rm`. Their ranges say that they do not run into each other, so nothing is proven. It compiles to the same shifts.

```
use std.console (console, print)

fn pack(reg: 0..8, rm: 0..8) -> 0..64 { reg * 8 + rm }

fn main() ! console, alloc {
  print("{pack(3, 5)}")
}
```

This prints `29`. The range of `reg * 8 + rm` is `0..64`, which is the result.

## The forms

| Written | Is |
| --- | --- |
| `bit_and(a, b)` | The bits set in both |
| `bit_or(a, b)` | The bits set in either |
| `bit_xor(a, b)` | The bits set in one only |
| `bit_not(a)` | Every bit turned around |
| `shift_left(a, by)` | The bits moved up, none dropped: `a` times two to the `by` |
| `wrapping_shift_left(a, by)` | The bits moved up, those past the top dropped, for a range that fills a size |
| `shift_right(a, by)` | The bits moved down, filled with the sign or with zeros |
| Any of them on numbers that are not negative | The exact range of the result |
| `bit_not`, or any of them on a number that may be negative, on a range that does not fill a size | Refused |

Next: [Conversion](conversion.md)
