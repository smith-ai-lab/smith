---
type: specification
title: Reading a value from a text
description: FromText, by which a value is read from one text, as a part of a path, a part of a query, or a blank of a prompt is, given to the whole numbers of every range, bool, and str, and from_text, which reads a value and says what is wrong.
status: partial
built:
  - "`std.fromtext`, with `FromText`, which a whole number of any range, `bool`, and `str` is given and which a type's own `impl` replaces, and `ReadText` with `from_text`, which reads a value of any type that has `FromText` and gives a `ReadError` that names the parameter"
  - "not a float, which waits for `f64`"
---

# Reading a value from a text

A part of a path, a part of a query, and a blank of a prompt each arrive as one text, and are read as the type of the parameter that takes them. `FromText` is the trait of a type that has a reading from one text.

```
use std.deserialize (ReadError)
use std.fromtext (from_text)

fn id_of(part: str) -> Result[u64, ReadError] {
  from_text("id", part)
}
```

`id_of("7")` is `.ok(7)`, and `id_of("x")` is `.err` of a `ReadError` whose `path` is `id`.

## The trait

```
trait FromText[T] {
  fn parse(text: str) -> Option[T]
  fn expects() -> str
}
```

`parse` gives `.some` of the value the text spells, and `.none` for any other text. `expects` says in words what `parse` reads, which `from_text` puts in the error.

## What is given

`FromText` is given to these types, and to no other:

| Type | The text that is read |
| --- | --- |
| A whole number of any range | Its decimal digits, with a `-` before them where the range has numbers below 0, and nothing else: `"7"` is 7, and `""`, `"-"`, `"+7"`, `" 7"`, `"7 "`, and `"7x"` are not read. A number outside the range is not read |
| `bool` | `true` or `false` |
| `str` | Any text, as it is |

A struct, an enum, a list, a tuple, a float, and every other type has none until its program writes an `impl` of `FromText` for it, which takes the place of nothing and is used where the type is read.

```
enum Level {
  low
  high
}

impl FromText[Level] {
  fn parse(text: str) -> Option[Level] {
    if text == "low" { Option.some(Level.low) } else if text == "high" { Option.some(Level.high) } else { Option.none }
  }
  fn expects() -> str { "low or high" }
}
```

A parameter of a type with no `FromText` that is read from a text is refused when the program is built, as a function that needs a trait its type has no implementation of is.

## Reading and the error

`from_text(name, text)` is the value of the type the place needs, or a [`ReadError`](deserialize.md) whose `path` is `name`, whose `line` and `column` are 0, and whose problem is `range` with what the type `expects` when the text is whole number digits that the type does not take, and `shape` with what it `expects` and `"a text"` for any other text.

## The forms

| Written | Is |
| --- | --- |
| `from_text("id", part)` | `.ok` of the value of the type the place needs read from `part`, or `.err` of a `ReadError` at `id` |
| `impl FromText[T]` | The reading of a type of the program's own |
