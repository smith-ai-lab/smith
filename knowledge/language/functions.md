---
type: specification
title: Functions
description: Typed parameters, a return type, and a block whose last expression is its value.
---

# Functions

A function has typed parameters, a return type, and a block for a body. The last expression of a block is its value. A comment starts with `//` on its own line.

```
// The area of a rectangle.
fn area(w: f32, h: f32) -> f32 { w * h }

fn double(x: f32) -> f32 { x * 2.0 }
```

A call is `area(2.0, 3.0)`, and arguments may be named, as in `area(w: 2.0, h: 3.0)`.

Next: [Bindings](bindings.md)
