---
type: specification
title: Iterate
description: A type becomes loopable by implementing Iterate, whose one function each walks the value and calls a lambda on every element, and which loop v in x calls.
status: planned
---

# Iterate

A type becomes loopable by implementing `Iterate`, a trait with one function, `each`, which walks a value and calls a lambda on every element.

```
trait Iterate[C, out T] {
  fn each(c: C, body: (T) -> ())
}
```

`C` is the type that is walked, `T` is the type of its elements, and `body` is called once for each element. `T` is marked `out`, so it is determined by `C`, as [Traits](traits.md) says: once the type that is walked is known, so is the type of its elements.

## Making a type loopable

A type implements `Iterate` by writing `each` for it. `Countdown` holds a number, and its `each` walks the numbers from that one down to `1`.

```
struct Countdown {
  from: i32
}

impl Iterate[Countdown, i32] {
  fn each(c: Countdown, body: (i32) -> ()) {
    loop i in 0..c.from { body(c.from - i) }
  }
}
```

`each` is a function like any other, so it is called with a lambda, as a function that takes a function is in [Lambdas](lambdas.md). This prints `3`, `2`, and `1`.

```
fn main() ! console, alloc {
  each(Countdown(from: 3)) fn(v) { print("{v}") }
}
```

## Looping over it

`loop v in x { ... }` means `each(x) fn(v) { ... }`. A type that implements `Iterate` is looped over like a range, with the name the element.

```
fn largest(c: Countdown) -> i32 {
  var top = 0
  loop v in c { if v > top { top = v } }
  top
}
```

`largest(Countdown(from: 3))` is `3`. `loop v in x { ... }` is the form that is written to run a block for each element. `each(x) fn(v) { ... }` is what it means, and it is accepted because `each` is an ordinary function.

## What the body may do

The body is a lambda that cannot escape, as [Lambdas](lambdas.md) says of every lambda. So it is inlined, and a `loop` over a user type costs nothing extra.

Its effects are checked where it is written, in the function that has the loop. A body that prints needs `console` in that function.

```
fn show(c: Countdown) ! alloc {
  loop v in c { print("{v}") }
}
```

This is refused, because `show` does not list `console`, which the `print` in the body needs.

## `with` and `each`

`each` is an ordinary function, so `with v = each(x)` works too. It is written only where the walk is the last thing a function does.

```
fn show(c: Countdown) ! console, alloc {
  with v = each(c)
  print("{v}")
}
```

`show(Countdown(from: 3))` prints `3`, `2`, and `1`, as the loop does.

## A walk over a tree

A walk over a tree passes `body` along to its recursive calls. The `each` of a tree calls itself on the left part, then on the value, then on the right part, and each call is handed the same `body`.

```
enum Tree {
  leaf
  node: Branch
}

struct Branch {
  left: Tree
  value: i32
  right: Tree
}

impl Iterate[Tree, i32] {
  fn each(t: Tree, body: (i32) -> ()) {
    match t {
      .leaf { }
      .node(b) {
        each(b.left, body)
        body(b.value)
        each(b.right, body)
      }
    }
  }
}
```

A `loop v in t` over a `Tree` runs its block once for each value, left to right.

## No `break`

A loop over a user type has no `break`. The body is a lambda, and `each` has no way to be told to stop, so a `break` in it has no loop to end, and it is refused.

```
fn first_big(c: Countdown) -> i32 {
  var found = 0
  loop v in c {
    if v < 3 { break }
    found = v
  }
  found
}
```

This is refused, as a `break` in any lambda is. A loop over a range or an array, and an open-ended loop, have a `break`, as [`loop`](loop.md#ending-early-with-break) and [The open-ended loop](open-loop.md) show.

## The forms

| Written | Is |
| --- | --- |
| `impl Iterate[C, T] { fn each(c: C, body: (T) -> ()) { ... } }` | Makes `C` loopable, with elements of type `T` |
| `loop v in x { e }` | The form that is written: the block run for each element of `x`, which means `each(x) fn(v) { e }` |
| `each(x) fn(v) { e }` | What the loop means, accepted because `each` is an ordinary function |
| `with v = each(x)` | Written only where the walk is the last thing a function does |
| `break` in these | Refused: there is no loop to end |

Next: [What arith proves](arith.md)
