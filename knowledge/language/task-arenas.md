---
type: specification
title: Memory of a task
description: Each child task gets its own arena, its result is built in its parent's, and a value lives as long as the task that created it.
status: planned
---

# Memory of a task

Each task has an [arena](arenas.md) of its own.

- Each child task gets its own arena, and its result is built in the parent's. In `fetch_both` of [Tasks](tasks.md), the page that `fetch(a)` gives is built in the arena of `fetch_both`.
- A value lives as long as the task that created it, and tasks pass plain data to each other.
- A server is one task per connection with one arena per request. `serve` of [What a spawned task may use](task-sharing.md) starts a task for each `conn`, and `handle` opens `with arena()` for each request, which frees it whole when the request is answered.

```
fn read_request(conn: Conn) -> Request ! net, alloc
fn respond(conn: Conn, request: Request) ! net, alloc

fn handle(conn: Conn) ! net, alloc, diverge {
  loop {
    with arena()
    request = read_request(conn)
    respond(conn, request)
  }
}
```

## The forms

| What | Has its own arena |
| --- | --- |
| A child task | Yes, and its result is built in the parent's |
| A request of a server | Yes, from `with arena()` |

Next: [Handlers and tasks](task-handlers.md)
