---
type: specification
title: JSON
description: std.json, a format for Serialize that writes a value as JSON text, and the settings that say how a case of an enum and the name of a field look.
status: partial
built:
  - "`std.json`, which a program imports with `use std.json (to_json, to_json_as, Json)`, with `to_json` and `to_json_as` of any value that has `Serialize` and the `Json`, `Cases`, and `Names` of the settings, written in Smith from `Serialize` and `Writer` alone"
  - "every shape of the table below, a case of an enum as a key and as a tag, `Names.camel`, and the refusal of a tag that is also a member, with `Clash`"
  - "not a float, which waits for `f64`, and not a `Map`, which is not built"
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

This prints `{"x":3,"y":4}`. The text has no spaces and no line breaks.

## What each shape becomes

| Shape | JSON |
| --- | --- |
| A boolean | `true` or `false` |
| A whole number | The number in decimal, as `-3` or `18446744073709551615` |
| A text | A string, as [A text](#a-text) says |
| Nothing, `()` | `null` |
| A value that is missing | `null` |
| A value that is there | The value |
| A sequence | An array |
| A tuple | An array |
| A struct | An object, with a member for each field, in the order of the fields |
| A case of an enum | As [the settings](#settings) say |

An `Option` in an `Option` writes `null` for `.none` and for `.some(.none)`, as JSON has one `null`.

## A text

A text is written between quotes. `"` is `\"` and `\` is `\\`. A line break, a tab, a return, a backspace, and a form feed are `\n`, `\t`, `\r`, `\b`, and `\f`, and each other byte below a space is `\u00` and two lower case hexadecimal digits. Every other byte is written as it is, so a character beyond ASCII keeps its bytes.

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

**A tag that is also a member is refused.** An object has each name once, so the tag's name may not be the name of another member of the same object. `to_json_as` gives a `Result`, and the error says which enum, which case, and which name.

```
struct Clash {
  of: str
  case: str
  member: str
}

fn to_json_as[T: Serialize](value: T, json: Json) -> Result[str, Clash] ! alloc
```

```
struct Circle {
  type: str
  radius: u64
}

to_json_as(Shape.circle(Circle(type: "round", radius: 2)), Json(cases: .tag("type"), names: .written))
```

This is `.err(Clash(of: "Shape", case: "circle", member: "type"))`: the struct's field `type` would be written beside the tag `type`. The name compared is the one that is written, so with `Names.camel` a field `my_tag` clashes with `.tag("myTag")`. A case that holds anything but a struct is written under `value`, so `.tag("value")` clashes with every such case. The first clash met ends the writing, and no text is given. `to_json` writes no tag, has no clash, and gives a `str`.

**`Names.camel`** writes a field's name with each `_` taken out and the letter after it in upper case, so `input_schema` is `inputSchema`. A `_` stays where the byte before it is `_` or the byte after it is not a lower case letter, so `x_`, `a__b`, and `a_1` are written as they are and `_x` is `X`. No two names of lower case letters, digits, and `_` are written alike. The names of cases are written as they are.

```
mcp = Json(cases: .tag("type"), names: .camel)

fn reply(c: Content) -> Result[str, Clash] ! alloc { to_json_as(c, mcp) }
```

The settings hold for the whole value that is written. A type that needs another form writes its own `impl` of `Serialize`, as [Serialize](serialize.md#writing-it-yourself) shows, and no type carries a setting of its own.

## The forms

| Written | Is |
| --- | --- |
| `to_json(x)` | The JSON of `x`, with a case as a key and names as written |
| `to_json_as(x, Json(cases: .tag("type"), names: .camel))` | `.ok` of the JSON of `x`, with the case under `type` and names in camel case, or `.err` of the first `Clash` |
