---
type: specification
title: What a spawned task may use
description: How a spawned lambda uses the values around it: an ordinary value is shared, a var is read as it was at the spawn, and a single-use value moves into the child.
status: planned
---

# What a spawned task may use

A spawned lambda runs at the same time as its parent, so three rules say what it may use of the function around it.

## An ordinary value is shared

An ordinary value is immutable, and the child finishes before the group's block ends, so nothing is copied. Both children here read `url`.

```
fn fetch_twice(url: str) -> Result[(List[Byte], List[Byte]), NetError] ! net, alloc {
  with tasks()
  ta = spawn { fetch(url) }
  tb = spawn { fetch(url) }
  pa = try join(ta)
  pb = try join(tb)
  .ok((pa, pb))
}
```

## A var is read as it was

A [`var`](var.md) is read as its value at the moment of the spawn, and the child cannot assign it. Results come back through `join`.

```
use std.bits (i32)

fn snapshot() -> i32 {
  var n = 1
  with tasks()
  t = spawn { n + 1 }
  n = 5
  n + join(t)
}
```

The child read `n` as `1`, so `join(t)` is `2` and `snapshot()` is `7`.

A child that assigns a `var` is refused. The compiler does not build this refusal, so no words are quoted.

```
use std.bits (i32)

fn counted() -> i32 {
  var n = 0
  with tasks()
  t = spawn { n = n + 1 }
  join(t)
  n
}
```

## A single-use value moves

A single-use value moves into the child, and the parent cannot use it afterwards. Here `conn` belongs to the child after the spawn.

```
fn serve(listener: Listener) ! net, alloc, diverge {
  with tasks()
  loop {
    conn = accept(listener)
    spawn { handle(conn) }
  }
}
```

A `Key` is single-use, as [Lending and take](take.md) says. The parent that spawns with `k1` has none left, so using it again is refused. The compiler does not build this refusal, so no words are quoted.

```
fn two_draws(key: take Key) -> f32 {
  (k1, k2) = split(key)
  with tasks()
  ta = spawn { uniform(k1) }
  tb = spawn { uniform(k1) }
  join(ta) + join(tb)
}
```

Written with `k1` and `k2`, one for each child, it is accepted.

## The forms

| What the child uses | What it gets |
| --- | --- |
| An ordinary value | The value, shared |
| A `var` | Its value at the spawn, never assigned |
| A single-use value | The value, which the parent no longer has |

Next: [Memory of a task](task-arenas.md)
