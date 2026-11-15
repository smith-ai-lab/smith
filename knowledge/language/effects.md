---
type: specification
title: Effects
description: Effects listed in a signature as permissions, how an effect is declared, how a handler is used and written, and the effects Smith provides.
---

# Effects

A function with no `!` clause is pure. Effects are listed after the return type, and a caller must have every effect its callees use. A function without an effect cannot perform it, so effects work as permissions. Below, `load` is shown by its signature alone.

```
fn load(path: str) -> Result[Bytes, IoError] ! fs, alloc

fn fixture_config() -> Result[Config, IoError] ! alloc {
  with memory_fs(fixtures)
  bytes = try load("model.toml")
  parse_config(bytes)
}
```

An effect is a parameter that the compiler passes for you. A signature states it, a call does not, and it costs nothing at run time. So a signature says everything a function may do, while the code that calls it stays plain.

- The stored signature always states the effect list. The compiler proposes it while the function is being written, and lambdas are inferred.
- A lambda's other effects are checked where it is written: whatever its body does must be allowed by the function it sits in, and the function receiving it says nothing about them. This is sound because a lambda passed to a call cannot be stored or returned. There are no effect variables.
- A function stored in a value states its effects in its type, and is pure when none are written.

## Declaring an effect

An effect is declared by listing its operations. The standard library declares `time` this way, shown here with only its operation for reading the clock.

```
effect time {
  fn now() -> u64
}
```

The declaration gives two things, and nothing else is generated.

- Each operation is an ordinary function that carries the effect. `now()` has the signature `fn now() -> u64 ! time`.
- The effect's name is a function that handles it. It takes a lambda for each operation, named as the operation is, and the body to run. Calling it is to an effect what calling a struct's name is to a struct.

A program declares its own effects in the same way, so nothing about `time` is special. An effect is a name like any other: it is defined in one module and imported by name, as [Modules](modules.md) describes.

## Handlers

A handler is a function that runs a lambda with an effect handled, installed with `with`. It is declared by putting the effect on its lambda parameter, as in `fn memory_fs[T](files: Files, body: () -> T ! fs) -> T`. `memory_fs` above handles `fs` with an in-memory file system, so `fixture_config` does not carry `fs`.

A handler is an ordinary function that calls the effect's name. This one is a clock for tests, which reads one tick later every time it is asked.

```
fn fake_clock[T](body: () -> T ! time) -> T {
  var ticks: u64 = 0
  time(
    now: fn() {
      ticks := saturating_add(ticks, 1)
      ticks
    },
    body: body,
  )
}

fn two_readings() -> (u64, u64) {
  with fake_clock()
  (now(), now())
}
```

`two_readings` returns `(1, 2)` and carries no effect. Inside it, each `now()` runs the lambda that `fake_clock` supplied.

- A handler keeps its state in a `var`. The lambda assigns `ticks`, a local of the function it is written in, which a lambda passed to a call may do because it cannot outlive that function.
- An operation is an ordinary call. Its lambda runs and returns a value to the code that asked, so a handler cannot stop that code or run it twice.
- The lambda runs under the handlers outside its own. An operation of `fake_clock` that read the clock would get the clock that was in force around `with fake_clock()`.
- The lambda's own effects are checked where it is written, like any lambda's. A handler whose operations read files must sit in a function that carries `fs`, and the code running under the handler does not.
- The handler in force at every call is known when the program is built, because `with` is part of the source and a lambda cannot escape. The compiler builds a copy of a function for each handler it runs under, as it does for each type a generic function is used with.

## The effects Smith provides

The standard library declares five effects. Each has operations, and a program can handle it.

| Effect | Allows |
| --- | --- |
| [`console`](../library/console.md) | Standard input, output, and error |
| `fs` | Files |
| `net` | The network |
| `time` | The clock |
| `ui` | Windows and events |

Six more are built in, because the compiler itself gives them their meaning. They have no operations to replace.

| Effect | Allows | Ends at |
| --- | --- | --- |
| `alloc` | Memory whose size is known only at run time | An [arena](storage.md) |
| `spawn` | Starting a task | A [task group](tasks.md) |
| `gpu` | Running on a GPU | A [device](devices.md) |
| `diverge` | Not terminating | Nothing |
| `foreign` | Calling a [foreign function](foreign.md) | Nothing |
| `exec` | Running another program | Nothing |

An effect that nothing handles is carried by every caller up to `main`. The effects `main` lists are provided by the operating system.

Next: [Text and printing](text.md)
