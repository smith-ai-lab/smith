---
type: specification
title: Cancellation
description: Ending a group's block is the one way to cancel a task, and a cancelled task ends at once if it is waiting and at its next suspension if it is running.
status: planned
---

# Cancellation

Ending a group's block is the one way to cancel. When the block of `with tasks()` ends, each child still running is cancelled. User code cannot end a task in any other way.

```
fn first_page(a: str, b: str) -> Result[List[u8], NetError] ! net, alloc {
  with tasks()
  ta = spawn { fetch(a) }
  tb = spawn { fetch(b) }
  join(ta)
}
```

`first_page` joins `ta` only. When the block ends, `tb` is cancelled if it has not finished.

## When it takes effect

A cancelled task that is waiting, as for a read, ends at once. One that is running ends the next time it would suspend, so code that never suspends is never interrupted.

```
fn busy(n: u64) -> u64 {
  var total: u64 = 0
  loop i in 0..n { total = wrapping_add(total, i) }
  total
}

fn abandon(n: u64) {
  with tasks()
  spawn { busy(n) }
  ()
}
```

`busy` carries no effect and so never suspends. The block of `abandon` ends at once, and the block waits until `busy` has ended on its own, as [Tasks](tasks.md) says.

## Its own children

A task's own children are cancelled with it.

```
fn outer(a: str) ! net, alloc {
  with tasks()
  spawn {
    with tasks()
    spawn { fetch(a) }
    ()
  }
  ()
}
```

When the block of `outer` ends, the child is cancelled, and the grandchild that fetches with it.

## The forms

| The task | Ends |
| --- | --- |
| Waiting, as for a read | At once |
| Running and about to suspend | When it would suspend |
| Running and never suspending | Not interrupted |
| Having children | They are cancelled with it |

Next: [How a task ends](task-ending.md)
