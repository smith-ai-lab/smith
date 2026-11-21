---
type: specification
title: Contracts
description: requires and ensures on a function, which state what the caller owes and what the function gives, and are proven when the program is built.
status: partial
built:
  - "`requires` and `ensures` on a function, each a comparison of whole numbers joined with `and`, `or`, and `not` over its parameters and the result's name, which `arith` proves of the arguments at every call and of what the function returns at each place it returns, and takes as facts in the other place"
  - "what `arith` knows of a `var` of a whole number in a function, which a contract and the code around it rely on alike: at each use, the condition of each `if` around it and what it was last given, where no assignment to it lies between that and the use on any path, outside a loop as inside one"
  - "`decreases` on a function, the measure that proves a function that calls itself ends, which is proven as [Termination](termination.md) says and never computed"
  - "a function that returns `bool`, carries no effect, and is one expression of comparisons joined with `and`, `or`, and `not`, or a call of another such function, as a contract: `requires f(args)` and `ensures f(args)` are proven and give facts as that expression with the arguments put in does, and a call that carries an effect is refused"
  - "a `requires` that calls a function that returns `bool` and carries no effect but `alloc`, with arguments written out whole (numbers, texts, lists and tuples of them, `true`, `false`) in the call or in a parameter of the function that was, which the kernel works out when the program is built: a call whose `requires` works out to `true` is proven, one that works out to `false` is refused with the call as written, and the comparison that gave `false` with where it is written and the numbers it compared, and one that takes more than 1000000 steps is refused naming the call and the limit; a function that only a `requires` or an `ensures` names is checked"
  - "not an argument that is a name, a call, or an operation, for the kernel to work out, and not a float"
---

# Contracts

A signature can state what must hold. `requires` is owed by the caller, and `ensures` is owed by the function. Both are checked when the program is built, with [`arith`](arith.md).

```
fn gap(a: Byte, b: Byte) -> Byte
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
use std.console (console, print)

fn main() ! console, alloc { print("{gap(3, 5)}") }
```

This is refused: "cannot prove that `3 >= 5`, which `gap` requires of its arguments", with the fix "check the arguments first with an `if`, or state it in a `requires`".

The caller proves it from what it knows. A condition it has checked is enough.

```
fn gap_or_zero(a: Byte, b: Byte) -> Byte {
  if a >= b { gap(a, b) } else { 0 }
}
```

`gap_or_zero(3, 5)` is `0`. Or it states the same `requires`, and passes the duty on to its own caller.

```
fn gap_again(a: Byte, b: Byte) -> Byte
  requires a >= b and a <= 100
{
  gap(a, b)
}
```

`gap_again(9, 3)` is `6`. A `requires` may join comparisons with `and`, `or`, and `not`.

## What the function gives

`ensures` states something about the result. The signature gives the result a name, and the `ensures` is about that name.

```
fn clamp(x: Byte, lo: Byte, hi: Byte) -> r: Byte
  requires lo <= hi
  ensures lo <= r and r <= hi
{
  if x < lo { lo } else if x > hi { hi } else { x }
}
```

`clamp(9, 2, 5)` is `5`. The function proves its `ensures` at each place it returns. One that does not is refused.

```
fn clamp(x: Byte, lo: Byte, hi: Byte) -> r: Byte
  requires lo <= hi
  ensures lo <= r and r <= hi
{
  x
}
```

This is refused: "cannot prove that `lo <= r and r <= hi`, which `clamp` ensures, where `r` is `x`", with the fix "state what is needed in a `requires`, or check it first with an `if`".

A signature that has an `ensures` names its result.

```
fn bigger(a: Byte) -> Byte
  ensures a + 1 <= 255
{
  a
}
```

This is refused: "`bigger` has an `ensures`, and its signature does not name its result".

## What the caller then knows

The caller takes the `ensures` as a fact about what the call gave. So a result that is known to be within bounds can be added to.

```
fn room(x: Byte) -> Byte {
  c = clamp(x, 0, 100)
  c + 100
}
```

`room(250)` is `200`. `clamp(x, 0, 100)` gave a `c` that is at most 100, so `c + 100` fits. With `clamp(x, 0, 200)` it is refused, as `c + 100` may be above 255.

## A requirement that is worked out

A predicate may be longer than `arith` reads, with a `loop` or a `match`. When every argument of the call is written out, the compiler works the predicate out when the program is built, one step at a time, and the call is proven if it gives `true`.

```
use std.bits (i32)
use std.console (console, print)

fn has(xs: List[i32], k: i32) -> bool {
  var found = false
  loop v in xs {
    if v == k {
      found = true
      break
    }
  }
  found
}

fn pick(xs: List[i32], k: i32) -> i32
  requires has(xs, k)
{
  k
}

fn main() ! console, alloc { print("{pick([1, 2, 3], 2)}") }
```

`pick([1, 2, 3], 2)` is `2`. With `5` in place of `2` it is refused: "`has([1, 2, 3], 5)` is false, and the last comparison it made was `3 == 5` is false". The message says where the answer came from: the comparison that gave `false` and the numbers it compared. A predicate that does not end within 1000000 steps is refused with the call and the limit named, and is never taken as proven. An argument that is not written out leaves the call to a `check`, as any `requires` that is not proven.

## A function used as a value

A function type states no contract, so a function with a `requires` is only called by its name, and using its name as a value is an error.

```
use std.console (console, print)

fn apply(f: fn(Byte, Byte) -> Byte) -> Byte { f(5, 3) }

fn main() ! console, alloc { print("{apply(gap)}") }
```

This is refused: "`gap` has a `requires`, which a function type does not state, so it is only called by its name".

A function with only an `ensures` can be a value, and what it gives is then not known to the caller.

```
fn small(x: Byte) -> r: Byte
  ensures r < 10
{
  if x < 10 { x } else { 9 }
}

fn apply(f: fn(Byte) -> Byte) -> Byte { f(5) }
```

`apply(small)` is `5`. Inside `apply`, `f(5)` is any `u8`, since `f` is a function type and states no `ensures`.

## The forms

| Written | Who owes it | About |
| --- | --- | --- |
| `requires p` | The caller, at each call | The arguments |
| `ensures p` | The function, where it returns | The result, by the name the signature gives it |
| `fn f(a: T) -> r: U` | | Names the result `r`, for an `ensures` |

Next: [Termination](termination.md)
