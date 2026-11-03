---
name: smith-dev-syntax-data
description: "New syntax (#1428): `struct` and `enum` declarations, payload enums, enums with a base type, building values (`Point(x = 1)`, `.circle(...)`, `(1, 2.5)`, `[1, 2, 3]`), zero values (`mut xs: T`, `T()`), private fields and `pub` fields, options. Load when declaring or building data in the new syntax."
---

# Structs, enums and building values

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Declaring

```smith
pub struct Point {
    pub x: f64                   // one field per line: no ; or ,
    pub y: f64
}

enum Color {
    red                          // enumerators are values, so lowercase
    green
    blue
}

enum Small: u8 {
    low = 2
    high = 9
}

pub enum Shape {                 // a payload enum; C's union is an error in a module
    circle: Circle
    rect: Rect
    empty
}
```

There are no field defaults (zero is the default), no designated initializers and no `(T){...}`.

## Building values

| Value | Written |
|---|---|
| struct | `Point(x = 1, y = 2)`: a call with every field named, in any order; a field left out is zero |
| generic struct | `Pair(i32)(first = 1, second = 2)`, or through an alias `IntPair(first = 1, second = 2)` |
| enum variant | `.red`, `.empty`, `.circle(Circle(radius = 1))`: the leading dot means the enum the context expects, and that is its only use |
| tuple | `(1, 2.5)` |
| array or slice | `[1, 2, 3]` (a slice gets a hidden array behind it) |
| option | `.some(x)`, `.none`, or a plain value where `?T` is expected |

```smith
p := Point(x = 1, y = 2)
color: Color = .green
s: Shape = .circle(Circle(radius = 1))
best: ?i32 = 7                   // a plain value where ?T is expected
none: ?i32 = .none
```

## Not settled yet

A type function that returns an enum (how `Option(T)` and `Result(T, E)` are written in the std library); the zero value of an enum whose values skip 0 (`Small` above), of a pointer and of a `fn(...)`; bitfields and `alignas`.
