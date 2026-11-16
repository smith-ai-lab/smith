---
type: specification
title: Enums and match
description: Enums that hold one of their cases, and match to take a value apart.
---

# Enums and `match`

An enum holds exactly one of its cases. Each case names a type, written `name: Type` like a struct field, or is bare. `match` takes a value apart, with a pattern and a block for each way it can be. In a pattern a case is always written with a leading dot, and a bare name always introduces a new variable for what it matches.

```
struct Circle {
  radius: f32
}

enum Shape {
  circle: Circle
  rect: Rect
  empty
}

fn shape_area(s: Shape) -> f32 {
  match s {
    .circle(c) { 3.14159 * c.radius * c.radius }
    .rect(r) { rect_area(r) }
    .empty { 0.0 }
  }
}

fn unit_square() -> Shape { .rect(Rect(w: 1.0, h: 1.0)) }
```

A case is built with a leading dot, as in `.rect(...)` or `.empty`, wherever the expected type is known. Otherwise it is written in full, as in `Shape.rect(...)`, which is allowed anywhere. Because a case names a type, the `Rect` inside a `Shape` can be passed on its own after a match.

- A case's pattern may hold another pattern, for what the case holds.
- The patterns of a `match` cover each case once: each case is written, or a bare name after them covers the rest. A case left out is an error, and so is a pattern that the ones before it already cover.
- A `match` whose value is used is an expression, and its blocks give the same type. Used as a statement, nothing its blocks give is read.

Next: [Generics and traits](traits.md)
