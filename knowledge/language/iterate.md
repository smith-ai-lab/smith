---
type: specification
title: Iterate
description: A type becomes loopable by implementing Iterate, whose one function each walks the value and calls a lambda on every element until the lambda says stop, and which loop v in x calls.
status: partial
built:
  - "`Flow`, `Iterate`, and the `Try` of `Flow` in the prelude, with an `impl` of `Iterate` for a `List`, which `loop` runs over a `List` and over a `str` as its bytes with, and an `impl` of a type of a program's own, which `loop v in x` runs over as well, whose elements are of the type that `Iterate` gives for it"
  - "`break` and `return` in a `loop` over a value: a `break` answers `.stop`, and a `return` keeps its value, answers `.stop`, and gives the function the value once `each` is over, from any `if`, `match`, or loop in the block"
  - "`each(x) fn(v) { ... }` as a call of a function that takes a function, whose lambda gives a `Flow`"
  - "a `loop` whose block cannot answer `.stop` builds no `Flow` and tests none, and a `loop` over a type with no implementation of `Iterate` is one error that names the type"
  - "not `try` inside a `loop`, so the `Countdown` of the page does not compile yet, and its `each` is written with a `match` on what `body` gives and a `return .stop` until it does"
  - "not an `each` that calls itself, as the `each` of a `Tree` does, since a function that calls itself is not built"
---

# Iterate

A type becomes loopable by implementing `Iterate`, a trait with one function, `each`, which walks a value and calls a lambda on every element, until the lambda says to stop.

```
enum Flow {
  go
  stop
}

trait Iterate[C, out T] {
  fn each(c: C, body: (T) -> Flow) -> Flow
}
```

`C` is the type that is walked, `T` is the type of its elements, and `body` is called once for each element. `body` answers with a `Flow`: `.go` to be given the next element, and `.stop` to be given no more. `each` gives a `Flow` too: `.stop` when `body` stopped it, and `.go` when it walked to the end. `T` is marked `out`, so it is determined by `C`, as [`out` parameters](out-parameters.md) says: once the type that is walked is known, so is the type of its elements.

## Making a type loopable

A type implements `Iterate` by writing `each` for it. `Countdown` holds a number, and its `each` walks the numbers from that one down to `1`.

```
struct Countdown {
  from: i32
}

impl Iterate[Countdown, i32] {
  fn each(c: Countdown, body: (i32) -> Flow) -> Flow {
    loop i in 0..c.from { try body(c.from - i) }
    .go
  }
}
```

`Flow` works with [`try`](try.md): `try body(v)` goes on to the next line when `body` gave `.go`, and leaves `each` with `.stop` when it gave `.stop`. So an `each` passes a stop along by writing `try` before each call of `body`, and ends with `.go`. The prelude gives `Flow` its `Try`:

```
impl Try[Flow, Flow, ()] {
  fn then(c: Flow, body: (()) -> Flow) -> Flow {
    match c {
      .go { body(()) }
      .stop { .stop }
    }
  }
}
```

`each` is a function like any other, so it can be called with a lambda that gives a `Flow`, as a function that takes a function is in [Lambdas](lambdas.md). This prints `3` and `2`, and stops there.

```
fn main() ! console, alloc {
  each(Countdown(from: 3)) fn(v) {
    print("{v}")
    if v == 2 { .stop } else { .go }
  }
}
```

## Looping over it

`loop v in x { ... }` calls `each(x)` with the block as its lambda, and the block answers `.go` when it reaches its end. A type that implements `Iterate` is looped over like a range, with the name the element.

```
fn largest(c: Countdown) -> i32 {
  var top = 0
  loop v in c { if v > top { top = v } }
  top
}
```

`largest(Countdown(from: 3))` is `3`. `loop v in x { ... }` is the form that is written to run a block for each element. A call of `each` with a lambda is what it means, and it is accepted because `each` is an ordinary function.

## What the body may do

The body is a lambda that cannot escape, as [Lambdas](lambdas.md) says of every lambda. So it is inlined, and a `loop` over a user type costs nothing extra.

Its effects are checked where it is written, in the function that has the loop. A body that prints needs `console` in that function.

```
fn show(c: Countdown) ! alloc {
  loop v in c { print("{v}") }
}
```

This is refused, because `show` does not list `console`, which the `print` in the body needs.

## A walk over a tree

A walk over a tree passes `body` along to its recursive calls. The `each` of a tree calls itself on the left part, then on the value, then on the right part, and each call is handed the same `body`. A `try` before each step but the last passes a stop up through every level, so the walk ends the moment one value says stop.

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
  fn each(t: Tree, body: (i32) -> Flow) -> Flow {
    match t {
      .leaf { .go }
      .node(b) {
        try each(b.left, body)
        try body(b.value)
        each(b.right, body)
      }
    }
  }
}
```

A `loop v in t` over a `Tree` runs its block once for each value, left to right. Nothing unwinds when it stops: each level of `each` returns `.stop` to the one that called it.

## `break` and `return`

A loop over a type that implements `Iterate` is ended by `break`, and left by [`return`](return.md), as a loop over a range is.

```
fn first_small(c: Countdown) -> i32 {
  var found = 0
  loop v in c {
    if v < 3 {
      found = v
      break
    }
  }
  found
}
```

`first_small(Countdown(from: 5))` is `2`: the block runs for `5`, `4`, `3`, and `2`, and the `break` at `2` ends the loop. In the lambda the loop gives `each`, a `break` is the answer `.stop`.

```
fn has_zero(t: Tree) -> bool {
  loop v in t {
    if v == 0 { return true }
  }
  false
}
```

A `return` in the block keeps its value, answers `.stop`, and when `each` has returned, the function gives the value. So `has_zero` gives `true` at the first `0` and walks no further.

A loop whose block has no `break` and no `return` always answers `.go`, which the compiler knows, so it costs what a loop that cannot be stopped would. A loop that can stop costs one test for each element.

## The forms

| Written | Is |
| --- | --- |
| `impl Iterate[C, T] { fn each(c: C, body: (T) -> Flow) -> Flow { ... } }` | Makes `C` loopable, with elements of type `T` |
| `try body(v)` in an `each` | Calls the block, and leaves `each` with `.stop` when the block said stop |
| `loop v in x { e }` | The form that is written: the block run for each element of `x`, by a call of `each` |
| `each(x) fn(v) { ... .go }` | A call of `each` with a lambda that gives a `Flow`, accepted because `each` is an ordinary function |
| `break`, `return` in the block | End the loop, and leave the function, as in any `loop` |

Next: [What arith proves](arith.md)
