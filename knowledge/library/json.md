---
type: specification
title: JSON
description: std.json, a format for Serialize that writes a value as JSON text, and the settings that say how a case of an enum and the name of a field look.
status: planned
---

# JSON

`std.json` writes a value as JSON. `to_json` takes any value that has [`Serialize`](serialize.md) and gives the text.

```
use std.console (console, print)
use std.json (to_json)

struct Point {
  x: i64
  y: i64
}

fn main() ! console, alloc {
  print(to_json(Point(x: 3, y: 4)))
}
```

This prints `{"x":3,"y":4}`. The text has no spaces and no line breaks. Nothing of `std.json` is built, so no words are quoted for what the compiler says today.

## What each shape becomes

| Shape | JSON |
| --- | --- |
| A boolean | `true` or `false` |
| A whole number | The number in decimal, as `-3` or `18446744073709551615` |
| A text | A string, with `"`, `\`, and the bytes below a space written as escapes |
| Nothing, `()` | `null` |
| A value that is missing | `null` |
| A value that is there | The value |
| A sequence | An array |
| A tuple | An array |
| A struct | An object, with a member for each field, in the order of the fields |
| A case of an enum | As [the settings](#settings) say |

An `Option` in an `Option` writes `null` for `.none` and for `.some(.none)`, as JSON has one `null`.

## A case of an enum

A case that holds nothing is its name as a string. A case that holds a value is an object with one member, named for the case.

```
enum Shape {
  circle: Circle
  square: u64
  empty
}
```

`to_json(Shape.empty)` is `"empty"`, `to_json(Shape.square(4))` is `{"square":4}`, and a `circle` of radius 2 is `{"circle":{"radius":2}}`. Every value is read back from what was written, whatever a case holds.

## Settings

JSON that another program defines has its own way to write a case and to name a field. The way is said once, in a `Json` value, and `to_json_as` writes with it.

```
struct Json {
  cases: Cases
  names: Names
}

enum Cases {
  key
  tag: str
}

enum Names {
  written
  camel
}
```

`to_json(x)` is `to_json_as(x, Json(cases: .key, names: .written))`.

**`Cases.tag`** writes the case's name as a member of the object, under the name given. With `.tag("type")`:

| The case holds | JSON |
| --- | --- |
| Nothing | `{"type":"empty"}` |
| A struct | The struct's members after the tag, `{"type":"circle","radius":2}` |
| Anything else | The value under `value`, `{"type":"square","value":4}` |

**`Names.camel`** writes a field's name with each `_` taken out and the letter after it in upper case, so `input_schema` is `inputSchema`. The names of cases are written as they are.

```
mcp = Json(cases: .tag("type"), names: .camel)

fn reply(c: Content) -> str ! alloc { to_json_as(c, mcp) }
```

The settings hold for the whole value that is written. A type that needs another form writes its own `impl` of `Serialize`, as [Serialize](serialize.md#writing-it-yourself) shows, and no type carries a setting of its own.

## The forms

| Written | Is |
| --- | --- |
| `to_json(x)` | The JSON of `x`, with a case as a key and names as written |
| `to_json_as(x, Json(cases: .tag("type"), names: .camel))` | The JSON of `x`, with the case under `type` and names in camel case |
