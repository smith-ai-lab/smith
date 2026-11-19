---
type: specification
title: Serialize
description: The Serialize trait, by which a value describes its shape to a writer that knows a format, given automatically to a type whose parts have it.
status: partial
built:
  - "`std.serialize`, with `Serialize` and `Writer`, which a program imports with `use` and which the prelude does not hold"
  - "`Serialize`, which a whole number, `bool`, `str`, `()`, and every struct, enum, tuple, list, or `Option` is given unless a part lacks it, which `without Serialize for T` takes away, and which a type's own `impl` replaces, written part by part to a `Writer` with no value built between"
  - "not a float, and so not `Writer`'s `float`, which waits for `f64`; not `Slice`, an array, or `Map`"
---

# Serialize

`Serialize` is the trait by which a value describes its shape to a writer. The writer knows a format, such as JSON, and the value knows none.

```
struct Point {
  x: i64
  y: i64
}

fn main() ! console, alloc {
  print(to_json(Point(x: 3, y: 4)))
}
```

This prints `{"x":3,"y":4}`. `Point` has `Serialize` without a line written for it, and `to_json` is a function of `std.json` that hands the value a writer of JSON. `Serialize` and `Writer` are in `std.serialize`, which a program imports with `use std.serialize (Serialize, Writer, write)`, and the prelude holds neither.

## The trait

`Serialize` has one function, which takes a value and a writer and gives the writer back with the value written to it.

```
trait Serialize[T] {
  fn write[W: Writer](value: T, to: take W) -> W ! alloc
}
```

The writer is [taken](../language/take.md) and given back, so a value is written straight to where it goes, and nothing is built in between.

Every function of `Serialize` and of `Writer` has the effect `alloc` and no other. A format writes into memory, and what is done with the bytes, sending them to a file or over a network, is done by whoever asked for them, with their own effects, so a writer does no input or output. A writer that asks for no memory writes `! alloc` on its functions as its trait does, since an `impl` has the effects of its trait's function, and asks for none when it runs.

## The shapes

A value describes itself with twelve shapes and no others. A format is a type that has `Writer`, which has a function for each.

| Shape | Is written by | For |
| --- | --- | --- |
| A boolean | `boolean(w, value)` | `bool` |
| A whole number | `signed(w, value)` and `unsigned(w, value)` | `i8` to `i64`, and `u8` to `u64` and `Nat` |
| A float | `float(w, value)` | `f32`, `f64` |
| A text | `text(w, value)` | `str` |
| Nothing | `nothing(w)` | `()` |
| A value that may be missing | `absent(w)`, or `present(w)` and then the value | `Option[T]` |
| A sequence | `sequence(w, size)`, each element, `end_sequence(w)` | `List[T]`, `Slice[T]`, an array |
| A tuple | `tuple(w, size)`, each part, `end_tuple(w)` | `(A, B)` |
| A struct | `structure(w, name, fields)`, then `field(w, name)` and the value for each field, `end_structure(w)` | A struct |
| A case of an enum | `case(w, of, name, holds)`, and then what it holds when it holds something | An enum |
| A map | `map(w, size)`, then a key and its value for each entry, `end_map(w)` | `Map[K, V]` |
| Bytes of a format | `raw(w, of, bytes)` | A value that holds the text of another value in a format, as `RawJson` |

```
trait Writer[W] {
  fn boolean(w: take W, value: bool) -> W ! alloc
  fn signed(w: take W, value: i64) -> W ! alloc
  fn unsigned(w: take W, value: u64) -> W ! alloc
  fn text(w: take W, value: str) -> W ! alloc
  fn nothing(w: take W) -> W ! alloc
  fn absent(w: take W) -> W ! alloc
  fn present(w: take W) -> W ! alloc
  fn sequence(w: take W, size: Nat) -> W ! alloc
  fn end_sequence(w: take W) -> W ! alloc
  fn tuple(w: take W, size: Nat) -> W ! alloc
  fn end_tuple(w: take W) -> W ! alloc
  fn structure(w: take W, name: str, fields: Nat) -> W ! alloc
  fn field(w: take W, name: str) -> W ! alloc
  fn end_structure(w: take W) -> W ! alloc
  fn case(w: take W, of: str, name: str, holds: bool) -> W ! alloc
  fn map(w: take W, size: Nat) -> W ! alloc
  fn end_map(w: take W) -> W ! alloc
  fn raw(w: take W, of: str, bytes: Slice[u8]) -> W ! alloc
}
```

`Writer` has a `float(w, value: f64) -> W ! alloc` for the shape of a float, which is written once `f64` is built. `of` names the format that `bytes` are in, as `"json"` does for the text of a [`RawJson`](json.md#a-part-of-the-text-that-is-read-later). A writer of that format writes the bytes where they go, as one value, with nothing copied but the bytes into its output. Any other writer writes them as a text, as `text` does, so bytes are never put into a format they are not in. The bytes are a text until there is a format that is not text and a shape for bytes, so a writer is not given bytes that are not UTF-8. How a shape looks in the output is the format's to say: whether a case of an enum is a text or an object in JSON is said by `std.json`, and `Serialize` says only that it is a case.

## Given automatically

A struct, an enum, a tuple, a list, a slice, and an `Option` have `Serialize` when each of their parts has it, as they have [`Eq`](../language/comparisons.md). The numbers, `bool`, `str`, and `()` have it. What `Point` above is given is what this writes:

```
impl Serialize[Point] {
  fn write[W: Writer](value: Point, to: take W) -> W ! alloc {
    var w = structure(to, "Point", 2)
    w = field(w, "x")
    w = write(value.x, w)
    w = field(w, "y")
    w = write(value.y, w)
    end_structure(w)
  }
}
```

The fields are written in the order the struct lists them.

## Taking it away

[`without`](../language/without.md) takes `Serialize` from a type, as it takes any trait that is given.

```
struct Session {
  token: str
}

without Serialize for Session
```

`to_json(Session(token: "s"))` is then refused, and so is writing any struct that holds a `Session`.

## Writing it yourself

A type's own `impl` replaces the one it is given, wherever the type is a part of another. This writes a `Celsius` as the number it holds.

```
struct Celsius {
  degrees: i64
}

impl Serialize[Celsius] {
  fn write[W: Writer](value: Celsius, to: take W) -> W ! alloc { signed(to, value.degrees) }
}
```

`to_json(Celsius(degrees: 21))` is `21`, and a struct with a `Celsius` field writes `21` for that field.

## A format of your own

A format is a type with an `impl` of `Writer`, written in any module. `std.json` is one, and it has no place the language knows of: a program's own format is written the same way and takes every type that has `Serialize`.

## The forms

| Written | Is |
| --- | --- |
| `write(value, w)` | `value` written to the writer `w`, which is given back |
| A struct, enum, tuple, list, slice, or `Option` | Has `Serialize` when its parts have |
| `without Serialize for T` | `T` and what holds it cannot be written |
| `impl Serialize[T] { ... }` | Replaces what `T` is given |
| `impl Writer[F] { ... }` | Makes `F` a format |
