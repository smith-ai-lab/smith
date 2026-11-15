---
type: specification
title: Foreign calls and native applications
description: Foreign declarations with C signatures, and how system frameworks are reached for native applications.
---

# Foreign calls and native applications

A foreign declaration states a C signature by hand and carries the `foreign` effect. Declarations are trusted, so they live in a few audited modules.

```
foreign "libSystem" fn write(fd: i32, buf: Ptr[u8], len: usize) -> isize ! foreign
```

`Ptr[T]`, `usize`, and `isize` exist for foreign signatures and are not used in ordinary code.

System frameworks are reached the same way, with no C or Objective-C source in a Smith program.

- On macOS and iOS, a Smith library wraps AppKit and UIKit by calling the Objective-C runtime's C functions. Windows, menus, and native controls are ordinary Smith values behind `with`.
- The OS still calls in for a few things, such as a window procedure or a delegate method. The platform library exports a small Smith function with the C calling convention that pushes those onto an event queue, which is what `next_event` reads.
- The `ui` handler runs its tasks on the main thread.
- Rendering goes to a GPU layer in the window, filled each frame by Smith's own kernels.

Next: [Modules](modules.md)
