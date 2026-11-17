---
type: specification
title: loop and iteration
description: loop for steps that run in order, the Iterate trait, and break.
status: partial
built: "`loop i in a..b` over whole numbers of one type, whose counter `arith` knows is in range of a `var` list's `size` as of any other, which runs in order and not at all when `a >= b`, and an open-ended `loop`, each ended by `break`, with or without `decreases` and a measure of an unsigned type, whose function carries `diverge` only without it; not a `loop` over an array or an `Iterate` type, or `reversed`"
---

# `loop` and iteration

The keyword states whether order matters: a `for` has no order, and a `loop` runs in order, one step at a time. A `loop` runs over a range, an array, or any type that implements the `Iterate` trait below. An open-ended `loop { ... }` runs until `break`, and its function carries the `diverge` effect. A `loop decreases e { ... }` is an open-ended loop that is proven to end, and carries none, as [Contracts and proofs](contracts.md) describes. There is no `while`.

```
fn smooth[n: Nat](x: [n]f32) -> f32 {
  var acc = 0.0
  loop i in 0..n { acc = acc * 0.9 + x[i] }
  acc
}
```

A type becomes loopable by implementing `Iterate`. Its one function, `each`, walks the value and calls `body` on every element. `loop v in x { ... }` means `each(x) fn(v) { ... }`.

```
trait Iterate[C, out T] {
  fn each(c: C, body: (T) -> ())
}

struct Countdown {
  from: i32
}

impl Iterate[Countdown, i32] {
  fn each(c: Countdown, body: (i32) -> ()) {
    loop i in 0..c.from { body(c.from - i) }
  }
}

fn largest(c: Countdown) -> i32 {
  var top = 0
  loop v in c { top = max(top, v) }
  top
}
```

- The body is a lambda that cannot escape, so it is inlined and a `loop` over a user type costs nothing extra. Its effects are checked where it is written.
- `each` is an ordinary function, so `with v = each(x)` also works when the walk is the last thing a function does.
- A walk over a tree passes `body` along to its recursive calls.
- `break` is allowed in a loop over a range or an array and in an open-ended loop. It is not allowed in a loop over a user type, because the body has no way to tell `each` to stop.
- The two ends of a range are worked out once, when the loop starts. Inside the loop, `arith` knows that the counter is at least the first end and below the second, as they were then, and ties them to nothing a `var` that the loop assigns holds in a later round, or after an assignment in this one. So `loop i in 0..xs.size` does not make `xs[i]` compile in a block that assigns `xs`.
- The two ends of a range have one type. An end that is a literal takes the type of the other. When both are literals, how the loop's name is used in its block decides their type, as for a literal bound to a name in [Numbers](numbers.md), and they are `i64` when nothing decides.
- A range `a..b` is a `Range` value, and going backward is a reversed range, `loop i in reversed(0..n)`.

Next: [Contracts and proofs](contracts.md)
