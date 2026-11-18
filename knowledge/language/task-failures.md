---
type: specification
title: A failed task
description: What join gives the task that waits for a child that ended by a failure, outcome, which lets that task recover, and a child that is never joined.
status: planned
---

# A failed task

A child that ended by a [failure](task-ending.md) was not given a value, so `join(t)` has none to give. A task that joins it ends by the same failure.

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

If `tb` runs out of memory, the `join(tb)` of `fetch_both` ends the task that runs `fetch_both` by running out of memory, and so does each parent that joins that task in turn. A `.err` that `fetch` returns is not a failure: it is a value, which `try` takes as it always does.

## The root

`main` is the root task, so a failure that reaches it ends the program with a failing status.

```
fn main() ! net, alloc, console {
  match fetch_both("a.example", "b.example") {
    .ok(pages) { print("fetched") }
    .err(e) { print("failed") }
  }
}
```

If either child of `fetch_both` runs out of memory, `main` ends the same way and the program ends with a failing status. It prints nothing.

## outcome

`outcome(t)` waits as `join` does and returns a `Result` whose error says which failure it was, so the task that calls it can recover.

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

The `.ok` of `outcome` holds what `fetch` gave, a `Result` of its own. The `.err` is the failure of the task, and `fetch_or_empty` does not end.

## A child that is never joined

A child that is never joined ends alone. This is why one connection of `serve` that runs out of memory does not end the server.

```
fn serve(listener: Listener) ! net, alloc, diverge {
  with tasks()
  loop {
    conn = accept(listener)
    spawn { handle(conn) }
  }
}
```

Nothing joins the child of a `conn`, so if `handle(conn)` runs out of memory, only that task ends.

## The forms

| Written | A child that ended by a failure |
| --- | --- |
| `join(t)` | Ends the joining task by the same failure |
| `outcome(t)` | Gives `.err` that says which failure, and the task goes on |
| Never joined | Ends alone |

Next: [Events](events.md)
