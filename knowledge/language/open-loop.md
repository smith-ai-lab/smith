---
type: specification
title: The open-ended loop
description: An open-ended loop runs its block again and again until break, carries the diverge effect, and with decreases is proven to end.
status: built
---

# The open-ended loop

An open-ended `loop` has no range. It runs its block again and again until a `break` ends it.

```
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

## Why the function says `diverge`

An open-ended loop may not end, as `takes(10, 0)` does not, since `left` is never below `0`. So the function that has one lists the `diverge` effect, and so does every function that calls it. Without it, `takes` is refused: "an open-ended `loop` needs the `diverge` effect, which `takes` does not declare", with the fix "write `fn takes(from: u64, step: u64) -> u64 ! diverge`".

A caller is refused in the same way.

```
fn main() ! console, alloc {
  print("{takes(10, 3)}")
}
```

This is refused: "`takes` needs the `diverge` effect, which `main` does not declare", with the fix "write `fn main() ! console, alloc, diverge`". [The effects Smith provides](provided-effects.md#built-in) lists `diverge`, and [Effects](effects.md) says how a signature lists them.

## A loop that is proven to end

A loop written `loop decreases e { ... }` is an open-ended loop that is proven to end, and its function lists no `diverge`. The expression `e`, the measure, is a whole number of an unsigned type. Each time the loop goes round again, the measure must be smaller than it was when that round began, and [`arith`](contracts.md) must prove it. An unsigned number cannot go below zero, so the loop cannot go on forever. The measure is never computed when the program runs.

```
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

A measure of a signed type is refused, since a signed number can go below zero. With `left: i64`, `loop decreases left` gives "`decreases` is a whole number of an unsigned type, and this is a `i64`".

How `decreases` proves a function that calls itself to end is on [Contracts and proofs](contracts.md).

## The forms

| Written | Is |
| --- | --- |
| `loop { e }` | Runs the block until a `break`; its function and every caller list `diverge` |
| `loop decreases e { e2 }` | The same, with the unsigned measure `e` proven smaller each time round again; no `diverge` |
| `break` | Ends the nearest `loop` |

Next: [Iterate](iterate.md)
