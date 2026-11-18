---
type: specification
title: System frameworks
description: How a native application reaches AppKit and UIKit through the Objective-C runtime's C functions, with no C or Objective-C source, and how the OS calls back in.
status: planned
---

# System frameworks

System frameworks are reached the same way as any foreign function, with no C or Objective-C source in a Smith program.

- On macOS and iOS, a Smith library wraps AppKit and UIKit by calling the Objective-C runtime's C functions. Windows, menus, and native controls are ordinary Smith values behind [`with`](with.md).

```
fn main() ! ui, diverge {
  with app = application()
  with win = window(app, "Smith", 800, 600)
  loop {
    match next_event(win) {
      .close { break }
      .key(k) { handle_key(k) }
      .frame(t) { draw(win, t) }
    }
  }
}
```

`application()` and `window(...)` are functions of that library, and each is a value behind `with`, as the example of [Events](events.md) shows. The library calls the Objective-C runtime's C functions through [foreign declarations](foreign.md); the program has none.

## When the OS calls in

The OS still calls in for a few things, such as a window procedure or a delegate method. The platform library exports a small Smith function with the C calling convention that pushes those onto an event queue, which is what `next_event` reads. This is why the program reads events and has no callback.

## Threads and drawing

- The `ui` handler runs its tasks on the main thread. The loop of `main` above runs there.
- Rendering goes to a GPU layer in the window, filled each frame by Smith's own kernels. The `.frame(t)` event is where a program draws, as `draw(win, t)` does above.

## The forms

| What | How it is reached |
| --- | --- |
| AppKit and UIKit | The Objective-C runtime's C functions, from a Smith library |
| A window, a menu, a control | An ordinary Smith value behind `with` |
| A call from the OS | A Smith function with the C calling convention that pushes onto an event queue |
| Drawing | Smith's own kernels, into a GPU layer |

Next: [Modules](modules.md)
