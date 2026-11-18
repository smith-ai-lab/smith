---
type: specification
title: Traits a type is given
description: Eq and Ord, which a struct, an enum, a tuple, or an array is given without an impl, how each orders its values, and how without takes one away.
status: partial
built:
  - "`without Eq for T`, which takes away the `Eq` a struct was given, and a struct with a part that has no `Eq` has none"
---

# Traits a type is given

`Eq` and `Ord` are given automatically, as [`Copy`](ownership.md) is. A type that has them needs no `impl` to be compared.

## Given from its parts

A struct, an enum, a tuple, or an array has `Eq` and `Ord` unless one of its parts lacks it. So `Door` has `Eq` when `Key` has it.

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
- An enum is ordered by the order of its cases, and then by what a case holds.
- Text is ordered by its bytes.

```
struct Version {
  major: i64
  minor: i64
}
```

`Version(major: 1, minor: 9) < Version(major: 2, minor: 0)` is `true`, as `major` is compared first, and `minor` only when the majors are equal.

## Taking one away

`without Eq for Key` takes away the `Eq` that `Key` was given. Then `Key` cannot be compared, and neither can a type that has a `Key`.

```
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

A type's own `impl` replaces the one it was given. A `Cat` that is equal by its age alone writes it, as [Comparisons](comparisons.md) shows.

```
impl Eq[Cat] {
  fn eq(a: Cat, b: Cat) -> bool { a.age == b.age }
}
```

## The forms

| Written | Is |
| --- | --- |
| a struct, enum, tuple, or array | Has `Eq` and `Ord` when each of its parts has |
| `without Eq for T` | `T` has no `Eq` |
| `without Ord for T` | `T` has no `Ord` |
| `impl Eq[T] { ... }` | Replaces the `Eq` that `T` was given |

Next: [Literals](literals.md)
