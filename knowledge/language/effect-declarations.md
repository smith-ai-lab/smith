---
type: specification
title: Declaring an effect
description: How an effect is declared by listing its operations, what a declaration gives, and how a program defines and imports effects of its own.
status: partial
built:
  - "`effect e { fn op(a: T = x) -> U }` declares an effect, and each operation is a function with no body that carries the effect, with the parameters, result, and defaults of any function, so a caller lists the effect, whether it calls the operation or a function that carries it, and a default may be left out of a call"
  - "`pub effect` in a module is imported with `use` by its name, and each operation by its own name, and an effect or an operation used with no import is refused as any name is"
  - "`pub` before an operation of an effect, which another module imports by its own name and calls, and an operation without it, refused in another module's `use` as any private name is, and refused in a handler written there with one error at the effect's name that lists the private operations"
  - "`console` is declared by `std/console.smith` and `print` is its operation, which a program that reaches `main` with no handler has answered by the target"
  - "the effect's name is a function that takes a lambda for each operation, each taking every parameter of the operation, and the body to run, and gives what the body gives"
  - "an effect that the program declares and lists in `main`, which the target does not provide and no handler answers, is one error at its name in the list that names the effect"
---

# Declaring an effect

An effect is declared by listing its operations. The standard library declares `time` this way, shown here with only its operation for reading the clock.

```
effect time {
  fn now() -> u64
}
```

An operation is written as a function with no body.

## What a declaration gives

The declaration gives two things, and nothing else is generated.

- Each operation is an ordinary function that carries the effect. `now()` has the signature `fn now() -> u64 ! time`.
- The effect's name is a function that handles it. It takes a lambda for each operation, named as the operation is, and the body to run. Calling it is to an effect what calling a struct's name is to a struct: the arguments are given by name.

## An operation carries its effect

Because `now` carries `time`, a caller must have `time`, as for any function with an effect.

```
use std.bits (u64)

fn stamp() -> u64 ! time { now() }
```

`print` is an operation of `console` in the same way, so a function that calls it without `console` is refused: "`print` needs the `console` effect, which `greet` does not declare". [Effects](effects.md) shows the whole refusal.

## The name that handles it

The name of the effect is called with a lambda for each operation, and then the body. [Handlers](handlers.md) says what to do with it. For `time` it reads:

```
time(now: { 42 }, body: body)
```

## Defaults

An operation may give its parameters defaults, like any function. The lambda that a handler supplies for it takes every argument, whether or not a call gave it.

```
effect log {
  fn write(text: str, level: i32 = 0)
}
```

A call may leave `level` out, as in `write("started")`, or give it, as in `write("failed", level: 2)`. The lambda that handles `write` names both parameters.

```
log(write: fn(text, level) { ... }, body: body)
```

## An effect of your own

A program declares its own effects in the same way, so nothing about `time` is special. An effect is a name like any other: it is defined in one module and imported by name, as [Importing with use](use.md) describes.

```
pub effect log {
  pub fn write(text: str, level: i32 = 0)
}
```

Written in `app/log.smith`, this effect is imported by another file with `use app.log (log, write)`: importing an effect brings in its name and nothing else, so each operation the file calls is imported by its own name, as [Nothing is imported implicitly](use.md#nothing-is-imported-implicitly) describes. The effects the standard library declares need no import, as [The effects Smith provides](provided-effects.md) says.

## An operation is private unless it is marked pub

An operation follows the rule of everything in a module, as a field of a struct does: it is private unless `pub` is written before it. `pub effect` opens the effect's name, and `pub fn` inside it opens that operation.

```
use std.console (console, print)

pub effect ledger {
  fn record(line: str)
}

pub fn pay(amount: Nat) ! ledger, alloc {
  record("paid {amount}")
}

pub fn audit[T](body: fn() -> T ! ledger) -> T ! console {
  ledger(record: fn(line) { print(line) }, body: body)
}
```

Another module imports `ledger`, writes `! ledger` on its own functions, calls `pay`, and runs under `audit`. It cannot call `record`: `use app.ledger (record)` is refused as any private name is. It cannot handle `ledger` either, since a handler names every operation, and one it cannot name it cannot give a function for. That is one error, at the effect's name in the handler, which lists the operations that are private: "`ledger` has operations that are private to `app.ledger`, so it cannot be handled here: `record`", with the fix "write `pub` before `record` in `app/ledger.smith`". So what is recorded is what this module's functions record, and nothing else.

An effect that any program may ask and answer, as `log` above, marks each operation `pub`.

## The forms

| Written | Means |
| --- | --- |
| `effect e { fn op(a: T) -> U }` | An effect `e` with the operation `op`, which carries `e` |
| `fn op(a: T = x)` in an effect | An operation whose parameter has a default |
| `e(op: { ... }, body: b)` | `e`, called with a lambda for each operation and then the body, handles it |
| `pub effect e { ... }` | An effect that other modules may import |
| `pub fn op(...)` in an effect | An operation that other modules may call, and give a function for in a handler |
| `fn op(...)` in a `pub effect` | An operation that only its own module calls and handles |

Next: [Handlers](handlers.md)
