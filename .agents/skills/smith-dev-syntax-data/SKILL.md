---
name: smith-dev-syntax-data
description: "New syntax (#1428): `struct` and `enum` declarations with indented members, `enum Small(u8):`, payload enums, building values with braces (`Point{x = 1}`, `T{}`, `Pair(i32){...}`), variants (`.red`, `.circle(c)`), tuples, arrays, options. Load when declaring or building data in the new syntax."
---

# Structs, enums and building values

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Declaring

```smith
pub struct Point:
    pub x: f64                      // one field per line: no ; or ,
    pub y: f64

enum Color:
    red                             // enumerators are values, so lowercase
    green
    blue
enum Small(u8):                     // the base type in parentheses
    low = 2
    high = 9
pub enum Shape:                     // a payload enum; C's union is an error in a module
    circle: Circle
    rect: Rect
    empty
```

There are no field defaults (zero is the default), no designated initializers and no `(T){...}`.

## Building values

| Value | Written |
|---|---|
| struct | `Point{x = 1, y = 2}`: every field named, in any order; a field left out is zero |
| zero value | `Point{}`, `list.List(i32){}` |
| generic struct | `Pair(i32){first = 1, second = 2}`, or through an alias `IntPair{first = 1, second = 2}` |
| enum variant | `.red`, `.empty`: the leading dot means the enum the context expects |
| payload variant | `.circle(Circle{radius = 1})`, `.square(2)`: parentheses wrap the one payload value |
| tuple | `(1, 2.5)` |
| array or slice | `[1, 2, 3]` (a slice gets a hidden array behind it) |
| option | `.some(x)`, `.none`, or a plain value where `?T` is expected |

```smith
let p = Point{x = 1, y = 2}
let cfg = Config{                   // newlines inside braces are ignored
    host = "example.org",
    retry = true,
}
let color: Color = .green
let s: Shape = .circle(Circle{radius = 1})
```

Braces only ever build a struct; a call never does. `Point(x = 1)` is an error whose fix writes `Point{x = 1}`.

## Not settled yet

The zero value of an enum whose values skip 0 (`Small` above), of a pointer and of a `fn(...)`.
