---
type: specification
title: Tasks and async
description: Task groups with spawn and join, cancellation, async I/O that reads as sequential code, how a task ends, and events.
---

# Tasks and async

Concurrent work runs in tasks, and there are no raw threads or locks. Data is immutable or uniquely owned, so data races cannot be written. `with tasks()` opens a task group. `spawn` takes the child's work as a lambda and starts it in the nearest group, and `join` waits for the child and gives its value. No child outlives the block. When the block ends, the children still running are cancelled, and the block waits until they have ended, so a child whose work must finish is joined. A function that spawns without opening its own group carries the `spawn` effect.

```
fn fetch_both(a: str, b: str) -> Result[(List[u8], List[u8]), NetError] ! net, alloc {
  with tasks()
  ta = spawn { fetch(a) }
  tb = spawn { fetch(b) }
  pa = try join(ta)
  pb = try join(tb)
  .ok((pa, pb))
}
```

A spawned lambda runs at the same time as its parent, so three rules say what it may use.

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

Several tasks may therefore call one [handler](effects.md). Its operations run one at a time, so the handler's `var`s are never assigned by two tasks at once. Each operation sees the handler's state whole and leaves it whole, as a read or a write does on a real file system. The order in which tasks reach a handler is not fixed, so a sequence of operations in one task can be interleaved with another task's.

All I/O is async and reads as sequential code. A call such as `fetch(a)` carries the `net` effect. Under an async handler it suspends its task, and under a blocking handler it blocks, with the same source either way. Only the operating system's handlers can suspend a task. A handler written in Smith returns from each operation.

- A function with a suspending effect compiles to a state machine whose frame lives in its task's arena. Tasks need no stacks of their own, and pure code compiles as ordinary functions.
- Each child task gets its own arena, and its result is built in the parent's.
- A server is one task per connection with one arena per request.
- A value lives as long as the task that created it, and tasks pass plain data to each other.

Three things end a task before its work is done. Two are failures: running out of memory, and a console write that the system refuses, as when output is piped to a program that has stopped reading. The third is being cancelled. In each case the task's arena is dropped whole, the `fs` and `net` handlers close the handles it left open, and the code it was running sees nothing and runs no further. No other task shares its memory, so none is left in a broken state.

- Only the operating system's handlers end a task. A cancelled task that is waiting, as for a read, ends at once. One that is running ends the next time it would suspend, so code that never suspends is never interrupted. Its own children are cancelled with it.
- Ending a group's block is the one way to cancel. User code cannot end a task in any other way.
- `join(t)` gives the child's value. If the child ended by a failure, the task that joins it ends by the same failure, and so does each parent that joins in turn. `main` is the root task, so a failure that reaches it ends the program with a failing status.
- `outcome(t)` waits as `join` does and returns a `Result` whose error says which failure it was, so the task that calls it can recover.
- A child that is never joined ends alone. This is why one connection of `serve` that runs out of memory does not end the server.

```
fn fetch_or_empty(url: str) -> List[u8] ! net, alloc {
  with tasks()
  t = spawn { fetch(url) }
  match outcome(t) {
    .ok(.ok(page)) { page }
    .ok(.err(e)) { [] }
    .err(failure) { [] }
  }
}
```

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
