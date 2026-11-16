---
type: specification
title: Structs and tuples
description: Structs with named fields and defaults, assigning to a field of a var, tuples, and patterns that take them apart.
status: partial
built: "structs with fields and defaults, assigning to a field of a `var`, tuples, and patterns that define and assign"
---

# Structs and tuples

A struct holds named fields, one per line. Construction is a call with named fields, and `.` reads a field. A tuple groups values without names and is taken apart by a pattern, in which `var` can mark a single binding. A pattern may define some names and assign to others: in `(total, rest) = split(total)`, where `total` is a `var`, `total` is assigned and `rest` is defined. Its elements are evaluated from left to right. The empty tuple `()` is what a function with no return type returns. A definition may also sit at the top level of a file, as `unit` does below.

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

A field of a `var` can be assigned. Nothing else can name that value, so the change is seen through that `var` alone: it now holds a copy of the struct with the field replaced.

```
fn widen(r: Rect, by: f32) -> Rect {
  var wide = r
  wide.w += by
  wide
}
```

`widen` leaves `r` as it was. Assigning to a field of a name that is not a `var`, such as `r.w`, is a compile error.

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
