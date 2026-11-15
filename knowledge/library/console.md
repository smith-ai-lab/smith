---
type: specification
title: Console
description: The console effect, for a program's standard input, output, and error, and how to handle it.
---

# Console

The module `std.console` declares the `console` [effect](../language/effects.md). It covers the three standard streams of a program, which the operating system grants together.

```
effect console {
  fn print(text: str)
  fn write(text: str)
  fn write_error(text: str)
  fn read_line() -> Option[str] ! alloc
}
```

| Operation | Does |
| --- | --- |
| `print` | Writes the text and a line break to standard output |
| `write` | Writes the text to standard output, with nothing added |
| `write_error` | Writes the text to standard error, with nothing added |
| `read_line` | Returns the next line of standard input without its line break, or `.none` at the end of input |

- `print` is in the prelude, so every program can use it without an import. The other three are imported from `std.console`.
- Text arrives exactly as it was written. The console is a program's real output, which another program may read, so nothing is added to it and nothing is formatted.
- Each operation is whole. A line that one task prints is never split by another task's output.

```
use std.console (read_line, write)

fn ask(question: str) -> Option[str] ! console, alloc {
  write(question)
  read_line()
}
```

A handler supplies all four operations. This one keeps every printed line for a test to look at, and passes the other operations to the console outside it, which is why it carries `console` itself.

```
use std.console (console, read_line, write, write_error)

fn capture[T](body: () -> T ! console) -> (T, List[str]) ! console, alloc {
  var lines: List[str] = []
  r = console(
    print: fn(text) { lines := push(lines, text) },
    write: fn(text) { write(text) },
    write_error: fn(text) { write_error(text) },
    read_line: fn() { read_line() },
    body: body,
  )
  (r, lines)
}
```
