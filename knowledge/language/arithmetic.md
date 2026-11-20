---
type: specification
title: Integer arithmetic
description: The exact range that a plain +, -, *, or / gives, the proof asked where a value enters a narrower range, narrow and clamp where nothing proves it or the value is a float, a range of up to 128 bits, and the functions that wrap.
status: partial
built:
  - "a plain `+`, `-`, or `*` of two whole numbers, a literal included, which gives the exact range of the result with nothing proven where a type of 64 bits holds it, and where none does, as of two numbers of 64 bits, takes two sides of one type and compiles when `arith` proves that the result fits that type, and an error that says what could not be proven"
  - "`wrapping_`, `saturating_`, and `checked_` of `add`, `sub`, and `mul`"
  - "a value that enters a narrower range, at a binding, a `var`, an argument, and a result, which compiles when a condition or a `requires` shows that it fits, and an error that says what could not be proven"
  - "a unary `-`, which gives the negated range and groups tighter than `*`"
  - "not the exact range of a result that no type of 64 bits holds, `/`, `%`, or `narrow(x)` and `clamp(x)` of every range, or of a float, or a range of 65 to 128 bits, which is refused as a wider one is"
---

# Integer arithmetic

A plain `+`, `-`, or `*` gives the exact range of its result, so it cannot fail and asks for nothing.

```
use std.console (console, print)

fn main() ! console, alloc {
  a: 0..256 = 20
  b: 0..256 = 5
  print("{a + b} {a - b} {a * b}")
}
```

This prints `25 15 100`. The result of `a + b` is a `0..511`, of `a - b` a `-255..256`, and of `a * b` a `0..65026`, whatever `a` and `b` hold.

## The range of a result

The range of `a + b` runs from the least of `a` plus the least of `b` to the greatest of `a` plus the greatest of `b`. The range of `a - b` runs from the least of `a` less the greatest of `b` to the greatest of `a` less the least of `b`. The range of `a * b` runs from the least to the greatest of the four products of the ends.

```
fn sum(a: 0..256, b: 0..256) -> 0..511 { a + b }
```

`sum(255, 255)` is `510`. A literal is the range that holds it alone, as [Literals](literals.md) says, so `a + 1` of a `0..256` is a `1..257`.

## Where a value enters a narrower range

A proof is asked for only where a value enters a range narrower than its own. That is a binding or a `var` with a stated type, an argument of a narrower parameter, and a narrower result.

```
fn average(a: 0..256, b: 0..256) -> 0..256 {
  sum = a + b
  sum / 2
}
```

This compiles with no proof. `sum` is a `0..511`, which a binding with no stated type keeps, and `sum / 2` is a `0..256`, which the result holds.

Each of these is refused, as the value is wider than the range it enters.

```
fn next(i: 0..256) -> 0..256 { i + 1 }
```

`i + 1` is a `1..257` and the result is a `0..256`: `i` may be 255. The result is where it enters.

```
fn narrow_binding(i: 0..256) {
  level: 0..101 = i
}
```

```
fn takes(level: 0..101) -> 0..101 { level }

fn call(i: 0..256) -> 0..101 { takes(i) }
```

The binding states `0..101`, and the argument of `takes` is a `0..101`, so each is where `i` enters a narrower range. The compiler states what it could not prove, such as that `i + 1` is below 256.

## Checking first

A condition that bounds the value is a proof, inside the block it guards.

```
use std.console (console, print)

fn next(i: 0..256) -> 0..256 {
  if i < 255 { i + 1 } else { 255 }
}

fn gap(a: 0..256, b: 0..256) -> 0..256 {
  if a >= b { a - b } else { 0 }
}

fn main() ! console, alloc {
  print("{next(7)} {next(255)} {gap(5, 3)} {gap(3, 5)}")
}
```

This prints `8 255 2 0`. In the first block of `gap`, `a >= b` holds, so `a - b` is at least 0, and in the `else` the subtraction is not written. The same holds for a condition with `and`, `or`, and `not`. What the compiler can prove appears under [What arith proves](arith.md), and how to state a fact once for a whole function under [Contracts](contracts.md).

## Entering a range with no proof

Where nothing proves that a value fits the range it enters, as a number read from outside, two functions of [`std.int`](../library/int.md) take it in. Each takes a value of any range and gives the range that is expected where it is written. They serve every operator and any expression, and each costs the comparisons it says when the program runs.

`narrow(x)` gives an `Option` of the range expected: `.some(x)` when `x` is in that range, and `.none` when it is not. `checked_add(a, b)` is `narrow(a + b)`.

```
use std.int (narrow)

fn to_byte(n: Nat) -> Option[Byte] { narrow(n) }

fn next_checked(i: Byte) -> Option[Byte] { narrow(i + 1) }
```

`to_byte(200)` is `.some(200)` and `to_byte(300)` is `.none`. `next_checked(7)` is `.some(8)` and `next_checked(255)` is `.none`.

`clamp(x)` gives the value of the range expected that is nearest `x`: `x` when it is in the range, the least value when `x` is below it, and the greatest when it is above. `saturating_add(a, b)` is `clamp(a + b)`.

```
use std.console (console, print)

fn next_saturating(i: Byte) -> Byte { clamp(i + 1) }

fn floor(x: Int) -> Byte { clamp(x) }

fn main() ! console, alloc {
  print("{next_saturating(255)} {next_saturating(7)} {floor(-3)} {floor(300)}")
}
```

This prints `255 8 0 255`. `floor(-3)` is `0`, the least `Byte`, and `floor(300)` is `255`, the greatest.

Where the range expected holds every value of `x`, the call is refused as needless, as `convert` of a value to its own type is, since it does nothing the program could see.

```
use std.int (narrow)

fn same(x: Byte) -> Option[Byte] { narrow(x) }
```

This is refused, as `Byte` holds every value of `x`. A call with no range expected is refused too, as `n = narrow(x)` with nothing that states the range of `n`.

## A float entering a range

A float becomes a whole number through the same two functions and in no other way. A float may hold a fraction, a number outside every range, or no number at all, so it is never taken as a whole number with nothing written.

`narrow(x)` of a float gives `.some` only when `x` is exactly a whole number of the range expected. A float with a fraction, one outside the range, an infinity, and a float that is not a number each give `.none`.

```
use std.int (narrow)

fn seats(x: f64) -> Option[0..500] { narrow(x) }
```

`seats(12.0)` is `.some(12)`. `seats(12.5)`, `seats(500.0)`, and `seats(-1.0)` are `.none`.

`clamp(x)` of a float gives the value of the range expected that is nearest `x`. A float with a fraction goes to the nearest whole number, and one that is exactly half way between two goes to the even one. A float below the range gives the least value, and one above it the greatest, as an infinity does. A float that is not a number gives the greatest value, as it comes after every other float where floats are ordered, as [Comparisons](comparisons.md) says.

```
use std.console (console, print)
use std.int (clamp)

fn level(x: f64) -> Byte { clamp(x) }

fn main() ! console, alloc {
  print("{level(2.4)} {level(2.5)} {level(3.5)} {level(-7.0)} {level(1e30)}")
}
```

This prints `2 2 4 0 255`. `2.5` is half way between `2` and `3` and goes to `2`, the even one, and `3.5` goes to `4`.

A program that wants another rounding rounds the float first, with `floor`, `ceil`, `trunc`, or `round` of [`std.float`](../library/float.md), each of which gives a float.

```
use std.float (floor)
use std.int (clamp)

fn level_down(x: f64) -> Byte { clamp(floor(x)) }
```

`level_down(2.9)` is `2`.

## More than 64 bits

A range of up to 128 bits is a range like any other. So the result of an operator is never lost for want of room in 64 bits: the product of two `Nat`s is the whole product, and a sum with a carry is the whole sum.

```
fn times(a: Nat, b: Nat) -> (Nat, Nat) {
  p = a * b
  (p / 18446744073709551616, p % 18446744073709551616)
}
```

`p` is a `0..340282366920938463426481119284349108226`, which holds the product of any two `Nat`s. Divided by 2 to the 64 it is below 2 to the 64, and so is what is left over, so each is a `Nat` with nothing to prove. `times(18446744073709551615, 2)` is `(1, 18446744073709551614)`: the high half and the low half of the product. The multiplication is the one instruction a processor has for it, and the two halves are the two words it gives.

A wide result enters a range of 64 bits as any value enters a narrower range: where it is shown to fit, or by `narrow` or `clamp`.

```
fn doubled(n: Nat) -> Nat { n * 2 }
```

This is refused: `n * 2` is a `0..36893488147419103231`, and `n` may be too great for it to fit a `Nat`.

```
fn doubled(n: Nat) -> Nat
  requires n < 1000
{
  n * 2
}
```

`doubled(21)` is `42`.

Only what is wide costs more. A range of more than 64 bits is kept in two words, and a program that has none is the program it was. Adding, subtracting, and comparing two wide numbers are two instructions each, and a product whose result is wide is one. Dividing a wide number by a number that is not a power of two is a function of many instructions, on every processor, and is slow beside every other operator.

A result whose range needs more than 128 bits has no type that holds it, and is refused until it is narrowed. A number of more bits than that, as a key of a signature is, is a type of the library, made of `Nat`s and worked on with the operators above.

## Dividing

`a / b` divides, and `a % b` is what is left over. Both round toward zero, which is what a processor's instruction gives, so `7 / 2` is `3` and `7 % 2` is `1`, and `-7 / 2` is `-3` and `-7 % 2` is `-1`. What is left over has the sign of the first operand, so `(a / b) * b + a % b` is always `a`. Both give the exact range of their results.

No number is divided by zero. `a / b` and `a % b` compile only when the range of `b` excludes zero, and nothing is checked when the program runs.

```
fn share(total: Nat, n: 1..101) -> Nat { total / n }
```

`share(10, 4)` is `2`. A divisor that is a literal other than `0` is a range that excludes zero, so `total / 4` compiles. A divisor that may be zero is refused.

```
fn share(total: Nat, n: Nat) -> Nat { total / n }
```

This is refused, as the range of `n` includes 0. A divisor enters a narrower range as any value does, so a check first is a proof.

```
fn share_of(total: Nat, n: 0..101) -> Nat {
  if n > 0 { share(total, n) } else { 0 }
}
```

`share_of(10, 4)` is `2` and `share_of(10, 0)` is `0`. In the block, `n > 0` holds, so `n` enters the `1..101` that `share` asks for.

## Wrapping

Nothing wraps unless a function that wraps is called. `wrapping_add`, `wrapping_sub`, and `wrapping_mul` keep the low bits of the result, and they are functions of the ranges that fill a size exactly, `0..256`, `0..65536`, and the others that [Bit operations](bits.md#a-number-that-may-be-negative-and-bit_not) lists.

```
use std.console (console, print)
use std.int (wrapping_add, wrapping_mul, wrapping_sub)

fn next_wrapping(i: Byte) -> Byte { wrapping_add(i, 1) }

fn main() ! console, alloc {
  x: Byte = 0
  y: -128..128 = 100
  print("{next_wrapping(255)} {next_wrapping(7)}")
  print("{wrapping_sub(x, 1)} {wrapping_mul(y, 2)}")
}
```

This prints `0 8` and `255 -56`. The `1` is a `Byte`, as it is a range that fits one.

```
use std.int (wrapping_add)

fn next(i: 0..101) -> 0..101 { wrapping_add(i, 1) }
```

This is refused, as `0..101` does not fill a size, so it has no wrapping function. A wrapping function takes two values of one range, written the same, and a wider range is not accepted for a narrower one here, as it is elsewhere, since widening one would change where a sum wraps.

```
use std.int (wrapping_add)

fn bump(a: Byte, b: 0..65536) -> 0..65536 { wrapping_add(a, b) }
```

This is refused, as `a` and `b` are of two ranges. The fix is to bind the narrower with the type of the other, as `wide: 0..65536 = a`.

## The forms

| Written | Is |
| --- | --- |
| `a + b`, `a - b`, `a * b` | The exact range of the result, which cannot fail |
| `a / b`, `a % b` | The exact range, with the range of `b` excluding zero |
| A value where a narrower range is expected | Compiles when it is shown to fit, refused otherwise |
| A result of 65 to 128 bits | A range, kept in two words |
| A result of more than 128 bits | Refused until it is narrowed |
| `narrow(x)` | `.some(x)` when `x` is in the range expected, `.none` when it is not |
| `clamp(x)` | The value of the range expected that is nearest `x` |
| `wrapping_add(a, b)`, `wrapping_sub(a, b)`, `wrapping_mul(a, b)` | The low bits, for a range that fills a size |

Next: [Bit operations](bits.md)
