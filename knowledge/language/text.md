---
type: specification
title: Text and printing
description: Where a program starts, print, string literals that hold expressions formatted by the Fmt trait, and Repr, which writes a value as source.
status: partial
built: "`main`, `print`, string literals and their escapes, strings that hold expressions of any type that implements `Fmt`, `repr` and the `Repr` of a `str`, and the `Fmt` and `Repr` every struct, enum, tuple, `bool`, `()`, and integer type is given, which `without` takes away"
---

# Text and printing

A program starts at `main`, and ends with status 0 when `main` returns. `print` takes a `str` and writes it and a line break to standard output. It carries the `console` effect, which covers standard input, output, and error. `print` is in scope in [every file](modules.md), and the rest of the console is in [std.console](../library/console.md).

```
fn main() ! console {
  print("hello world")
}
```

Every string literal may hold expressions in braces, and there is no other kind of string. Each expression is turned into text by the `Fmt` trait, and the pieces are joined into one `str`. `print` never formats anything: it only ever receives a `str`.

```
trait Fmt[T] {
  fn fmt(value: T) -> str ! alloc
}

fn greet(name: str, age: i32) ! console, alloc {
  print("hello {name}, you are {age}")
}
```

- The braces hold any expression of a type that has `Fmt`.
- Nothing is special-cased. A `str` in braces goes through its own `Fmt` implementation, like a value of any other type.
- There are no format options inside the braces. A width or a number of decimal places is an ordinary function call, as in `"{fixed(x, 2)}"`.
- A string with an expression in it has a size known only at run time, so it carries `alloc`. A plain string literal does not.

`Fmt` is text for a person. A second trait, `Repr`, writes a value as Smith source, so that what is printed can be pasted back into a program.

```
trait Repr[T] {
  fn repr(value: T) -> str ! alloc
}

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

- `repr` is an ordinary function, called inside the braces like any other. The braces always use `Fmt`, and a value never prints differently because of where it is.
- The `Repr` of a `str` is the text in quotes, with its escapes. The `Fmt` of a `str` is the text alone. For a number and a `bool` the two are the same, and a float always shows its point, as in `2.0`.
- The `Repr` of a struct is its construction, as in `Cat(name: "tom")`, with the `Repr` of each field. A tuple is `("a", "b")` and the empty tuple is `()`, a case is `.cat(Cat(name: "tom"))` or `.none`, and an array or list is `[1, 2, 3]`.
- A struct with exactly one field that is not a proof, such as `Fin`, is written as that field's value.
- Both traits are given automatically, as [`Copy`](ownership.md) is: a struct, an enum, or a tuple has each unless one of its fields lacks it. Its `Fmt` is its `Repr`. `without Fmt for Secret` takes one away.
- A type that should read differently to a person implements `Fmt` itself, and its implementation replaces the one it was given. Its `Repr` stays.

A backslash writes a character that would otherwise mean something else. These escapes are the whole set, and any other character after a backslash is a compile error.

| Written | Means |
| --- | --- |
| `\"` | A double quote |
| `\\` | A backslash |
| `\{` and `\}` | A brace |
| `\n` | A line break |
| `\t` | A tab |

```
fn show(x: i32, y: i32) ! console, alloc {
  print("point \{{x}, {y}\}")
}
```

`show(3, 4)` prints `point {3, 4}`.

- A brace that is not escaped always opens or closes an expression, so a `}` on its own is a compile error.
- A string literal ends on the line it starts. A line break in the text is written `\n`.

Next: [Ownership](ownership.md)
