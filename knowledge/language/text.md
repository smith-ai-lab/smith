---
type: specification
title: Text and printing
description: Where a program starts, print, and string literals that hold expressions formatted by the Fmt trait.
status: partial
built: "`main`, `print`, string literals, and their escapes"
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

- The braces hold any expression. A type joins in by implementing `Fmt`, and `derive Fmt for Rect` writes the obvious implementation.
- Nothing is special-cased. A `str` in braces goes through its own `Fmt` implementation, like a value of any other type.
- There are no format options inside the braces. A width or a number of decimal places is an ordinary function call, as in `"{fixed(x, 2)}"`.
- A string with an expression in it has a size known only at run time, so it carries `alloc`. A plain string literal does not.

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
