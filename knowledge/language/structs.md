---
type: specification
title: Structs and tuples
description: Structs with named fields and defaults, tuples, and patterns that take them apart.
---

# Structs and tuples

A struct holds named fields, one per line. Construction is a call with named fields, and `.` reads a field. A tuple groups values without names and is taken apart by a pattern, in which `var` can mark a single binding. Its elements are evaluated from left to right. The empty tuple `()` is what a function with no return type returns. A definition may also sit at the top level of a file, as `unit` does below.

```
struct Rect {
  w: f32
  h: f32
}

fn rect_area(r: Rect) -> f32 { r.w * r.h }

fn sides(r: Rect) -> (f32, f32) { (r.w, r.h) }

fn perimeter(r: Rect) -> f32 {
  (a, var b) = sides(r)
  b = b * 2.0
  a * 2.0 + b
}

unit = Rect(w: 1.0, h: 1.0)
```

A field may have a default, written as a parameter's is and under the same rules. Construction may then leave the field out.

```
struct Border {
  width: f32 = 1.0
  inset: f32 = 0.0
}

thin = Border()
wide = Border(width: 4.0)
```

Next: [Enums and `match`](enums.md)
