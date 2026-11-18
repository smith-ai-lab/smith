---
type: specification
title: Traits
description: A trait as a set of functions over one or more types, an impl that supplies them for particular types, how a call finds the impl, and the defaults of a trait's parameters.
status: partial
built:
  - "`impl`, calls of trait functions with defaults"
---

# Traits

A trait is a set of functions over one or more types. An `impl` supplies those functions for particular types.

## A trait and an impl

`Named` is a trait with one function, which takes a `T` and gives a `str`. `T` is a [type parameter](type-parameters.md) of the trait. Each `impl` names the types it is for in brackets, and writes the functions.

```
struct Cat {
  name: str
}

struct Bird {
  name: str
}

trait Named[T] {
  fn name_of(x: T) -> str
}

impl Named[Cat] {
  fn name_of(x: Cat) -> str { x.name }
}

impl Named[Bird] {
  fn name_of(x: Bird) -> str { "bird" }
}
```

A trait function is called like any other function. The call finds the `impl` from the types of its arguments, so `name_of(Cat(name: "Tom"))` is `"Tom"`, and `name_of(Bird(name: "Tweety"))` is `"bird"`.

## When a call has no impl

A call of a trait function for types that have no `impl` is refused.

```
trait Named[T] {
  fn name_of(x: T) -> str
}

fn main() ! console {
  print(name_of(Cat(name: "Tom")))
}
```

The error is "there is no implementation of `Named` for `Cat`".

## What an impl must write

An `impl` writes every function of its trait. This one leaves out `shout`.

```
trait Named[T] {
  fn name_of(x: T) -> str
  fn shout(x: T) -> str
}

impl Named[Cat] {
  fn name_of(x: Cat) -> str { x.name }
}
```

It is refused: "this implementation of `Named` lacks `shout`".

A trait has at most one `impl` for a choice of types. A second `impl Named[Cat]` is refused: "`Named` is already implemented for `Cat`".

## When two traits share a name

Each trait is its own set of functions, so two traits may have a function of the same name. A call that could mean either is refused, and it is written with the name of the trait, as `Named.label(c)`.

```
trait Named[T] {
  fn label(x: T) -> str
}

trait Titled[T] {
  fn label(x: T) -> str
}

impl Named[Cat] {
  fn label(x: Cat) -> str { x.name }
}

impl Titled[Cat] {
  fn label(x: Cat) -> str { "Mr" }
}

fn main() ! console {
  c = Cat(name: "Tom")
  print(Named.label(c))
  print(Titled.label(c))
}
```

This prints `Tom` and then `Mr`. Written as `label(c)`, the call is refused: "`label` is a function of `Named` and `Titled`", and the fix is to write `Named.label` or `Titled.label`. A trait function whose name is not shared is written plain.

## An impl for a function

A function's name may be written where a type is expected. It names the type whose one value is that function. So a trait is implemented for a function as it is for any type, and that is how something is said about a function without a new kind of declaration.

```
fn double(x: f32) -> f32 { x + x }

trait Described[F] {
  fn describe() -> str
}

impl Described[double] {
  fn describe() -> str { "twice its argument" }
}
```

`Described[double]` is the `impl` for the function `double` and for no other. It is found as any `impl` is, and it lives where any `impl` may, as [Where an impl lives](impl-placement.md) says: beside the trait or beside the function. This is how a function is given its rule for [differentiation](differentiable.md).

The function is still called and passed as it always is. Only where a type is expected does its name mean this type.

## Defaults of a trait's functions

A trait may give a function's parameter a default, as [Functions](functions.md) shows for any function. A call may leave the argument out.

```
trait Named[T] {
  fn name_of(x: T, title: str = "Dr") -> str
}

impl Named[Cat] {
  fn name_of(x: Cat, title: str) -> str { title }
}
```

`name_of(c)` is `"Dr"`, and `name_of(c, title: "Mr")` is `"Mr"`. The trait states the default and the `impl` does not repeat it. An `impl` that writes `title: str = "Dr"` is refused: "the defaults of `name_of` are stated in `Named`, and an implementation does not repeat them", and the fix is to remove the default of `title`.

## What a trait costs

Calls are specialized at compile time, so a trait costs nothing at run time.

## The forms

| Written | Is |
| --- | --- |
| `trait T[A] { fn f(x: A) -> R }` | A set of functions over the type `A` |
| `impl T[Cat] { fn f(x: Cat) -> R { e } }` | The functions of `T` for `Cat` |
| `f(c)` | A call of a trait function, which finds the `impl` from the arguments |
| `T.f(c)` | The same call, written with the trait, when two traits have an `f` |
| `fn f(x: A, y: B = e) -> R` in a trait | A default that the `impl` does not repeat |

Next: [Bounds and `requires`](bounds.md)
