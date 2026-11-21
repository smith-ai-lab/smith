---
type: specification
title: Tasks
description: A task group opened with tasks(), children started with spawn and waited for with join, and the spawn effect.
status: planned
---

# Tasks

Concurrent work runs in tasks, and there are no raw threads or locks. Data is immutable or uniquely owned, so data races cannot be written. `with tasks()` opens a task group. `spawn` takes the child's work as a [lambda](lambdas.md) and starts it in the nearest group, and `join` waits for the child and gives its value.

`fetch` is shown by its signature alone. A call such as `fetch(a)` carries the `net` effect.

```
fn fetch(url: str) -> Result[List[Byte], NetError] ! net, alloc

fn fetch_one(url: str) -> Result[List[Byte], NetError] ! net, alloc {
  with tasks()
  t = spawn { fetch(url) }
  join(t)
}
```

`t` is the child, and `join(t)` is what `fetch(url)` gave.

## Two children

A child runs at the same time as its parent, so two spawns run two children together. Each is joined for its value.

```
fn fetch_both(a: str, b: str) -> Result[(List[Byte], List[Byte]), NetError] ! net, alloc {
  with tasks()
  ta = spawn { fetch(a) }
  tb = spawn { fetch(b) }
  pa = try join(ta)
  pb = try join(tb)
  .ok((pa, pb))
}
```

`join` gives what the child gave, here a `Result`, so [`try`](try.md) takes it.

## The first of several

`first` waits for the first of several children of the same group to finish, and gives its value. It takes one child and the list of the others, so that it is never asked to wait for none, and the children give one type.

```
fn quickest(a: str, b: str, c: str) -> Result[List[Byte], NetError] ! net, alloc {
  with tasks()
  ta = spawn { fetch(a) }
  tb = spawn { fetch(b) }
  tc = spawn { fetch(c) }
  first(ta, [tb, tc])
}
```

`quickest` gives the page of whichever server answers first. When the children do different work, the work gives one enum, and the enum says which child finished.

- A child that `first` waits for belongs to the nearest group, as for `join`, and one of another group is refused.
- The child that finished counts as joined, so `join` of it, or `first` with it again, is refused. The others are not waited for: they keep running until they are joined, or until the block ends and cancels them.
- When two children have finished together, the one that comes first, `a` and then `rest` in order, is the one `first` gives, so the result is the same every run.

## No child outlives the block

When the block of `with tasks()` ends, the children still running are cancelled, and the block waits until they have ended. A child whose work must finish is joined, as above. This function does not join its child, so the child is cancelled when the block ends, and the function gives nothing.

```
fn warm(url: str) ! net, alloc {
  with tasks()
  spawn { fetch(url) }
  ()
}
```

[Cancellation](cancellation.md) says what cancelling does.

## A function that spawns without a group

A function that spawns without opening its own group spawns into its caller's, and it carries the `spawn` effect. The caller that opens the group does not carry it.

```
use std.bits (i32)

fn start(n: i32) ! spawn {
  spawn { n * 2 }
  ()
}

fn start_two() {
  with tasks()
  start(1)
  start(2)
}
```

Leaving `spawn` out of `start` is refused, as is any effect left out of a signature. The compiler does not build this refusal, so no words are quoted.

## The forms

| Written | Is |
| --- | --- |
| `with tasks()` | A task group for the rest of the block |
| `spawn { e }` | A child that runs `e`, in the nearest group |
| `join(t)` | The value of the child `t`, after waiting for it |
| `first(t, rest)` | The value of the first of `t` and the children in `rest` to finish |
| `! spawn` | A function that spawns into its caller's group |

Next: [What a spawned task may use](task-sharing.md)
