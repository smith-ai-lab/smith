---
type: specification
title: loop
description: loop runs a block once for each number in a range, in order, and break ends it early.
status: partial
built: "`loop i in a..b` over whole numbers of one type, whose counter `arith` knows is in range of a `var` list's `size` as of any other, which runs in order and not at all when `a >= b`, ended by `break`; not a `loop` over an array, or `reversed`"
---

# `loop`

A `loop` runs a block once for each number in a range, in order, one step at a time.

```
fn sum_below(n: u64) -> u64 {
  var total: u64 = 0
  loop i in 0..n { total = wrapping_add(total, i) }
  total
}
```

`sum_below(10)` is `45`. The block runs with `i` as `0`, then `1`, and so on up to `9`, and `total` is what it is after the last round. The keyword states whether order matters: a [`for`](for.md) has no order, and a `loop` runs in order. There is no `while`: a loop that goes on while something holds is an [open-ended loop](open-loop.md).

## Which numbers

A range `a..b` is a `Range` value, as [Ranges and slices](ranges.md) says. It runs from `a` up to `b` and excludes `b`, so the block of `loop i in 0..3` runs with `0`, `1`, and `2`. A range with no numbers, as `5..2` is, runs the block no times.

```
fn main() ! console, alloc {
  loop i in 5..2 { print("{i}") }
  print("done")
}
```

This prints only `done`. The counter is a name for the number of this round and not a `var`, so the block cannot assign it. `loop i in 0..3 { i = 5 }` is refused: "`i` is not a `var`, so it cannot be assigned to", with the fix "declare it with `var`: write `var i` where it is defined". A `loop` gives nothing, so a function whose block ends in one gives nothing. `fn count() -> u64 { loop i in 0..3 { } }` is refused: "this gives nothing, where a `u64` is needed".

## The type of the counter

The two ends of a range have one type, and the counter has it. With `a: u8` and `b: u32`, `loop i in a..b` is refused: "the two ends of a range need to be of one type, and the first is `u8` where the second is `u32`".

An end that is a literal takes the type of the other end. With `a: u8`, the literal in `loop i in a..300` is a `u8`, and it does not fit: "`300` does not fit `u8`, which holds 0 to 255".

When both ends are literals, how the counter is used in the block decides their type, as for a literal bound to a name in [Literals](literals.md#where-the-type-comes-from). Here `j` is added to a `u8`, so the ends are `u8`.

```
fn main() ! console, alloc {
  loop j in 0..3 {
    x: u8 = 250
    print("{x + j}")
  }
}
```

This prints `250`, `251`, and `252`. When nothing decides, the ends are `i64`: `loop i in 0..300` counts `i` to `299`, a number a `u8` does not hold.

## Ending early with `break`

`break` ends the loop at once, and the code after the loop goes on.

```
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
fn twice(body: () -> ()) { body() }

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
fn next(n: u8) -> u8 {
  var last: u8 = 0
  loop i in 0..n { last = i + 1 }
  last
}
```

`next(4)` is `4`. The counter is below `n`, which is at most `255`, so `i + 1` is at most `255` and fits a `u8`. With `i + 2` it is refused, since `254 + 2` does not fit: "cannot prove that `i + 2 <= 255`, so `i + 2` may not fit `u8`", with the fix "check the operands first, or use `wrapping_add`, `saturating_add`, or `checked_add`".

This is what makes `xs[i]` compile in `loop i in 0..xs.size`, as [Indexing](indexing.md#from-a-loop) shows. The ends are worked out once, so `arith` ties them to nothing that a `var` the loop assigns holds in a later round, or after an assignment in this one. So the `ys[i]` below is refused, since `ys` is assigned before it.

```
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
fn largest(a: [3]u32) -> u32 {
  var best: u32 = 0
  loop v in a { if v > best { best = v } }
  best
}
```

`largest([4, 9, 6])` is `9`. `break` ends a loop over an array as it ends one over a range.

```
fn before_zero(a: [4]u8) -> u64 {
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
