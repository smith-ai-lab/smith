---
type: specification
title: Deserialize
description: The Deserialize trait, by which a value is built from a reader that knows a format, given automatically to a type whose parts have it, and what is done with a field that is missing.
status: partial
built:
  - "`std.deserialize`, with `Deserialize`, `Reader`, `ReadError`, and `Problem`, which a program imports with `use` and which the prelude does not hold"
  - "`Deserialize`, which a whole number, `bool`, `str`, `()`, and every struct, enum, tuple, list, or `Option` is given unless a part lacks it, which `without Deserialize for T` takes away, and which a type's own `impl` replaces, built part by part from a `Reader` with nothing built between"
  - "a field left out is `.none` for an `Option`, its default when it has one, and otherwise the reader's `fail` with `missing` and the field's name; a field given twice is `twice`; a number outside its type is `range` with the type's name; a name that is no case is `case`"
  - "`raw` of a `Reader`, which gives the bytes of the next value as a piece of the input, and `std.json`, which has a `Reader` written in Smith and reads with it"
  - "not a float, `Slice`, an array, or `Map`, which wait for their shapes"
---

# Deserialize

`Deserialize` is the trait by which a value is built from a reader. The reader knows a format, such as JSON, and the type knows none. It is [`Serialize`](serialize.md) the other way round.

```
use std.deserialize (ReadError)
use std.json (from_json)

struct Point {
  x: Int
  y: Int
}

fn parse(text: str) -> Result[Point, ReadError] ! alloc {
  from_json(text)
}
```

`parse("{\"x\":3,\"y\":4}")` is `.ok(Point(x: 3, y: 4))`. `Point` has `Deserialize` without a line written for it, the type that is read is the one the place of the result needs, and `from_json` is a function of `std.json`. `Deserialize`, `Reader`, `ReadError`, and `Problem` are in `std.deserialize`, which a program imports with `use std.deserialize (Deserialize, Reader, ReadError, Problem, read)`, and the prelude holds none of them. The words of `from_json` are in [JSON](json.md#reading).

## The trait

`Deserialize` has one function, which takes a reader and gives it back with the value, or with what was wrong.

```
trait Deserialize[T] {
  fn read[R: Reader](from: take R) -> (R, Result[T, ReadError]) ! alloc
}
```

The reader is [taken](../language/take.md) and given back, as a writer is. A value is built straight from what is read, and nothing is built in between.

## The shapes

A reader gives the shapes a [writer](serialize.md#the-shapes) takes, one at a time, as the type asks for them. A format is a type that has `Reader`.

```
trait Reader[R] {
  fn boolean(r: take R) -> (R, Result[bool, ReadError]) ! alloc
  fn signed(r: take R) -> (R, Result[Int, ReadError]) ! alloc
  fn unsigned(r: take R) -> (R, Result[u64, ReadError]) ! alloc
  fn text(r: take R) -> (R, Result[str, ReadError]) ! alloc
  fn nothing(r: take R) -> (R, Result[(), ReadError]) ! alloc
  fn present(r: take R) -> (R, Result[bool, ReadError]) ! alloc
  fn sequence(r: take R) -> (R, Result[(), ReadError]) ! alloc
  fn more(r: take R) -> (R, Result[bool, ReadError]) ! alloc
  fn tuple(r: take R, size: Nat) -> (R, Result[(), ReadError]) ! alloc
  fn end_tuple(r: take R) -> (R, Result[(), ReadError]) ! alloc
  fn structure(r: take R, name: str) -> (R, Result[(), ReadError]) ! alloc
  fn field(r: take R) -> (R, Result[Option[str], ReadError]) ! alloc
  fn unknown(r: take R, name: str) -> (R, Result[(), ReadError]) ! alloc
  fn case(r: take R, of: str) -> (R, Result[str, ReadError]) ! alloc
  fn end_case(r: take R) -> (R, Result[(), ReadError]) ! alloc
  fn raw(r: take R, of: str) -> (R, Result[Slice[Byte], ReadError]) ! alloc
  fn fail(r: take R, problem: Problem) -> (R, ReadError) ! alloc
}
```

- `present` says whether a value that may be missing is there. When it is, the value is read next.
- `sequence` opens a sequence, and `more` says before each element whether there is another. The sequence is closed when `more` gives `false`.
- `structure` opens a struct. `field` then gives the name of the field the input holds next, written as the type writes its fields, and the type reads the value of the field of that name. It gives `.none` when the struct has ended. For a name the type has no field of, the type calls `unknown`, and the reader passes over the value or refuses it, as the format says.
- `case` gives the name of the case the input holds. What that case holds is read next, and `end_case` closes it, for a case that holds nothing too.
- `raw` gives the bytes of the next value, in the format `of` names, as a piece of the input that shares its memory, and moves past the value. A reader of another format reads a text and gives its bytes. It gives bytes and not a text, so that a format that is not text has it too. A type that is a part of the input that is read later reads itself with it, as [`RawJson`](json.md#a-part-of-the-text-that-is-read-later) does, with `"json"`, so that what a format writes with [`raw`](serialize.md#the-shapes) is read back as the same bytes.
- `fail` makes the error for what the type itself finds wrong, as a field that is missing, and the reader puts the path and the place in it.

A reader gives names and is given none, so a type holds no table of its fields: it compares the name it is given with each of its own.

## Given automatically

A struct, an enum, a tuple, a list, and an `Option` have `Deserialize` when each of their parts has it, as they have `Serialize`. The numbers, `bool`, `str`, and `()` have it. [`without`](../language/without.md) takes it away, and a type's own `impl` replaces it.

A number that the type cannot hold is an error: a `u8` is read with `unsigned`, and 300 is refused. Nothing is turned into another shape to make it fit.

## A field that is missing

A struct is built when every field has a value. A field the input did not hold gets one in two cases, and is an error in every other.

```
struct Tool {
  name: str
  description: Option[str]
  timeout: u64 = 30
}
```

- A field of type `Option` is `.none`.
- A field with a [default](../language/structs.md) is its default.
- Any other field that is missing is an error that names it.

So an input that holds only `name` gives a `Tool` with no description and a timeout of 30, and an input with no `name` is refused. A field the input holds twice is an error.

## What was wrong

A `ReadError` says where in the value the trouble is and what it is.

```
pub struct ReadError {
  pub path: str
  pub problem: Problem
  pub line: Nat
  pub column: Nat
}

enum Problem {
  malformed
  missing: str
  shape: (str, str)
  range: str
  case: str
  unknown: str
  twice: str
}
```

`path` is the way to the value from the top, as `tools[2].timeout`. `shape` holds what was needed and what was found, `range` the type a number did not fit, `case` a name that is no case of the enum, `unknown` a field the format refuses, and `twice` a field given two times. `line` and `column` are the place in a text, and are 0 for a format that has none. Reading stops at the first thing that is wrong, and the path and the place are worked out only then, so a read that succeeds does nothing for them.

## The forms

| Written | Is |
| --- | --- |
| `read(r)` | A value of the type the place needs, read from the reader `r`, which is given back |
| A struct, enum, tuple, list, or `Option` | Has `Deserialize` when its parts have |
| A missing `Option` field | `.none` |
| A missing field with a default | Its default |
| Any other missing field | An error that names it |
| `without Deserialize for T` | `T` and what holds it cannot be read |
| `impl Deserialize[T] { ... }` | Replaces what `T` is given |
| `impl Reader[F] { ... }` | Makes `F` a format that is read |
