---
type: specification
title: Describe
description: The Describe trait, by which a type says what it is made of with no value in hand, each part with its name and its comment, and Describer, which a format for descriptions has.
status: partial
built:
  - "`std.describe`, with `Describe` and `Describer`, which a program imports with `use` and which the prelude does not hold"
  - "`Describe`, which a whole number, `bool`, `str`, `()`, and every struct, enum, tuple, list, or `Option` is given unless a part lacks it, which `without Describe for T` takes away, and which a type's own `impl` replaces, told part by part to a `Describer` with no value in hand, as `Describe[T].describe(d)`"
  - "the comment of a struct, a field, an enum, and a case, for the types of the file that is built and only where that file writes `Describe` or imports `std.describe`; a type of another module is described with the empty text"
  - "not a map, which `Describe` is not given to as `Serialize` is not, so `map` of `Describer` is never called; not a float, `Slice`, or an array; not what reads a description"
---

# Describe

`Describe` is how a type says what it is made of. [`Serialize`](serialize.md) walks a value, and a description is wanted where there is none: what a function takes is told to whoever may call it before any call is made. A format for descriptions, as JSON Schema, is a type that has `Describer`, and one format describes every type.

```
struct Note {
  // What the note is called.
  title: str
  // How much it matters, from 1 to 5.
  weight: 1..6 = 3
  tags: List[str]
}
```

Described to the `Describer` of JSON Schema, `Note` is an object with a `title` that is a string, "What the note is called.", a `weight` that is a whole number from 1 to 5 and may be left out, and `tags` that is an array of strings. Nothing is written for it.

## The trait

```
trait Describe[T] {
  fn describe[D: Describer](to: take D) -> D ! alloc
}
```

`describe` takes no value. Which type is described is the one named where it is called, as `Describe[Note].describe(d)`.

## The describer

A format for descriptions is a type that has `Describer`, which has a function for each shape. Each takes the describer and gives it back, and has the effect `alloc` and no other, as `Writer`'s do.

```
trait Describer[D] {
  fn boolean(d: take D) -> D ! alloc
  fn signed(d: take D, least: Int, greatest: Int) -> D ! alloc
  fn unsigned(d: take D, least: u64, greatest: u64) -> D ! alloc
  fn text(d: take D) -> D ! alloc
  fn nothing(d: take D) -> D ! alloc
  fn optional(d: take D) -> D ! alloc
  fn sequence(d: take D) -> D ! alloc
  fn tuple(d: take D, size: Nat) -> D ! alloc
  fn structure(d: take D, name: str, about: str, fields: Nat) -> D ! alloc
  fn field(d: take D, name: str, about: str, optional: bool) -> D ! alloc
  fn end_structure(d: take D) -> D ! alloc
  fn enumeration(d: take D, name: str, about: str, cases: Nat) -> D ! alloc
  fn case(d: take D, name: str, about: str, holds: bool) -> D ! alloc
  fn end_enumeration(d: take D) -> D ! alloc
  fn map(d: take D) -> D ! alloc
}
```

`signed` tells the least and the greatest value of a range when both are in `Int`, and `unsigned` when they are not. `optional`, `sequence`, and `map` are each followed by what it holds, `tuple` by its parts, `field` by what the field is, and `case` by what the case holds when `holds` is true. `structure` and `enumeration` are ended by `end_structure` and `end_enumeration`.

## The shapes

A type is described with the shapes a value is [written](serialize.md#the-shapes) with, and a `Describer` has a function for each, as a `Writer` has. Each carries what a description needs and a value does not.

| Shape | What the describer is told |
| --- | --- |
| A boolean, a text, nothing | That it is one |
| A whole number | Its least and its greatest value, which its range gives |
| A value that may be missing | That it may be, and then what it is |
| A sequence | That it is one, and then what each element is |
| A tuple | How many parts, and then each |
| A struct | Its name and its comment, and then for each field its name, its comment, whether it may be left out, and what it is |
| An enum | Its name and its comment, and then for each case its name, its comment, and what it holds |
| A map | What its keys and its values are |

A field may be left out where it has a default or is an `Option`, as [Deserialize](deserialize.md) reads one that is missing. A comment is the one written on the type, the field, or the case, as [Functions](../language/functions.md#comments) says, and the empty text where there is none.

## Given automatically

`Describe` is given to every type `Serialize` is given to, in the same way: a whole number, `bool`, `str`, `()`, and every struct, enum, tuple, list, and `Option` whose parts have it. `without Describe for T` takes it away, and a type's own `impl` takes the place of the one it is given, wherever the type is a part of another.

A comment reaches the executable only for a type that is described, so a program that describes nothing holds none.

## What reads a description

- [MCP](mcp.md) tells a client what each tool takes, from the arguments struct that [`Function`](../language/traits.md#an-impl-for-a-function) gives for the tool.
- An [HTTP server](http.md) can say what each route takes and gives.
- A task of [`project.smith`](../language/project-tasks.md) has its parameters as its command-line flags, which is the same description read another way.

## The forms

| Written | Is |
| --- | --- |
| `Describe[T].describe(d)` | `T` described to the describer `d` |
| `impl Describer[D]` | A format for descriptions |
| `without Describe for T` | `T` with no description |
| `impl Describe[T]` | A description of `T` written by hand |
