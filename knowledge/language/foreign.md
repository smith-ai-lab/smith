---
type: specification
title: Foreign calls and native applications
description: Foreign declarations with C signatures, and how system frameworks are reached for native applications.
status: planned
---

# Foreign calls and native applications

A foreign declaration states a C signature by hand and carries the `foreign` effect. Declarations are trusted, so they live in a few audited modules.

```
foreign "libSystem" fn write(fd: i32, buf: Ptr[u8], len: usize) -> isize ! foreign
```

`Ptr[T]`, `usize`, and `isize` exist for foreign signatures and are not used in ordinary code.

A foreign declaration is the one door out of the language, and no code is privileged to use another. The standard library goes through it like any program: a system call on Linux is declared the same way, with the kernel as the library.

- The compiler cannot see what foreign code does, so `foreign` cannot be handled away. A function that makes a foreign call carries `foreign`, and so does every caller, up to `main`. A dependency cannot make one unseen.
- A foreign call is kept in one place by putting it behind an [effect](effect-declarations.md). The lambdas of a [handler](handlers.md) are checked against the function that installs it, so only that function carries `foreign`, and the code running under the handler carries the effect it handles. A program that wraps a system library declares an effect for it and installs its handler once, near `main`.
- This is how `print` carries `console` alone. An executable starts in a short piece of the standard library that the operating system calls. It holds every effect, installs the real handlers for the effects `main` lists, and then calls `main`.
- Foreign calls reach the operating system and its libraries only. A Smith program has no linker and vendors only Smith source, so it cannot bring in a library written in another language.
- A foreign call is a direct call in the C calling convention, with nothing converted on the way.

System frameworks are reached the same way, with no C or Objective-C source in a Smith program.

- On macOS and iOS, a Smith library wraps AppKit and UIKit by calling the Objective-C runtime's C functions. Windows, menus, and native controls are ordinary Smith values behind `with`.
- The OS still calls in for a few things, such as a window procedure or a delegate method. The platform library exports a small Smith function with the C calling convention that pushes those onto an event queue, which is what `next_event` reads.
- The `ui` handler runs its tasks on the main thread.
- Rendering goes to a GPU layer in the window, filled each frame by Smith's own kernels.

Next: [Modules](modules.md)
