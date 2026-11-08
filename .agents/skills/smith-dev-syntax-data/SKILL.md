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
| enum variant | `.red`, `.empty`: the leading dot means the enum the context expects; a plain enum (no payload) is ordered with `<`, `<=`, `>`, `>=` in declaration order (owner, 2026-11-05: "Yeah, I think we should be able to do what you're saying"), and a leading dot in a comparison takes its type from the other operand (`kind >= .kw_alignas`); a payload enum is not ordered; the variants of a `pub enum` are public with it |
| payload variant | `.circle(Circle(radius = 1))`, `.square(2)`: parentheses wrap the one payload value |
| tuple | `(1, 2.5)`, `()` empty |
| named tuple | `(count = n, ms = ms)`: its type is the anonymous struct `(count: i32, ms: f64)` |
| array or slice | `[1, 2, 3]` (a slice gets a hidden array behind it), `[a..., b]` splices a compile-time pack or a tuple into the literal, as `...` does in a call (owner, 2026-11-07: "I feel like we should support spread in array literals"), `[]` empty (owner, 2026-11-05: "empty array literals should be `[]`"; it takes its type from context: a field, a parameter, a declared type, as in `Attribute(name = n, args = [])`; a bare `xs := []` is an error for now, write `xs: []i32 = []`, and inferring it from later uses is planned), `array.filled(u32, 256, 0)` |
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

- Every field is given, unless the struct declares a default for it. There is no zero value: `T()` builds only a struct whose fields all have defaults. Ported C writes C's zero as the default of each field a construction leaves out (`count: usize = 0`, `name: str = ""`, `parent: ?*Work = .none`, #2530, [smith-dev-syntax-privacy](../smith-dev-syntax-privacy/SKILL.md)). A struct with private fields is built outside its module by a function of that module ([smith-dev-syntax-privacy](../smith-dev-syntax-privacy/SKILL.md)).
- D23 (owner, 2026-11-08: "Whenever you have that, I think typically what you actually have is an enum that's pretending to be a struct."; "If you really just want a sentinel for, like, okay, I don't have any value here, you want to use none, like you said. And otherwise, you want to use the enum."; "Do not set things to undefined."): a field a construction leaves out is never zero-filled and never `= undefined`; a field whose null or zero means "no value" is `?T = .none`; a struct whose constructions each fill a different subset of fields is a payload enum with one variant per state, each carrying only its own fields.
- The parser reads `Point(1, 2)` as a call; the checker sees that `Point` is a type. Braces are gone: `Point{x = 1}` is an error whose fix writes `Point(x = 1)`.
- An array is filled with `array.filled(T, n, value)` (`import std.array`) or left `= undefined`.
