---
type: specification
title: Effects
description: An effect as something a function does outside itself, how a signature lists the effects a function may perform, and what a caller must have.
status: partial
built:
  - "`console`, `alloc`, and `diverge` in a signature, each listed once and required of every caller that needs it, and checked where a lambda is written"
  - "not an effect on a function type"
---

# Effects

An effect is something a function does outside itself, and a signature lists each one the function may do, after its return type with a `!`.

```
fn main() ! console {
  print("hello")
}
```

`! console` says that `main` may use the console, and `print` does. This prints `hello`.

## A function with no effect

A function with no `!` clause is pure. It does nothing outside itself, so it can be called anywhere.

```
fn first(a: i32, b: i32) -> i32 { a }
```

## Effects as permissions

A function without an effect cannot perform it, so an effect works as a permission. A pure function that prints is refused.

```
fn greet(name: str) {
  print("hello")
}

fn main() ! console {
  greet("Ana")
}
```

This gives "`print` needs the `console` effect, which `greet` does not declare", with the fix "write `fn greet(name: str) ! console`".

## Listing more than one

Effects are listed after the return type, separated by commas, in any order.

```
fn shout(name: str) -> str ! console, alloc {
  print("hey")
  "hello {name}"
}
```

`! alloc, console` means the same. An effect is named once in a list, so naming it again is refused.

```
fn main() ! console, console {
  print("hi")
}
```

This gives "the `console` effect is already listed".

## What a caller needs

A caller must have every effect its callees use. `main` below calls `shout`, which needs `console` and `alloc`.

```
fn main() ! console, alloc {
  print(shout("Ana"))
}
```

This prints `hey` and then `hello Ana`. A caller that lacks one of them is refused.

```
fn main() ! console {
  print(shout("Ana"))
}
```

This gives "`shout` needs the `alloc` effect, which `main` does not declare", with the fix "write `fn main() ! console, alloc`".

A call states no effect. `shout("Ana")` is written as it would be for a pure function, because an effect is a parameter that the compiler passes for you, and it costs nothing at run time. So a signature says everything a function may do, while the code that calls it stays plain.

## Who writes the list

The stored signature always states the effect list. The compiler proposes it while the function is being written, as the fix beside each refusal above does. A [lambda](lambdas.md) has no signature, and its effects are inferred.

## A lambda's effects

A lambda's effects are checked where it is written: whatever its body does must be allowed by the function it sits in, and the function receiving it says nothing about them. `do` below runs a lambda and states no effect.

```
fn do[T](body: () -> T) -> T { body() }

fn main() ! console {
  do { print("hi") }
}
```

This prints `hi`, because `main` allows `console`. With `fn main() {` in its place the program is refused: "`print` needs the `console` effect, which `main` does not declare".

A function that receives a lambda never lists the effects of the lambda, and no signature can say "whatever the lambda does". There are no effect variables. This is sound because the function that receives a lambda can call it and pass it on, and cannot store or return it, so the lambda runs only inside the call it is written at. A [lambda that is kept](lambdas.md#where-a-lambda-may-go) is a function stored in a value, and states its effects as the next section says.

## A function stored in a value

A function stored in a value states its effects in its type, after the result as in a signature, and is pure when none are written.

```
struct Button {
  label: str
  on_press: () -> () ! console
}
```

`on_press` is a function that may use the console. A field of type `() -> ()` would be pure.

## The forms

| Written | Means |
| --- | --- |
| `fn f() { ... }` | `f` is pure |
| `fn f() ! console { ... }` | `f` may use the console |
| `fn f() -> T ! console, alloc { ... }` | The effects follow the return type, in any order, each once |
| `(A) -> B ! console` | A stored function that may use the console |
| `(A) -> B` | A stored function that is pure |

Next: [Declaring an effect](effect-declarations.md)
