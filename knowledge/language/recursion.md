---
type: specification
title: Recursion
description: A tail call compiles to a loop, any other recursion keeps its frames on a stack that grows in blocks asked of alloc, and so recursion cannot overflow the stack.
status: partial
built:
  - "a function whose every call of itself is a tail call, the last expression of its body or of a block of an `if` or a `match` that is itself in that place, or what a `return` gives, and which has a `decreases` that `arith` proves smaller at each call, is a loop with no frame for each call"
  - "a function that calls itself where the call is not the last thing it does, which carries `alloc` and has a `decreases` that `arith` proves smaller at each call, and keeps its frames on a stack of its own that goes on in a fresh block of at least 64 KB asked of `alloc` when one is full, so no depth of calls overflows the stack"
  - "not functions that call each other, not a call of itself in a lambda, and not recursion under an installed handler of `alloc`"
---

# Recursion

Recursion cannot overflow the stack. A call in tail position is a loop, and every other recursive call keeps its frame on a stack that goes on in a fresh block, asked of `alloc`, when the block it is on is full.

```
fn power(x: f64, n: Nat, acc: f64) -> f64 {
  if n == 0 { acc } else { power(x, n - 1, acc * x) }
}
```

`power` calls itself as the last thing it does, a tail call, so it compiles to a loop with no frame per call. A tail-recursive function uses no stack and may run in a GPU kernel.

## A call that is not a tail call

```
fn power_slow(x: f64, n: Nat) -> f64 ! alloc {
  if n == 0 { 1.0 } else { x * power_slow(x, n - 1) }
}
```

The multiplication happens after the call returns, so this is not a tail call. Each level keeps a frame, on a stack that grows in blocks asked of `alloc` as any memory is, so the depth is limited only by memory. Such a function carries `alloc`. Leaving `alloc` out is refused, as for any function that allocates.

## Code with no recursion

Code with no recursion has a deepest chain of calls that is known at compile time, and the compiler confirms it fits the stack.

```
fn double(x: f64) -> f64 { x * 2.0 }
fn quad(x: f64) -> f64 { double(double(x)) }
```

The chain `quad`, `double` is two calls deep.

## Where recursion may run

Code in a [`with device(gpu)`](devices.md) scope has no recursion other than a tail call, so `power` may run there and `power_slow` may not.

## The forms

| Recursion | Compiled as | Needs |
| --- | --- | --- |
| A tail call | A loop | Nothing |
| Any other | A frame on a stack of blocks asked of `alloc`, for each level | `alloc` |
| None | A chain of calls whose depth is known | Nothing |

Next: [Types that contain themselves](recursive-types.md)
