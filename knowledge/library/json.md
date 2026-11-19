---
type: specification
title: JSON
description: std.json, a format for Serialize and Deserialize that writes a value as JSON text and reads JSON text into a value, the settings that say how a case of an enum and the name of a field look, and RawJson, a part of the text that is read later.
status: partial
built:
  - "`std.json`, which a program imports with `use std.json (to_json, to_json_as, Json)`, with `to_json` and `to_json_as` of any value that has `Serialize` and the `Json`, `Cases`, and `Names` of the settings, written in Smith from `Serialize` and `Writer` alone"
  - "every shape of the table below, a case of an enum as a key and as a tag, `Names.camel`, and the refusal of a tag that is also a member, with `Clash`"
  - "`from_json` and `from_json_as` of any type that has `Deserialize` and `Discard`, which read the text with a `Reader` written in Smith, with `Unknown` in the settings, the errors with their line, column, and path, `describe`, and `RawJson` with its `Deserialize`, and nothing is built between the text and the value"
  - "`RawJson` written with `to_json` as the bytes it holds, with `raw` of `Writer` and `Reader`, and under `value` where a case that is written with a tag holds it, `raw_json` and `raw_text`, and bytes that are not JSON, or not a text, written as a text"
  - "not a float, which waits for `f64`, not a `Map`, which is not built, and not `JsonValue`"
---

# JSON

`std.json` writes a value as JSON and reads JSON into a value. `to_json` takes any value that has [`Serialize`](serialize.md) and gives the text, and `from_json` takes a text and gives any value that has [`Deserialize`](deserialize.md).

```
use std.console (console, print)
use std.json (to_json)

struct Point {
  x: Int
  y: Int
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
  unknown: Unknown = .ignore
}

enum Cases {
  key
  tag: str
}

enum Names {
  written
  camel
}

enum Unknown {
  ignore
  refuse
}
```

`to_json(x)` is `to_json_as(x, Json(cases: .key, names: .written))`, and `from_json(text)` is `from_json_as(text, Json(cases: .key, names: .written))`. `unknown` is for reading, and says what is done with a member of the input that the type has no field of, as [Reading](#reading) says.

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
use std.json (Json, to_json_as)

mcp = Json(cases: .tag("type"), names: .camel)

fn reply(c: Content) -> Result[str, Clash] ! alloc { to_json_as(c, mcp) }
```

The settings hold for the whole value that is written or read. A type that needs another form writes its own `impl` of `Serialize`, as [Serialize](serialize.md#writing-it-yourself) shows, and no type carries a setting of its own.

## Reading

`from_json` takes a text and gives a `Result` of the type that the place of the result needs, as [Deserialize](deserialize.md) says. `from_json_as` takes the settings too.

```
use std.bits (u64)
use std.deserialize (Problem, ReadError)
use std.json (from_json)

struct Tool {
  name: str
  description: Option[str]
  timeout: u64 = 30
}

fn parse(text: str) -> Result[Tool, ReadError] ! alloc {
  from_json(text)
}
```

`parse("{\"name\":\"a\"}")` is `.ok` of a `Tool` with no description and a timeout of 30. The text may have a space, a tab, a line break, or a return between any two of its tokens, and nothing but those after the value.

```
fn from_json[T](text: str) -> Result[T, ReadError] ! alloc
fn from_json_as[T](text: str, json: Json) -> Result[T, ReadError] ! alloc
```

### What each shape is read from

| Shape | The text that is read |
| --- | --- |
| A boolean | `true` or `false` |
| A whole number | A number with no fraction and no exponent, as `-3` or `18446744073709551615`, and `-0` is `0` |
| A text | A string, with each escape JSON has undone, and a `\u` that is one half of a pair joined to the other |
| Nothing, `()` | `null` |
| A value that is missing | `null`, which is `.none`, and anything else is the value |
| A sequence | An array |
| A tuple | An array of exactly its size |
| A struct | An object, with a member for each field, in any order |
| A case of an enum | As [the settings](#settings) say |

**Nothing is converted.** `"5"` for a `u64`, `5.5` or `2e3` for a whole number, and `null` for anything that is not an `Option` or `()` are errors, and so is a number that the type cannot hold, as `300` for a `u8`. A `null` where an `Option` is read is `.none`, so `.some(.none)` is read back as `.none`, as it is written the same.

**A field the type has and the text has not** is `.none` for an `Option`, its default when it has one, and an error that names it in every other case. **A member that the type has no field of** is passed over when `unknown` is `.ignore`, which is the default, after being checked to be JSON, and is the error `unknown` when it is `.refuse`. **A member given twice** in one object is the error `twice`, whether or not the type has the field.

A text that has no escape is a piece of the input, and nothing is copied for it.

### A case of an enum

With `Cases.key`, `"empty"` is a case that holds nothing, and `{"square":4}` is a case that holds a value. With `Cases.tag`, the case is read from the member of that name, wherever it is in the object, and the other members are read in their order with the tag passed over.

```
mcp = Json(cases: .tag("type"), names: .camel)
```

With that setting `{"type":"circle","radius":2}` and `{"radius":2,"type":"circle"}` are `Shape.circle(Circle(radius: 2))`, `{"type":"square","value":4}` and `{"value":4,"type":"square"}` are `Shape.square(4)`, and `{"type":"empty"}` is `Shape.empty`. An object that has no tag is the error `missing` with the tag, at its `}`. The members besides the tag are the members of a struct that the case holds, with the rules above, or the one member `value` of any other value, and an object that holds a case that is not a struct has no other member. A name that is no case is the error `case`. When the tag is not the first member, the reader walks the bytes of the object once more to find it, and nothing is copied. Cases are read to any depth, and a case written with a tag asks for memory in proportion to the number of cases that are open round it.

With `Names.camel`, a member is the field whose written name is exactly the member's name. `inputSchema` is the field `input_schema`, and `input_schema` is a member the type has no field of, so it is passed over or refused as `unknown` says. A name that `to_json_as` would not write, as `first_Name` or `_x`, is no field either. The names of cases and the tag are read as they are.

### What was wrong

An error is a `ReadError`, as [Deserialize](deserialize.md#what-was-wrong) gives it. `line` and `column` say where, counted from 1, and the column counts bytes. `path` is the way to the value, as `tools[2].timeout`. Reading stops at the first error, so a read that succeeds works out none of them. `describe` makes the words.

```
fn describe(e: ReadError) -> str ! alloc
```

For a text of three tools whose third has a timeout that is a text, `describe` is `1:59: tools[2].timeout: expected a whole number, found a text`. A path that is empty is left out. Where each error is, and what it holds, is in the table.

| Error | The place | What it says |
| --- | --- | --- |
| `malformed` | The first byte that is not what JSON has there, or the end of the text, or the byte after the value | `not valid JSON` |
| `shape` | The start of the value that was found | `expected a whole number, found a text`, with the words `a boolean`, `a whole number`, `a text`, `a list`, `an object`, `a case`, `null`, `a number`, and `a number that is not whole` |
| `range` | The start of the number | `a number that does not fit u8`, and `u64` or `i64` for a number that is more than they hold, or a negative number for an unsigned type |
| `missing` | The `}` that ends the object | ``missing `y` ``, with the name of the field |
| `twice` | The start of the name of the second member | `` `x` is given twice `` |
| `unknown` | The start of the name | `` unknown `z` `` |
| `case` | The name of the case | `` `triangle` is not a case `` |

A tuple of two that is an array of three is `shape`, `the end of the list` wanted and `a value` found, at the third, and an array of one is `a value` wanted at the `]`. The path of a place that is a name, a value, or a byte that is no value is the way to the member it is in, and of a place after a value, or a `]` or `}`, the way to the array or object that holds it.

```
use std.deserialize (ReadError, Problem)
use std.json (from_json_as, Json, describe)

refuse = Json(cases: .key, names: .written, unknown: .refuse)
```

With `refuse`, `{"x":1,"y":3,"z":0}` read as a `Point` is ``1:15: z: unknown `z` ``.

## A part of the text that is read later

`RawJson` is a value of the text of one JSON value that is not taken apart. It always holds one whole JSON value, as a `str` always holds UTF-8, so its field is not `pub`, and it is made by reading, or by `raw_json`, which checks.

```
struct RawJson {
  text: str
}

fn raw_json(text: str) -> Result[RawJson, ReadError] ! alloc
fn raw_text(raw: RawJson) -> str
```

`raw_json("[1, 2] ")` is `.ok` of a `RawJson` that holds `[1, 2]`, as a piece of the text with the space round it left out, and shares its memory. A text that is not one whole JSON value is the error that reading it gives, so `raw_json(text)` is `.ok` exactly when `from_json(text)` of a `RawJson` is. `raw_text` gives the text.

It has `Deserialize`: read, it takes whatever value is next, checks that it is one whole JSON value, and holds exactly its bytes, with no space round it, as a piece of the input that shares its memory. `from_json(raw_text(raw))` reads it later into a type.

```
struct Call {
  name: str
  arguments: RawJson
}
```

`{"name":"f","arguments":{"x": [1.5e3, "y"]}}` read as a `Call` has `{"x": [1.5e3, "y"]}` for `raw_text(call.arguments)`, with the space in it as it was.

It has `Serialize` too, which writes its bytes where the value goes, as they are, with each space in them and every digit of each number: `to_json` of that `Call` is `{"name":"f","arguments":{"x": [1.5e3, "y"]}}`, and `1.50`, `1e400`, and a number of 40 digits are written as they were read. No program builds a `RawJson` from text, as its field is private, so `to_json` writes what it holds as it is and checks nothing. The bytes of another format are written as a text. Bytes that are not UTF-8 are written as a text with each byte that begins no character as U+FFFD. A `RawJson` that a case holds is a value that is not a struct, so with `Cases.tag` it is written under `value`, and `.tag("value")` clashes with it as it does with any such value. Both use `raw` with `"json"`, which [Serialize](serialize.md#the-shapes) and [Deserialize](deserialize.md#the-shapes) say. Written by another format, a `RawJson` is a text, and read from another format, it is the bytes of a text, so what a format writes it reads back.

## Memory

Reading asks for memory for the texts that hold an escape, for the lists of the value, and for the objects and arrays that are passed over or held by a `RawJson`, in proportion to each, and for nothing else. A value of numbers, `bool`, and `Option`, with tuples, structs, and cases that hold them, asks for none.

## The forms

| Written | Is |
| --- | --- |
| `to_json(x)` | The JSON of `x`, with a case as a key and names as written |
| `to_json_as(x, Json(cases: .tag("type"), names: .camel))` | `.ok` of the JSON of `x`, with the case under `type` and names in camel case, or `.err` of the first `Clash` |
| `from_json(text)` | `.ok` of the value of the type the place needs, with a case as a key and names as written, or `.err` of the first `ReadError` |
| `from_json_as(text, Json(cases: .tag("type"), names: .camel, unknown: .refuse))` | The same, with the case under `type`, names in camel case, and a member the type has no field of refused |
| `describe(e)` | The words of an error, as `1:59: tools[2].timeout: expected a whole number, found a text` |
| `RawJson` | One JSON value as its text, which `from_json` reads later and `to_json` writes as it is |
| `raw_json(text)` | `.ok` of the `RawJson` of one whole JSON value, or the error that reading it gives |
