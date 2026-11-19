---
type: specification
title: Bit operations
description: The functions that and, or, xor, and not the bits of a range that fills a size and shift them, with a shift proven to stay inside the range.
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

## The ranges that fill a size

`Bits` is implemented for the ranges that are all the values of 8, 16, 32, or 64 bits, signed or not, and for no other range.

| Unsigned | Signed |
| --- | --- |
| `0..256`, the `Byte` | `-128..128` |
| `0..65536` | `-32768..32768` |
| `0..4294967296` | `-2147483648..2147483648` |
| `0..18446744073709551616`, the `Nat` | `-9223372036854775808..9223372036854775808`, the `Int` |

The names of these are the sizes, `u8` to `i64`, in [`std.bits`](../library/bits.md). A function over any other range is refused, as a range of fewer values has bits that no function can leave alone.

```
fn mask(x: 0..101) -> 0..101 { bit_and(x, 7) }
```

This is refused, as `0..101` does not fill a size. A foreign declaration is written with these ranges, and they are always kept as themselves, as [How a whole number is kept](../compiler.md#how-a-whole-number-is-kept) says.

## And, or, xor, and not

`bit_and` keeps the bits that are set in both numbers, `bit_or` those set in either, `bit_xor` those set in one only, and `bit_not` turns every bit around.

```
fn main() ! console, alloc {
  a: Byte = 0b1100
  b: Byte = 0b1010
  print("{bit_and(a, b)} {bit_or(a, b)} {bit_xor(a, b)} {bit_not(a)}")
}
```

This prints `8 14 6 243`, which is `0b1000`, `0b1110`, `0b0110`, and `0b11110011`.

A function does the common job of picking a part out of a number.

```
fn low_byte(x: u32) -> u32 { bit_and(x, 0xff) }

fn main() ! console, alloc {
  x: u32 = 0x12345678
  print("{low_byte(x)}")
}
```

This prints `120`, which is `0x78`.

## Shifting

`shift_left` moves the bits up by `by` places, and `shift_right` moves them down. `shift_left` drops the bits that leave the top. `shift_right` keeps the sign of a signed number and fills an unsigned one with zeros.

```
fn second_byte(x: u32) -> u32 { bit_and(shift_right(x, 8), 0xff) }

fn main() ! console, alloc {
  a: Byte = 0b1100
  u: Byte = 0b1111_0000
  s: i8 = -8
  print("{second_byte(0x12345678)}")
  print("{shift_left(a, 2)} {shift_right(a, 2)}")
  print("{shift_left(u, 1)} {shift_right(u, 1)}")
  print("{shift_left(s, 1)} {shift_right(s, 1)}")
}
```

This prints `86`, `48 3`, `224 120`, and `-16 -4`. The `240` of `u` loses its top bit going left, and a zero comes in at the top going right. The `-8` of `s` stays negative going right.

## An amount that must be proven

A shift compiles only when `by` is proven to be below the number of bits in the range, as an index is proven below a size. Nothing tests it when the program runs.

```
fn up(x: u32, by: Byte) -> u32 {
  shift_left(x, by)
}
```

This is refused: `by` may be 255, which a shift of a `u32` does not allow. A condition proves it for the block it guards.

```
fn up(x: u32, by: Byte) -> u32 {
  if by < 32 { shift_left(x, by) } else { 0 }
}

fn main() ! console, alloc {
  print("{up(1, 4)} {up(1, 40)}")
}
```

This prints `16 0`. An amount that is a number is proven by itself, so `shift_left(x, 8)` on a `Byte` is refused, as 8 is not below 8.

## Packing small numbers

Small numbers are packed into a larger one with `+` and `*`, as in `reg * 8 + rm`. Their ranges say that they do not run into each other, so nothing is proven. It compiles to the same shifts.

```
fn pack(reg: 0..8, rm: 0..8) -> 0..64 { reg * 8 + rm }

fn main() ! console, alloc {
  print("{pack(3, 5)}")
}
```

This prints `29`. The range of `reg * 8 + rm` is `0..64`, which is the result.

## What is known of a result

What `bit_and`, `bit_or`, `bit_xor`, and the shifts give has a range that `arith` knows, so `bit_and(x, 31) + 200` of a `Byte` fits a `Byte`. [What arith proves](arith.md#what-it-starts-from) says what is known of each.

## The forms

| Written | Is |
| --- | --- |
| `bit_and(a, b)` | The bits set in both |
| `bit_or(a, b)` | The bits set in either |
| `bit_xor(a, b)` | The bits set in one only |
| `bit_not(a)` | Every bit turned around |
| `shift_left(a, by)` | The bits moved up, those past the top dropped |
| `shift_right(a, by)` | The bits moved down, filled with the sign or with zeros |
| Any of them on a range that does not fill a size | Refused |

Next: [Conversion](conversion.md)
