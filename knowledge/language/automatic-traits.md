---
type: specification
title: Traits a type is given
description: Eq and Ord, which a struct, an enum, a tuple, an array, a List, or a Slice is given without an impl, how each orders its values, and how without takes one away.
status: partial
built:
  - "`Ord` given to a `str`, a struct, an enum, a tuple, `bool`, and `()`, from their parts as the page says, and `without Ord for T`, which takes it away, and a type's own `impl Ord`, which replaces it"
  - "An `impl Eq[T]` that a program writes is refused, as `==` is only the `Eq` a type is given"
  - "`Eq` of a `List` and of a `Slice`, which are equal when they have the same size and equal elements in order, and a list whose element has no `Eq` has none; not `Ord` of a list or a slice"
  - "`without Eq for T`, which takes away the `Eq` a struct was given, and a struct with a part that has no `Eq` has none"
---

# Traits a type is given

`Eq` and `Ord` are given automatically, as [`Copy`](ownership.md) is. A type that has them needs no `impl` to be compared.

## Given from its parts

A struct, an enum, a tuple, an array, a `List`, or a `Slice` has `Eq` and `Ord` unless one of its parts lacks it, and the part of a list is its element. So `Door` has `Eq` when `Key` has it.

```
struct Key {
  secret: str
}

struct Door {
  key: Key
  name: str
}
```

`Door(key: Key(secret: "k"), name: "front") == Door(key: Key(secret: "k"), name: "front")` is `true`.

## How a given Ord orders

The `Ord` a type is given orders it from its parts, as a dictionary orders words.

- A struct is ordered by its fields, in the order they are written.
- A tuple and an array are ordered by their elements, in order.
- A `List` and a `Slice` are ordered by their elements, in order, and one that is the start of another comes before it. So `[1, 2] < [1, 3]` and `[1, 2] < [1, 2, 0]` are both `true`.
- An enum is ordered by the order of its cases, and then by what a case holds.
- Text is ordered by its bytes.

```
struct Version {
  major: Int
  minor: Int
}
```

`Version(major: 1, minor: 9) < Version(major: 2, minor: 0)` is `true`, as `major` is compared first, and `minor` only when the majors are equal.

## Taking one away

`without Eq for Key` takes away the `Eq` that `Key` was given. Then `Key` cannot be compared, and neither can a type that has a `Key`.

```
use std.console (console, print)

struct Key {
  secret: str
}

without Eq for Key

struct Door {
  key: Key
}

fn main() ! console {
  a = Door(key: Key(secret: "a"))
  if a == a { print("same") }
}
```

This is refused: "there is no implementation of `Eq` for `Door`, as `Key` has no `Eq`". The same comparison of two `Key`s is refused: "there is no implementation of `Eq` for `Key`".

## Writing your own

`Eq` is not written by a program. `==` means that two values are the same value, so the `Eq` a type is given is the only one it has, and an `impl Eq[Cat]` is refused, as [Comparisons](comparisons.md) says. A looser comparison is a function with a name of its own.

## The forms

| Written | Is |
| --- | --- |
| a struct, enum, tuple, array, `List`, or `Slice` | Has `Eq` and `Ord` when each of its parts has |
| `without Eq for T` | `T` has no `Eq` |
| `without Ord for T` | `T` has no `Ord` |
| `impl Eq[T] { ... }` | Refused: `==` means the same value, and is not written by a program |

Next: [Literals](literals.md)
