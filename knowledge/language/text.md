---
type: specification
title: Text and printing
description: Where a program starts, print, and string literals that hold expressions formatted by the Fmt trait.
---

# Text and printing

A program starts at `main`. `print` takes a `str` and writes it and a newline to standard output. It carries the `console` effect, which covers standard input, output, and error.

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
- A literal brace is written `\{`.
- A string with an expression in it has a size known only at run time, so it carries `alloc`. A plain string literal does not.

Next: [Ownership](ownership.md)
