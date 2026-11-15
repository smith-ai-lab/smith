---
type: specification
title: Tasks and async
description: Task groups with spawn and join, async I/O that reads as sequential code, running out of memory, and events.
---

# Tasks and async

Concurrent work runs in tasks, and there are no raw threads or locks. Data is immutable or uniquely owned, so data races cannot be written. `with tasks()` opens a task group. `spawn` takes the child's work as a trailing block (see [Lambdas, `with`, and `try`](lambdas.md)) and starts it in the nearest group, and `join` waits for its result. Every child finishes before the block ends. A function that spawns without opening its own group carries the `spawn` effect.

```
fn fetch_both(a: str, b: str) -> Result[(Bytes, Bytes), NetError] ! net, alloc {
  with tasks()
  ta = spawn { fetch(a) }
  tb = spawn { fetch(b) }
  pa = try join(ta)
  pb = try join(tb)
  .ok((pa, pb))
}
```

A spawned block runs at the same time as its parent, so three rules say what it may use.

- An ordinary value is shared. It is immutable, and the child finishes before the group's block ends, so nothing is copied.
- A `var` is read as its value at the moment of the spawn and cannot be assigned. Results come back through `join`.
- A single-use value moves into the child, and the parent cannot use it afterwards.

```
fn serve(listener: Listener) ! net, alloc, diverge {
  with tasks()
  loop {
    conn = accept(listener)
    spawn { handle(conn) }
  }
}
```

Here `conn` belongs to the child after the spawn. A child runs under the handlers in force where it was spawned, except that it gets its own arena.

All I/O is async and reads as sequential code. A call such as `fetch(a)` carries the `net` effect. Under an async handler it suspends its task, and under a blocking handler it blocks, with the same source either way.

- A function with a suspending effect compiles to a state machine whose frame lives in its task's arena. Tasks need no stacks of their own, and pure code compiles as ordinary functions.
- Each child task gets its own arena, and its result is built in the parent's.
- A server is one task per connection with one arena per request.
- A value lives as long as the task that created it, and tasks pass plain data to each other.

Running out of memory ends the task that ran out, and nothing else. Its arena is dropped whole, the `fs` and `net` handlers close the handles it left open, and its parent sees the failure as an error from `join`. No other task shares its memory, so none is left in a broken state. Only the `alloc` handler can end a task, and user code has no way to ask for it. `main` is the root task, so running out of memory there ends the program.

Events are handled the same way. A native application is a task that waits for the next event, so user code has no callbacks, and the window lives exactly as long as `main`.

```
fn main() ! ui, diverge {
  with app = application()
  with win = window(app, "Smith", 800, 600)
  loop {
    e = next_event(win)
    match e {
      .close { break }
      .key(k) { handle_key(k) }
      .frame(t) { draw(win, t) }
    }
  }
}
```

Next: [Foreign calls and native applications](foreign.md)
