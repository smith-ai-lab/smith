---
type: specification
title: Handlers
description: A handler, a function that runs a lambda with an effect handled, how it is written with the effect's name, what it keeps, and what it may do.
status: planned
---

# Handlers

A handler is a function that runs a lambda with an effect handled, and it is installed with [`with`](with.md). It is declared by putting the effect on its lambda parameter, and it handles the effect by calling the effect's name, as [Declaring an effect](effect-declarations.md) describes.

```
fn fixed_clock[T](body: fn() -> T ! time) -> T {
  time(now: { 42 }, body: body)
}

fn reading() -> u64 {
  with fixed_clock()
  now()
}
```

`reading()` is `42` and carries no effect. `fixed_clock` gives `time` an operation, the lambda `{ 42 }`, and runs `body` with it in force, so inside `reading` the call `now()` runs that lambda.

## What a handler takes off a signature

A function that runs under a handler does not carry the effect the handler handles. `read_all` is shown by its signature alone, and `memory_fs` handles `fs` with an in-memory file system, so `fixture_config` does not carry `fs`.

```
fn read_all(path: str) -> Result[List[u8], FsError] ! fs, alloc

fn memory_fs[T](files: Files, body: fn() -> T ! fs) -> T

fn fixture_config() -> Result[Config, FsError] ! alloc {
  with memory_fs(fixtures)
  bytes = try read_all("model.toml")
  parse_config(bytes)
}
```

## A handler that keeps state

A handler is an ordinary function. This one is a clock for tests, which reads one tick later every time it is asked.

```
fn fake_clock[T](body: fn() -> T ! time) -> T {
  var ticks: u64 = 0
  time(
    now: {
      ticks = saturating_add(ticks, 1)
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

`two_readings` returns `(1, 2)` and carries no effect. Each `now()` runs the lambda that `fake_clock` supplied.

- A handler keeps its state in a `var`. The lambda assigns `ticks`, a local of the function it is written in, which a lambda may do because one that assigns a name around it is only passed to a call, and cannot outlive that function.
- An operation is an ordinary call. Its lambda runs and returns a value to the code that asked, so a handler cannot stop that code or run it twice. Each `now()` above gets one value, and the code after it goes on.

## Handlers inside handlers

The lambda of an operation runs under the handlers outside its own. A handler whose operation reads the clock gets the clock that was in force around its `with`.

```
fn doubled_clock[T](body: fn() -> T ! time) -> T ! time {
  time(now: { now() * 2 }, body: body)
}

fn doubled() -> u64 {
  with fixed_clock()
  with doubled_clock()
  now()
}
```

`doubled()` is `84`: `now()` runs the lambda of `doubled_clock`, whose `now()` runs the lambda of `fixed_clock`. The lambda's own effects are checked where it is written, like any lambda's, so `doubled_clock` lists `time`. Without it the function is refused: "`now` needs the `time` effect, which `doubled_clock` does not declare".

A handler whose operations print must sit in a function that carries `console`, and the code running under the handler does not.

```
fn loud_clock[T](body: fn() -> T ! time) -> T ! console {
  time(
    now: {
      print("asked")
      7
    },
    body: body,
  )
}

fn main() ! console {
  with loud_clock()
  now()
}
```

`main` carries `console` for the `print` that `loud_clock` does, and the code after `with` needs only `time`. Without `! console` on `loud_clock` it is refused: "`print` needs the `console` effect, which `loud_clock` does not declare".

## How a handler is chosen

The handler in force at every call is known when the program is built, because `with` is part of the source and a lambda cannot escape. The compiler builds a copy of a function for each handler it runs under, as it does for each type a generic function is used with. `report` below is built twice.

```
fn fixed[T](tick: u64, body: fn() -> T ! time) -> T {
  time(now: { tick }, body: body)
}

fn report() -> u64 ! time { now() }

fn at_one() -> u64 {
  with fixed(1)
  report()
}

fn at_two() -> u64 {
  with fixed(2)
  report()
}
```

`at_one()` is `1` and `at_two()` is `2`, and `report` costs what its body costs under each handler.

## The forms

| Written | Means |
| --- | --- |
| `fn h[T](body: fn() -> T ! e) -> T` | A handler of `e`: it runs `body` with `e` handled |
| `e(op: { ... }, body: body)` | Inside a handler, the operation `op` is that lambda |
| `with h()` then lines | The lines run under `h`, so they do not carry `e` |
| `var` in a handler | The handler's state, assigned by its lambdas |
| `fn h[T](body: fn() -> T ! e) -> T ! f` | A handler whose lambdas use `f`, which its caller must have |

Next: [The effects Smith provides](provided-effects.md)
