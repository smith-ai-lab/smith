---
type: specification
title: Enums and match
description: Enums that hold one of their cases, and match to take a value apart.
---

# Enums and `match`

An enum holds exactly one of its cases. Each case names a type, written `name: Type` like a struct field, or is bare. `match` takes a value apart, with a block per case. A case is always written with a leading dot, in a pattern and in an expression alike, and a bare name in a pattern always introduces a new variable.

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

A case is built with a leading dot, as in `.rect(...)` or `.empty`, wherever the expected type is known. Otherwise it is written in full, as in `Shape.rect(...)`. Because a case names a type, the `Rect` inside a `Shape` can be passed on its own after a match.

Next: [Generics and traits](traits.md)
