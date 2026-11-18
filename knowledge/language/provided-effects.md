---
type: specification
title: The effects Smith provides
description: The five effects the standard library declares, the six the compiler gives, and where an effect that nothing handles ends.
status: partial
built: "`console`, `alloc`, and `diverge` in a signature, `console` required by `print`, `alloc` by a string that holds an expression, and `diverge` by an open-ended `loop` with no `decreases`"
---

# The effects Smith provides

Smith provides eleven effects, which a signature lists by name like any other. Five are declared by the standard library, and a program can handle them. Six are given by the compiler.

```
fn main() ! console {
  print("hello")
}
```

## Declared by the standard library

The standard library declares five effects. Each has operations, and a program can handle it. Each is imported by name, as any effect is: none is in [the prelude](prelude.md).

| Effect | Allows |
| --- | --- |
| [`console`](../library/console.md) | Standard input, output, and error |
| [`fs`](../library/fs.md) | Files |
| `net` | The network |
| `time` | The clock |
| `ui` | Windows and events |

`console` is needed by `print`, and a program imports both with `use std.console (console, print)`. A function that calls it lists `console`, as `main` does above.

## Built in

Six more are built in, because the compiler itself gives them their meaning. They have no operations to replace.

| Effect | Allows | Ends at |
| --- | --- | --- |
| `alloc` | Memory whose size is known only at run time | An [arena](arenas.md) |
| `spawn` | Starting a task | A [task group](tasks.md) |
| `gpu` | Opening a [device](devices.md) to run on a GPU | Nothing |
| `diverge` | Not terminating | Nothing |
| `foreign` | Calling a [foreign function](foreign.md) | Nothing |
| `exec` | Running another program | Nothing |

A string that holds an expression needs `alloc`, so the function that writes it lists it.

```
fn greet(name: str) -> str ! alloc { "hello {name}" }
```

An open-ended `loop` with no `decreases` may not end, so the function that has it lists `diverge`, and so does every caller.

```
fn spin() ! diverge {
  loop { }
}

fn main() { spin() }
```

This gives "`spin` needs the `diverge` effect, which `main` does not declare", with the fix "write `fn main() ! diverge`". With `loop decreases n`, as [the open-ended `loop`](open-loop.md#a-loop-that-is-proven-to-end) describes, the loop is proven to end and the function lists nothing.

## Where an effect ends

An effect that nothing handles is carried by every caller up to [`main`](first-program.md), where a program starts. The effects `main` lists are provided by the operating system.

```
fn greet(name: str) ! console, alloc {
  print("hello {name}")
}

fn main() ! console, alloc {
  greet("Ana")
}
```

`greet` carries `console` and `alloc` for what it does, and `main` lists them because no handler sits between them. A handler between them takes the effect off the signature of what lies under it, as [Handlers](handlers.md) shows.

Next: [Strings that hold expressions](interpolation.md)
