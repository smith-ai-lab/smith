---
type: specification
title: Events
description: A native application is a task that waits for the next event, so user code has no callbacks, and the window lives exactly as long as main.
status: planned
---

# Events

Events are handled the same way as other I/O. A native application is a task that waits for the next event, so user code has no callbacks.

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

`next_event(win)` waits for the next event, as `fetch(a)` waits for a page, and the `match` says what each event does. `handle_key` and `draw` are ordinary functions.

## The window lives as long as main

`with app = application()` and `with win = window(...)` are [`with`](with.md), so each lasts for the rest of the block. The window lives exactly as long as `main`: `.close` breaks the loop, `main` ends, and the window with it.

## The forms

| Written | Is |
| --- | --- |
| `next_event(win)` | Waits for the next event of the window |
| `match e { .close ... .key(k) ... .frame(t) ... }` | What to do for each event |

Next: [Foreign calls](foreign.md)
