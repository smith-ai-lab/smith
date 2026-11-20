---
type: specification
title: The open-ended loop
description: An open-ended loop runs its block again and again until break, carries the diverge effect, and with decreases is proven to end.
status: partial
built:
  - "an open-ended `loop` ended by `break`, whose function carries `diverge`, and `loop decreases e` with a measure of an unsigned type that `arith` proves smaller at every place the loop goes round again, where the measure must also fit its type when the round begins"
  - "not a measure checked only where the loop goes round again, so not a distance to an end"
---

# The open-ended loop

An open-ended `loop` has no range. It runs its block again and again until a `break` ends it.

```
use std.bits (u64)
use std.int (wrapping_add, wrapping_sub)

fn takes(from: u64, step: u64) -> u64 ! diverge {
  var left: u64 = from
  var times: u64 = 0
  loop {
    if left < step { break }
    left = wrapping_sub(left, step)
    times = wrapping_add(times, 1)
  }
  times
}
```

`takes(10, 3)` is `3`: `step` can be taken from `10` three times, and then `left` is `1`, which is below `step`, so the `break` runs. `break` ends the loop as it ends [a loop over a range](loop.md#ending-early-with-break), and the code after the loop goes on.

There is no `while`. A loop that goes on while something holds is an open-ended loop that breaks when it does not, as `takes` does with `if left < step { break }`.

A loop that goes on until something is found, and not once for each of a known number of things, is an open-ended loop with a measure. A loop over a range that is left by `break` before its end, with a counter nothing reads, is this loop written the long way.

## Why the function says `diverge`

An open-ended loop may not end, as `takes(10, 0)` does not, since `left` is never below `0`. So the function that has one lists the `diverge` effect, and so does every function that calls it. Without it, `takes` is refused: "an open-ended `loop` needs the `diverge` effect, which `takes` does not declare", with the fix "write `fn takes(from: u64, step: u64) -> u64 ! diverge`".

A caller is refused in the same way.

```
use std.console (console, print)

fn main() ! console, alloc {
  print("{takes(10, 3)}")
}
```

This is refused: "`takes` needs the `diverge` effect, which `main` does not declare", with the fix "write `fn main() ! console, alloc, diverge`". [The effects Smith provides](provided-effects.md#built-in) lists `diverge`, and [Effects](effects.md) says how a signature lists them.

## A loop that is proven to end

A loop written `loop decreases e { ... }` is an open-ended loop that is proven to end, and its function lists no `diverge`. The expression `e`, the measure, is made of whole numbers. At each place the loop goes round again, [`arith`](arith.md) must prove two things of it: that it is not below zero, and that it is smaller than it was when that round began. A number that gets smaller each round and never goes below zero cannot do so forever, so the loop ends. The measure is never computed when the program runs, so it is reasoned about as an exact number, with no type it must fit, as the size of an array is.

```
use std.bits (u64)
use std.int (wrapping_add)

fn count_down(n: u64) -> u64 {
  var left = n
  var rounds: u64 = 0
  loop decreases left {
    if left == 0 { break }
    left = left - 1
    rounds = wrapping_add(rounds, 1)
  }
  rounds
}
```

`count_down(5)` is `5`. A round that goes on has passed `left == 0`, so `left` is at least `1` and the subtraction fits, and it ends with `left` one smaller. Nothing here says `diverge`, and a `main` that prints `count_down(5)` needs only `console` and `alloc`.

A loop whose measure does not get smaller is refused. Here nothing changes `left`.

```
use std.bits (u64)
use std.int (wrapping_add)

fn count_down(n: u64) -> u64 {
  var left = n
  var rounds: u64 = 0
  loop decreases left {
    if left == 0 { break }
    rounds = wrapping_add(rounds, 1)
  }
  rounds
}
```

This is refused: "cannot prove that `left` is smaller than when the round began, so this `loop` may not end", with the fix "make each round that goes on make the measure smaller, or check what it needs first with an `if`".

## A distance to an end

The measure is checked only where the loop goes round again, and nowhere before. So a loop that moves a place toward an end is proven to end by the distance that is left, though nothing says the place is inside when the round begins.

```
use std.bits (u64)
use std.int (wrapping_add)

fn count_to(size: u64) -> u64 {
  var at: u64 = 0
  var n: u64 = 0
  loop decreases size - at {
    if at >= size { break }
    at = at + 1
    n = wrapping_add(n, 1)
  }
  n
}
```

`count_to(3)` is `3`. A round that goes on has passed `at >= size`, so `at` was below `size` when it began. It ends with `at` one more, so `size - at` is not below zero and is one smaller than it was.

A function that moves the place says how far in its contract, and the loop that calls it uses that. Here `skip` promises a place after the one it was given and not past the end, which is what the loop needs.

```
fn skip(src: List[Byte], at: u64) -> next: u64
  requires at < src.size
  ensures next > at and next <= src.size
{
  at + 1
}

fn steps(src: List[Byte]) -> u64 {
  var at: u64 = 0
  var n: u64 = 0
  loop decreases src.size - at {
    if at >= src.size { break }
    at = skip(src, at)
    n = wrapping_add(n, 1)
  }
  n
}
```

A measure that is not proven to stay at zero or above is refused as one that does not get smaller is. With `loop decreases left` where `left` is an `i64` that the round makes one smaller and nothing keeps from going below zero, the loop is refused.

How `decreases` proves a function that calls itself to end is on [Termination](termination.md#proving-that-a-function-ends).

## The forms

| Written | Is |
| --- | --- |
| `loop { e }` | Runs the block until a `break`; its function and every caller list `diverge` |
| `loop decreases e { e2 }` | The same, with the measure `e` proven not below zero and smaller each time round again; no `diverge` |
| `break` | Ends the nearest `loop` |

Next: [Iterate](iterate.md)
