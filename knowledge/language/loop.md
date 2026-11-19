---
type: specification
title: loop
description: loop runs a block once for each number in a range, in order, and break ends it early.
status: partial
built:
  - "`loop i in a..b` over whole numbers of one type, whose counter `arith` knows is in range of a `var` list's `size` as of any other, which runs in order and not at all when `a >= b`, ended by `break`"
  - "`loop v in xs` over a `List` of any type, which gives `v` each element from the first to the last, once each, over the list the loop began with, which a `var` list assigned in the block does not change, and ended by `break` or left by `return`; the scalar form of its round is one comparison and one load, with no call and no check of the index"
  - "`loop v in x` over a `str`, which gives `v` each byte of it, and over a type of a program's own that implements [`Iterate`](iterate.md)"
  - "not a `loop` over an array or `reversed`, a counter whose type is a range, or the two ends of a range of different types"
---

# `loop`

A `loop` runs a block once for each number in a range, in order, one step at a time.

```
use std.int (wrapping_add)

fn sum_below(n: Nat) -> Nat {
  var total: Nat = 0
  loop i in 0..n {
    step: Nat = i
    total = wrapping_add(total, step)
  }
  total
}
```

`sum_below(10)` is `45`. The block runs with `i` as `0`, then `1`, and so on up to `9`, and `total` is what it is after the last round. The keyword states whether order matters: a [`for`](for.md) has no order, and a `loop` runs in order. There is no `while`: a loop that goes on while something holds is an [open-ended loop](open-loop.md).

## Which numbers

A range `a..b` is a `Range` value, as [Ranges and slices](ranges.md) says. It runs from `a` up to `b` and excludes `b`, so the block of `loop i in 0..3` runs with `0`, `1`, and `2`. A range with no numbers, as `5..2` is, runs the block no times.

```
use std.console (console, print)

fn main() ! console, alloc {
  loop i in 5..2 { print("{i}") }
  print("done")
}
```

This prints only `done`. The counter is a name for the number of this round and not a `var`, so the block cannot assign it. `loop i in 0..3 { i = 5 }` is refused: "`i` is not a `var`, so it cannot be assigned to", with the fix "declare it with `var`: write `var i` where it is defined". A `loop` gives nothing, so a function whose block ends in one gives nothing. `fn count() -> u64 { loop i in 0..3 { } }` is refused: "this gives nothing, where a `u64` is needed".

## The type of the counter

The counter is a range: from the least value the first end can have to the greatest value the second end can have, which it stays below. With `n: 0..11`, the counter of `loop i in 0..n` is a `0..10`, as `n` is at most 10 and the counter is below it.

```
use std.console (console, print)

fn main() ! console, alloc {
  loop j in 0..3 {
    x: Byte = 250
    print("{x + j}")
  }
}
```

This prints `250`, `251`, and `252`. Here `j` is a `0..3`, so `x + j` is a `0..258`, as [Integer arithmetic](arithmetic.md) says. The two ends may be of any ranges, and an end that is a literal is the range that holds it alone, so `loop i in 0..300` counts `i` to `299`, a number a `u8` does not hold.

### A `var` the loop changes

A `var` with no stated type has the range of what it is first given, as [`var`](var.md#the-range-of-a-var) says, so a `var` that a loop assigns states its range.

```
fn last_index(n: 0..11) -> 0..10 {
  var last = 0
  loop i in 0..n { last = i }
  last
}
```

This is refused, as `last` is a `0..1`, the range of the `0` it was first given, and `i` is outside it. The fix is to state the range.

```
fn last_index(n: 0..11) -> 0..10 {
  var last: 0..10 = 0
  loop i in 0..n { last = i }
  last
}
```

`last_index(4)` is `3`. Each assignment in the loop is proven to stay in the range of the `var`.

## Ending early with `break`

`break` ends the loop at once, and the code after the loop goes on. [`return`](return.md) ends the loop and the function with it.

```
use std.console (console, print)

fn main() ! console, alloc {
  loop i in 0..10 {
    if i == 3 { break }
    print("{i}")
  }
}
```

This prints `0`, `1`, and `2`. When `i` is `3` the loop ends, and the rounds for `4` to `9` never run.

`break` ends the nearest loop around it and no other. In this one it ends the inner loop each time, so the outer loop still runs three rounds.

```
use std.console (console, print)

fn main() ! console, alloc {
  loop i in 0..3 {
    loop j in 0..3 {
      if j == 1 { break }
      print("{i} {j}")
    }
  }
}
```

This prints `0 0`, `1 0`, and `2 0`.

A `break` that is in no loop has nothing to end: "`break` is outside a `loop`, so there is nothing for it to end". A `break` inside a [lambda](lambdas.md) does not reach a loop outside the lambda, and it is refused.

```
use std.console (console)

fn twice(body: fn()) { body() }

fn main() ! console, alloc {
  loop i in 0..3 {
    twice { break }
  }
}
```

This is refused: "`break` is inside a lambda, which cannot end a `loop` outside it".

## The ends are worked out once

The two ends of a range are worked out when the loop starts. Assigning a `var` that an end was read from changes nothing about how many rounds there are.

```
use std.bits (u64)
use std.console (console, print)
use std.int (wrapping_add)

fn main() ! console, alloc {
  var n: u64 = 3
  loop i in 0..n {
    n = wrapping_add(n, 1)
    print("{i}")
  }
  print("n is {n}")
}
```

This prints `0`, `1`, and `2`, and then `n is 6`: the loop ran three rounds, though `n` grew in each.

## What the compiler knows inside a loop

Inside the block, [`arith`](arith.md) knows that the counter is at least the first end and below the second, as they were when the loop started. So an operation on the counter that fits only because of that compiles.

```
fn next(n: Byte) -> Byte {
  var last: Byte = 0
  loop i in 0..n { last = i + 1 }
  last
}
```

`next(4)` is `4`. The counter is below `n`, which is at most `255`, so `i` is a `0..255`, `i + 1` is at most `255`, and a `u8` holds it. With `i + 2` it is refused, as `i` may be `254` and `254 + 2` is above `255`, with the fix to check the operands first.

This is what makes `xs[i]` compile in `loop i in 0..xs.size`, as [Indexing](indexing.md#from-a-loop) shows. The ends are worked out once, so `arith` ties them to nothing that a `var` the loop assigns holds in a later round, or after an assignment in this one. So the `ys[i]` below is refused, since `ys` is assigned before it.

```
use std.bits (u32)

fn last_of(xs: List[u32]) -> u32 {
  var ys = xs
  var t: u32 = 0
  loop i in 0..ys.size {
    ys = []
    t = ys[i]
  }
  t
}
```

This is refused: "cannot prove that `i < ys.size`, which indexing `ys` needs", with the fix "put `i` in the range of a `loop` over `0..ys.size`, check `i < ys.size` with an `if`, or state it in a `requires`". It is refused the same way when `ys = []` comes after the line `t = ys[i]`, because the next round reads a `ys` that `arith` knows nothing of.

## A loop over an array

A `loop` runs over an array as well as over a range. It runs once for each element in order, and the name is the element.

```
use std.bits (u32)

fn largest(a: [3]u32) -> u32 {
  var best: u32 = 0
  loop v in a { if v > best { best = v } }
  best
}
```

`largest([4, 9, 6])` is `9`. `break` ends a loop over an array as it ends one over a range.

```
use std.bits (u64)
use std.int (wrapping_add)

fn before_zero(a: [4]Byte) -> u64 {
  var n: u64 = 0
  loop v in a {
    if v == 0 { break }
    n = wrapping_add(n, 1)
  }
  n
}
```

`before_zero([5, 7, 0, 3])` is `2`.

## Going backward

A range goes up, and `loop i in 5..0` is a range with no numbers, so it runs no times. Going backward is written with a reversed range.

```
use std.console (console, print)

fn main() ! console, alloc {
  loop i in reversed(0..3) { print("{i}") }
}
```

This prints `2`, `1`, and `0`.

## The forms

| Written | Is |
| --- | --- |
| `loop i in a..b { e }` | The block with `i` as each number from `a` up to but not including `b`, in order |
| `loop i in a..b` with `a >= b` | No rounds |
| `loop i in reversed(a..b) { e }` | The same numbers, from the last to the first |
| `loop v in x { e }` | The block with `v` as each element of the array `x`, in order |
| `break` | Ends the nearest `loop` |

Next: [The open-ended loop](open-loop.md)
