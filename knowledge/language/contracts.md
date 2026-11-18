---
type: specification
title: Contracts
description: requires and ensures on a function, which state what the caller owes and what the function gives, and are proven when the program is built.
status: partial
built:
  - "`requires` and `ensures` on a function, each a comparison of whole numbers joined with `and`, `or`, and `not` over its parameters and the result's name, which `arith` proves of the arguments at every call and of what the function returns at each place it returns, and takes as facts in the other place"
  - "not a predicate in a contract, and not a float"
---

# Contracts

A signature can state what must hold. `requires` is owed by the caller, and `ensures` is owed by the function. Both are checked when the program is built, with [`arith`](arith.md).

```
fn gap(a: u8, b: u8) -> u8
  requires a >= b
{
  a - b
}
```

`gap` takes two numbers and says that the first is not below the second. `gap(5, 3)` is `2`. The `requires` is also what proves `a - b` fits, since inside `gap` it is a fact.

The compiler only checks a contract. It never searches for a proof, and nothing is ever tested when the program runs in a proof's place. Most proofs are never written out: `arith` builds them, and where it cannot, the author supplies the proof, as [What arith proves](arith.md) says.

## What the caller owes

Every call proves the `requires` of the function it calls, of the arguments it passes. A call that cannot is refused where it is called.

```
fn main() ! console, alloc { print("{gap(3, 5)}") }
```

This is refused: "cannot prove that `3 >= 5`, which `gap` requires of its arguments", with the fix "check the arguments first with an `if`, or state it in a `requires`".

The caller proves it from what it knows. A condition it has checked is enough.

```
fn gap_or_zero(a: u8, b: u8) -> u8 {
  if a >= b { gap(a, b) } else { 0 }
}
```

`gap_or_zero(3, 5)` is `0`. Or it states the same `requires`, and passes the duty on to its own caller.

```
fn gap_again(a: u8, b: u8) -> u8
  requires a >= b and a <= 100
{
  gap(a, b)
}
```

`gap_again(9, 3)` is `6`. A `requires` may join comparisons with `and`, `or`, and `not`.

## What the function gives

`ensures` states something about the result. The signature gives the result a name, and the `ensures` is about that name.

```
fn clamp(x: u8, lo: u8, hi: u8) -> r: u8
  requires lo <= hi
  ensures lo <= r and r <= hi
{
  if x < lo { lo } else if x > hi { hi } else { x }
}
```

`clamp(9, 2, 5)` is `5`. The function proves its `ensures` at each place it returns. One that does not is refused.

```
fn clamp(x: u8, lo: u8, hi: u8) -> r: u8
  requires lo <= hi
  ensures lo <= r and r <= hi
{
  x
}
```

This is refused: "cannot prove that `lo <= r and r <= hi`, which `clamp` ensures, where `r` is `x`", with the fix "state what is needed in a `requires`, or check it first with an `if`".

A signature that has an `ensures` names its result.

```
fn bigger(a: u8) -> u8
  ensures a + 1 <= 255
{
  a
}
```

This is refused: "`bigger` has an `ensures`, and its signature does not name its result".

## What the caller then knows

The caller takes the `ensures` as a fact about what the call gave. So a result that is known to be within bounds can be added to.

```
fn room(x: u8) -> u8 {
  c = clamp(x, 0, 100)
  c + 100
}
```

`room(250)` is `200`. `clamp(x, 0, 100)` gave a `c` that is at most 100, so `c + 100` fits. With `clamp(x, 0, 200)` it is refused: "cannot prove that `c + 100 <= 255`, so `c + 100` may not fit `u8`".

## A function used as a value

A function type states no contract, so a function with a `requires` is only called by its name, and using its name as a value is an error.

```
fn apply(f: (u8, u8) -> u8) -> u8 { f(5, 3) }

fn main() ! console, alloc { print("{apply(gap)}") }
```

This is refused: "`gap` has a `requires`, which a function type does not state, so it is only called by its name".

A function with only an `ensures` can be a value, and what it gives is then not known to the caller.

```
fn small(x: u8) -> r: u8
  ensures r < 10
{
  if x < 10 { x } else { 9 }
}

fn apply(f: (u8) -> u8) -> u8 { f(5) }
```

`apply(small)` is `5`. Inside `apply`, `f(5)` is any `u8`, since `f` is a function type and states no `ensures`.

## The forms

| Written | Who owes it | About |
| --- | --- | --- |
| `requires p` | The caller, at each call | The arguments |
| `ensures p` | The function, where it returns | The result, by the name the signature gives it |
| `fn f(a: T) -> r: U` | | Names the result `r`, for an `ensures` |

Next: [Termination](termination.md)
