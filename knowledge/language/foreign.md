---
type: specification
title: Foreign calls
description: A foreign declaration states a C signature by hand, is called directly in the C calling convention, and reaches the operating system and its libraries only.
status: planned
---

# Foreign calls

A foreign declaration states a C signature by hand and carries the `foreign` effect. It is the one door out of the language, and no code is privileged to use another.

```
foreign "libSystem" fn write(fd: i32, buf: Ptr[u8], len: usize) -> isize ! foreign
```

The declaration names the library, `"libSystem"`, then the function with its C signature. The standard library goes through this door like any program: a system call on Linux is declared the same way, with the kernel as the library.

## The types of a signature

`Ptr[T]`, `usize`, and `isize` exist for foreign signatures and are not used in ordinary code.

```
fn write_bytes(buf: Ptr[Byte], len: usize) -> isize ! foreign {
  write(1, buf, len)
}
```

`write_bytes` is ordinary code that calls `write`, and its signature has the `Ptr[u8]` and `usize` that `write` has.

## Trusted

Declarations are trusted, so they live in a few audited modules.

## How a call is made

A foreign call is a direct call in the C calling convention, with nothing converted on the way. The `fd`, `buf`, and `len` of `write` are passed as they are.

## What a foreign call may reach

Foreign calls reach the operating system and its libraries only. A Smith program has no linker and vendors only Smith source, so it cannot bring in a library written in another language. `"libSystem"` above is the operating system's, and the kernel of Linux is its library there.

## The forms

| Written | Is |
| --- | --- |
| `foreign "lib" fn f(...) -> T ! foreign` | A foreign function `f` of the library `lib` |
| `Ptr[T]`, `usize`, `isize` | Types for foreign signatures |

Next: [The foreign effect](foreign-effect.md)
