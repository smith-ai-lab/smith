---
type: specification
title: Integer arithmetic
description: Why a plain +, -, or * compiles only when its result provably fits, and the functions that wrap, saturate, or check where it cannot be proven.
status: built
---

# Integer arithmetic

Integer arithmetic never overflows silently. A plain `+`, `-`, or `*` compiles only when the result provably fits the type of its operands, each operation by itself, and no check is ever inserted behind the author's back.

```
fn main() ! console, alloc {
  a: u8 = 20
  b: u8 = 5
  print("{a + b} {a - b} {a * b}")
}
```

This prints `25 15 100`. The compiler knows that `a` is 20 and `b` is 5, so each result fits a `u8`.

## When it cannot be proven

When the compiler cannot prove that the result fits, the operation is refused, and the message states what it could not prove.

```
fn next(i: u8) -> u8 { i + 1 }
```

This gives "cannot prove that `i + 1 <= 255`, so `i + 1` may not fit `u8`", with the fix "check the operands first, or use `wrapping_add`, `saturating_add`, or `checked_add`". `i` may be 255.

`-` and `*` are refused the same way, and the goal they state is the bound they could break.

```
fn gap(a: u8, b: u8) -> u8 { a - b }
```

This gives "cannot prove that `a - b >= 0`, so `a - b` may not fit `u8`".

```
fn area(w: u8, h: u8) -> u8 { w * h }
```

This gives "cannot prove that `w * h <= 255`, so `w * h` may not fit `u8`".

## Checking first

A condition that bounds the operands is a proof, inside the block it guards.

```
fn gap(a: u8, b: u8) -> u8 {
  if a >= b { a - b } else { 0 }
}

fn main() ! console, alloc {
  print("{gap(5, 3)} {gap(3, 5)}")
}
```

This prints `2 0`. In the first block `a >= b` holds, so `a - b` is at least 0, and in the `else` the subtraction is not written. The same holds for a condition with `and`, `or`, and `not`. Most integer arithmetic is on indices and sizes, whose limits are already in their types, so the proof is usually automatic. What the compiler can prove appears under [What arith proves](arith.md), and how to state a fact once for a whole function under [Contracts](contracts.md).

## Choosing what happens

Where the author does not want to check first, the behaviour is picked by name. Each exists for `add`, `sub`, and `mul`, as the functions of the traits `Wrapping`, `Saturating`, and `Checked`, which every integer type implements.

```
// 255 becomes 0.
fn next_wrapping(i: u8) -> u8 { wrapping_add(i, 1) }

// 255 stays 255.
fn next_saturating(i: u8) -> u8 { saturating_add(i, 1) }

// 255 gives .none.
fn next_checked(i: u8) -> Option[u8] { checked_add(i, 1) }

fn main() ! console, alloc {
  print("{next_wrapping(255)} {next_wrapping(7)}")
  print("{next_saturating(255)} {next_saturating(7)}")
  match next_checked(255) {
    .some(n) { print("some {n}") }
    .none { print("none") }
  }
  match next_checked(7) {
    .some(n) { print("some {n}") }
    .none { print("none") }
  }
}
```

This prints `0 8`, `255 8`, `none`, and `some 8`. The same holds below zero and at the other end of a type.

```
fn main() ! console, alloc {
  x: u8 = 0
  y: i8 = 100
  print("{wrapping_sub(x, 1)} {saturating_sub(x, 1)}")
  print("{wrapping_mul(y, 2)} {saturating_mul(y, 2)}")
}
```

This prints `255 0` and `-56 127`. `checked_mul(y, 2)` is `.none`, and `checked_sub(y, 1)` is `.some(99)`.

A literal operand takes the type of the other, so the `1` above is a `u8`, as [Literals](literals.md) says. Two operands of different types are refused, as [Number types](number-types.md) says.

## The forms

| Written | When the result does not fit |
| --- | --- |
| `a + b`, `a - b`, `a * b` | Does not compile |
| `wrapping_add(a, b)`, `wrapping_sub(a, b)`, `wrapping_mul(a, b)` | Keeps the low bits |
| `saturating_add(a, b)`, `saturating_sub(a, b)`, `saturating_mul(a, b)` | Gives the nearest value the type holds |
| `checked_add(a, b)`, `checked_sub(a, b)`, `checked_mul(a, b)` | Gives `.none`, and `.some(r)` otherwise |

Next: [Bit operations](bits.md)
