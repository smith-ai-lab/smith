---
type: specification
title: Repr
description: The Repr trait, which writes a value as Smith source, and the Fmt and Repr that structs, enums, and tuples are given.
status: partial
built:
  - "`repr` and the `Repr` of a `str`, and the `Fmt` and `Repr` every struct, enum, tuple, `bool`, `()`, and integer type is given, which `without` takes away"
  - "the `Fmt` and `Repr` of a `List` or a `Slice`, written `[1, 2, 3]` with the `Repr` of each element, which is `[]` for none, and which a list has when its element has it"
---

# Repr

`Repr` writes a value as Smith source, so that what is printed can be pasted back into a program. It is the second trait that turns a value into text, beside [`Fmt`](interpolation.md#fmt), which is text for a person.

```
trait Repr[T] {
  fn repr(value: T) -> str ! alloc
}
```

`repr` is an ordinary function, called inside the braces of a string like any other. The braces always use `Fmt`, and a value never prints differently because of where it is.

```
use std.console (console, print)

struct Cat {
  name: str
}

fn describe(tom: Cat) ! console, alloc {
  print("{tom.name}")
  print("{repr(tom.name)}")
  print("{tom}")
}
```

`describe(Cat(name: "tom"))` prints `tom`, then `"tom"`, then `Cat(name: "tom")`.

## What each type writes

- The `Repr` of a `str` is the text in quotes, with its escapes. `repr("a\"b\n")` is `"a\"b\n"`. The `Fmt` of a `str` is the text alone.
- For an integer and a `bool` the two are the same: `repr(7)` is `7` and `repr(true)` is `true`. A float always shows its point, as in `2.0`.
- The `Repr` of a struct is its construction, as in `Cat(name: "tom")`, with the `Repr` of each field.
- A tuple is `("a", "b")` and the empty tuple is `()`.
- A case of an enum is `.cat("a")` for a case that holds a value and `.none` for one that does not, with a leading dot as in a pattern.
- An array or list is `[1, 2, 3]`.

```
use std.console (console, print)

enum Pet {
  cat: str
  none
}

fn pets() ! console, alloc {
  print("{repr(Pet.cat("a"))} {repr(Pet.none)} {repr(("a", "b"))} {repr(())}")
}
```

`pets()` prints `.cat("a") .none ("a", "b") ()`.

## What is given

Both traits are given automatically, as [`Copy`](ownership.md) is: a struct, an enum, or a tuple has each unless one of its fields lacks it. Its `Fmt` is its `Repr`, so `"{tom}"` above is `Cat(name: "tom")` and `"{Pet.cat("a")}"` is `.cat("a")`.

`without` takes one away. After `without Fmt for Secret`, a `Secret` cannot be put in braces.

```
use std.bits (i32)
use std.console (console, print)

struct Secret {
  key: i32
}

without Fmt for Secret

fn leak(s: Secret) ! console, alloc {
  print("{s}")
}
```

This is refused: "there is no implementation of `Fmt` for `Secret`". `without Repr for Cat` does the same for `repr`, which is then refused with "there is no implementation of `Repr` for `Cat`".

A type that should read differently to a person implements `Fmt` itself, as [Strings that hold expressions](interpolation.md#fmt) shows, and its implementation replaces the one it was given. Its `Repr` stays: `repr(Money(cents: 5))` is still `Money(cents: 5)` after `Money` has its own `Fmt`.

## The forms

| Value | `Repr` |
| --- | --- |
| `"a"` | `"a"` |
| `7`, `true` | `7`, `true` |
| `Cat(name: "tom")` | `Cat(name: "tom")` |
| `("a", "b")`, `()` | `("a", "b")`, `()` |
| `Pet.cat("a")`, `Pet.none` | `.cat("a")`, `.none` |
| An array or list of 1, 2, 3 | `[1, 2, 3]` |

Next: [main](main.md)
