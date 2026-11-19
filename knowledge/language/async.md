---
type: specification
title: Async
description: All I/O is async and reads as sequential code, suspending a task under an async handler and blocking under a blocking one.
status: planned
---

# Async

All I/O is async and reads as sequential code. A call such as `fetch(a)` carries the `net` effect, and the source is the same whichever handler runs it.

```
fn fetch_pair(a: str, b: str) -> Result[(List[Byte], List[Byte]), NetError] ! net, alloc {
  pa = try fetch(a)
  pb = try fetch(b)
  .ok((pa, pb))
}
```

Under an async handler the call suspends its task. Under a blocking handler it blocks. `fetch_pair` is written once for both.

## Who may suspend a task

Only the operating system's handlers can suspend a task. A handler written in Smith returns from each operation, so a call under one does not suspend. `fixed_clock` of [Handlers](handlers.md) is such a handler: `now()` under it gives its value and returns.

## What it compiles to

- A function with a suspending effect compiles to a state machine whose frame lives in its task's arena. Tasks need no stacks of their own. `fetch_pair` is such a function, since it carries `net`, which suspends under an async handler.
- Pure code compiles as ordinary functions. `reading` of [Handlers](handlers.md) carries no effect, so it is an ordinary function.

## The forms

| What the call is under | What it does |
| --- | --- |
| An async handler of the operating system | Suspends its task |
| A blocking handler of the operating system | Blocks |
| A handler written in Smith | Returns from the operation |

Next: [Cancellation](cancellation.md)
