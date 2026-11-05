---
name: smith-dev-syntax-data
description: "New syntax (#1428): `struct` and `enum` declarations with indented members, field defaults, `enum Small(u8):`, payload enums, building values with parentheses (`Point(1, 2)`, `Point(x = 1, y = 2)`, `Pair(i32)(1, 2)`), variants (`.red`, `.circle(c)`), tuples, named tuples `(count = n, ms = ms)`, arrays and `array.filled`, options. Load when declaring or building data in the new syntax."
---

# Structs, enums and building values

Part of the tour [smith-dev-syntax](../smith-dev-syntax/SKILL.md); the spec is #1428.

## Declaring

```smith
pub struct Point:
    pub x: f64                      # one field per line: no ; or ,
    pub y: f64
pub struct Options:
    pub retry: bool = false         # a default: a compile-time constant

enum Color:
    red                             # enumerators are values, so lowercase
    green
    blue
enum Small(u8):                     # the base type in parentheses
    low = 2
    high = 9
pub enum Shape:                     # a payload enum; C's union is an error in a module
    circle: Circle
    rect: Rect
    empty
```

## Building values

| Value | Written |
|---|---|
| struct | `Point(1, 2)`, `Point(x = 1, y = 2)`, `Point(1, y = 2)`: Python's rules, positional (in field order) then named |
| generic struct | `Pair(i32)(1, 2)`, or through an alias `IntPair(first = 1, second = 2)` |
| enum variant | `.red`, `.empty`: the leading dot means the enum the context expects |
| payload variant | `.circle(Circle(radius = 1))`, `.square(2)`: parentheses wrap the one payload value |
| tuple | `(1, 2.5)`, `()` empty |
| named tuple | `(count = n, ms = ms)`: its type is the anonymous struct `(count: i32, ms: f64)` |
| array or slice | `[1, 2, 3]` (a slice gets a hidden array behind it), `array.filled(u32, 256, 0)` |
| option | `.some(x)`, `.none`, or a plain value where `?T` is expected |

```smith
p := Point(1, 2)
cfg := Config(                      # newlines inside parentheses are ignored
    host = "example.org",
    retry = true,
)
o := Options()                      # every field has a default
color: Color = .green
r := (count = 3, ms = 1.5)
ms := r.ms                          # fields(typeof(r)) reads it too
```

- Every field is given, unless the struct declares a default for it. There is no zero value: `T()` builds only a struct whose fields all have defaults. A struct with private fields is built outside its module by a function of that module ([smith-dev-syntax-privacy](../smith-dev-syntax-privacy/SKILL.md)).
- The parser reads `Point(1, 2)` as a call; the checker sees that `Point` is a type. Braces are gone: `Point{x = 1}` is an error whose fix writes `Point(x = 1)`.
- An array is filled with `array.filled(T, n, value)` (`import std.array`) or left `= undefined`.
