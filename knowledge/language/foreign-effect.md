---
type: specification
title: The foreign effect
description: Why foreign cannot be handled away, how a foreign call is kept in one place behind an effect, and how the start of an executable carries it so that print carries console alone.
status: planned
---

# The foreign effect

The compiler cannot see what foreign code does, so `foreign` cannot be handled away. A function that makes a foreign call carries `foreign`, and so does every caller, up to `main`. A dependency cannot make one unseen.

```
fn write_bytes(buf: Ptr[u8], len: usize) -> isize ! foreign {
  write(1, buf, len)
}

fn announce(buf: Ptr[u8], len: usize) ! foreign {
  write_bytes(buf, len)
  ()
}
```

`announce` calls `write_bytes`, so it carries `foreign`. A `main` that calls `announce` carries it too, and a `main` that leaves it out is refused. The compiler does not build this refusal, so no words are quoted.

## Keeping a call in one place

A foreign call is kept in one place by putting it behind an [effect](effect-declarations.md). The lambdas of a [handler](handlers.md) are checked against the function that installs it, so only that function carries `foreign`, and the code running under the handler carries the effect it handles.

A program that wraps a system library declares an effect for it and installs its handler once, near `main`.

```
effect beep {
  fn ring() -> isize
}

fn bell[T](buf: Ptr[u8], body: fn() -> T ! beep) -> T ! foreign {
  beep(ring: { write(1, buf, 1) }, body: body)
}

fn ding() -> isize ! beep { ring() }

fn rung(buf: Ptr[u8]) -> isize ! foreign {
  with bell(buf)
  ding()
}
```

`ding` carries `beep` and not `foreign`: it runs under the handler. `bell` installs the handler and calls `write`, so it carries `foreign`, and so does `rung`, which calls `bell`.

## How print carries console alone

This is how `print` carries `console` alone. An executable starts in a short piece of the standard library that the operating system calls. It holds every effect, installs the real handlers for the effects `main` lists, and then calls `main`.

```
fn main() ! console {
  print("hello")
}
```

`main` lists `console` and not `foreign`. The piece that calls `main` installs the real handler for `console`, so `main` does not carry `foreign`.

## The forms

| What | Carries `foreign` |
| --- | --- |
| A function that makes a foreign call | Yes |
| Every caller of it, up to `main` | Yes |
| The function that installs a handler that calls foreign code | Yes |
| The code running under that handler | No: it carries the effect the handler handles |

Next: [System frameworks](system-frameworks.md)
