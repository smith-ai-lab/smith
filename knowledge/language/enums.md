---
type: specification
title: Enums
description: Enums that hold exactly one of their cases, each case holding a type or nothing, and how a case is built.
status: partial
built: "enums whose cases hold a type or nothing, with type parameters, and cases built with a leading dot or in full"
---

# Enums

An enum is a value that is exactly one of its cases. Each case is a name, and holds either nothing or one value of a type.

```
enum Light {
  red
  yellow
  green
}
```

A `Light` is a `red`, a `yellow`, or a `green`, and these cases hold nothing. A case that holds something names its type, written `name: Type` as a [struct](structs.md)'s field is.

```
struct Circle {
  radius: i32
}

struct Rect {
  w: i32
  h: i32
}

enum Shape {
  circle: Circle
  rect: Rect
  empty
}
```

A `Shape` is a `circle` that holds a `Circle`, a `rect` that holds a `Rect`, or an `empty` that holds nothing. The cases of an enum are written one to a line, and `enum Shape { rect: Rect, empty }` is refused: "expected a line break or `}`, found `,`".

## Building a case

A case is built with a leading dot, followed by what it holds in parentheses, or by nothing when it holds nothing. The dot form works wherever the expected type is known: a function's result, a parameter, or a binding that states its type.

```
fn unit_square() -> Shape { .rect(Rect(w: 1, h: 1)) }

fn make(r: Rect) -> Shape { .rect(r) }
```

Where the expected type is not known, the case is written in full, with the name of the enum before the dot. This form is allowed anywhere.

```
fn main() ! console, alloc {
  s = Shape.rect(Rect(w: 7, h: 1))
  e = Shape.empty
}
```

The dot form where the type is not known is refused.

```
fn main() ! console {
  s = .empty
}
```

This gives "the enum of `.empty` is not known here", with the fix "write the enum's name before the dot, or give the binding a type". So `s: Shape = .empty` is the other way to write it.

Where the type is known, `.rect(r)` is the form that is written. `Shape.rect(r)` is accepted there and means the same.

A case is built from what it holds, and from nothing else.

```
s = Shape.round(3)
```

This gives "`Shape` has no case named `round`".

```
s = Shape.rect("x")
```

This gives "this gives text, where a `Rect` is needed".

```
s = Shape.empty(1)
```

This gives "`.empty` holds nothing, so it takes no value".

```
s = Shape.rect
```

This gives "`.rect` holds a `Rect`, and none is given".

## What a case holds

A case holds a whole value of its type, so the `Rect` in a `.rect` is a `Rect` like any other, and [`match`](match.md) is how it is taken out. An enum is not a struct: it has no fields, and `s.rect` is refused with "the field `rect` is read from a `Shape`, which is not a struct".

## Type parameters

An enum may take type parameters in brackets after its name, and a case may hold one. `Option[T]` is such an enum, and is always in scope: its cases are `some`, which holds a `T`, and `none`.

```
fn lone(n: u8) -> Option[u8] { .some(n) }

fn nothing() -> Option[u8] { .none }
```

[Type parameters](type-parameters.md) says how a program writes its own.

## The forms

| Written | Is |
| --- | --- |
| `enum E { a }` | An enum with a case `a` that holds nothing |
| `enum E { a: T }` | A case `a` that holds a `T` |
| `.a(e)` | The case `a` holding `e`, where the expected type is known |
| `.a` | A case that holds nothing, where the expected type is known |
| `E.a(e)` and `E.a` | The same, written in full, which is written where the expected type is not known, and accepted anywhere |

Next: [Match](match.md)
