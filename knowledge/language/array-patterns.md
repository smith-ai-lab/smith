---
type: specification
title: Patterns in brackets
description: A pattern in brackets takes an array apart with one pattern for each element, and matches a List of exactly that many elements.
status: planned
---

# Patterns in brackets

A pattern in brackets takes an array apart, with one pattern for each element. It is written in a [`match`](match.md), as a pattern of a tuple is.

## Taking an array apart

```
fn name(rgb: [3]Byte) -> str {
  match rgb {
    [0, 0, 0] { "black" }
    [255, 255, 255] { "white" }
    [r, g, b] { "something else" }
  }
}
```

`name([0, 0, 0])` is `"black"`, and `name([255, 255, 255])` is `"white"`. Anything else, as `name([9, 0, 0])`, is taken by the last pattern, which gives each element a name: `r` is `9`, `g` is `0`, and `b` is `0`. The first pattern that matches is the one taken, as [Match](match.md) says for any pattern.

## A list

A pattern in brackets matches a [`List`](../library/list.md) too, when the list has exactly that many elements. A list can have any number, so a `match` on a list ends with a bare name, as a `match` on whole numbers does, and a `match` without one is refused as it is there.

```
fn describe(xs: List[Byte]) -> str {
  match xs {
    [] { "empty" }
    [x] { "one" }
    [x, y] { "two" }
    other { "more" }
  }
}
```

`describe` of `[]` is `"empty"`, of `[7]` is `"one"`, and of `[1, 2, 3]` is `"more"`. This is how a program reads its arguments, as [`List`](../library/list.md) shows.

## What has no pattern

There is no pattern for the rest of a list. A list of any length is taken by the bare name at the end, and that name is the whole list, as `other` is above.

## The forms

| Written | Matches |
| --- | --- |
| `[a, b, c]` | An array of three elements, or a list of exactly three, naming each |
| `[0, 0, 0]` | An array or a list whose elements are these |
| `[]` | The empty list |
| `other` | Any list, as the last pattern |

Next: [`for` expressions](for.md)
