---
type: specification
title: Random values for tests
description: The Random trait, which gives a value of a type that is good at finding mistakes, who has it without writing it, and how to take it away or replace it.
status: planned
---

# Random values for tests

`Random` is a trait with one function, `random`, which turns a key into a value of the type.

```
trait Random[T] {
  fn random(key: take Key) -> T
}
```

A `Key` is used once, as [Lending and take](take.md) describes, so `random` takes it.

```
n: i32 = random(key)
```

`random(key)` is an `i32` when `n` says so. Every number type, `bool`, and `str` has `Random`.

## A type is given it

A struct, an enum, or a tuple is given `Random` automatically, as [`Copy`](ownership.md) is, unless one of its parts lacks it. So a type a program defines has it with nothing more written.

```
struct Point {
  x: i32
  y: i32
}

p: Point = random(key)
```

`Point` has `Random` since `i32` has it. A `Point` that held a part with no `Random` would have none.

## Taking it away

`without Random for T` takes it away, as [without](without.md) describes.

```
without Random for Point
```

After this a `Point` has no `Random`, so a test cannot take one as a parameter.

## Replacing it

A type's own `impl` replaces the one it was given, which is how a test gets values of a particular shape.

```
struct Pair {
  a: i32
  b: i32
}

impl Random[Pair] {
  fn random(key: take Key) -> Pair {
    n: i32 = random(key)
    Pair(a: n, b: n)
  }
}
```

Every `Pair` that `random` gives has two equal parts.

## Random and Sample

`Random[T]` gives a value that is good at finding mistakes, and `Sample` draws from exactly the distribution it is given, as [Random](../library/random.md) describes.

## The forms

| Written | Means |
| --- | --- |
| `random(key)` | A value of the type the context asks for |
| A number type, `bool`, or `str` | Has `Random` |
| A struct, enum, or tuple | Has `Random` when every part does, with nothing written |
| `without Random for T` | `T` has no `Random` |
| `impl Random[T] { ... }` | Replaces the `Random` that `T` was given |

Next: [Properties](properties.md)
