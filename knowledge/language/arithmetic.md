---
type: specification
title: Integer arithmetic
description: The exact range that a plain +, -, *, or / gives, the proof asked where a value enters a narrower range, and the functions that wrap.
status: partial
built:
  - "a plain `+`, `-`, or `*` on `u8` to `i64`, which compiles when `arith` proves that the result fits the type of its operands, and an error that says what could not be proven"
  - "`wrapping_`, `saturating_`, and `checked_` of `add`, `sub`, and `mul`"
  - "not a range as a type, the exact range of a result, `/`, or `%`"
---

# Integer arithmetic

A plain `+`, `-`, or `*` gives the exact range of its result, so it cannot fail and asks for nothing.

```
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

## More than 64 bits

A result whose range needs more than 64 bits has no type that holds it, so it is refused until it is narrowed. It is narrowed where it enters a range of 64 bits or fewer, with the proof that it fits.

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

Nothing wraps unless a function that wraps is called. `wrapping_add`, `wrapping_sub`, and `wrapping_mul` keep the low bits of the result, and they are functions of the ranges that fill a size exactly, `0..256`, `0..65536`, and the others that [Bit operations](bits.md#the-ranges-that-fill-a-size) lists.

```
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
fn next(i: 0..101) -> 0..101 { wrapping_add(i, 1) }
```

This is refused, as `0..101` does not fill a size, so it has no wrapping function.

## The forms

| Written | Is |
| --- | --- |
| `a + b`, `a - b`, `a * b` | The exact range of the result, which cannot fail |
| `a / b`, `a % b` | The exact range, with the range of `b` excluding zero |
| A value where a narrower range is expected | Compiles when it is shown to fit, refused otherwise |
| A result of more than 64 bits | Refused until it is narrowed |
| `wrapping_add(a, b)`, `wrapping_sub(a, b)`, `wrapping_mul(a, b)` | The low bits, for a range that fills a size |

Next: [Bit operations](bits.md)
