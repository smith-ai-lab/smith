---
type: specification
title: Console
description: The console effect, for a program's standard input, output, and error, and how to handle it.
---

# Console

The module `std.console` declares the `console` [effect](../language/effect-declarations.md). It covers the three standard streams of a program, which the operating system grants together.

```
enum Stream {
  stdout
  stderr
}

effect console {
  fn print(text: str, end: str = "\n", to: Stream = .stdout)
  fn input(prompt: str = "") -> Option[str] ! alloc
}
```

| Operation | Does |
| --- | --- |
| `print` | Writes the text and then `end` to the stream `to` |
| `input` | Writes the prompt to standard output, then returns the next line of standard input without its line break, or `.none` at the end of input |

```
print("hello world")
print("loading", end: "")
print("could not open the file", to: .stderr)
```

- `print` is in the prelude, so every program can use it without an import. `input` and `Stream` are imported from `std.console`.
- The console is a program's real output, which another program may read, so the text is written as it is given and nothing is formatted.
- Each operation is whole. A line that one task prints is never split by another task's output.
- A write that the system refuses [ends the task](../language/tasks.md) that made it, as running out of memory does. Its parent sees an error from `join`, and in `main` the program ends with a failing status. A handler written in Smith never does this.

```
use std.console (console, input, print)

fn greet() ! console, alloc {
  match input("name: ") {
    .some(name) { print("hello {name}") }
    .none { print("hello") }
  }
}
```

A handler supplies both operations. This one keeps everything printed to standard output for a test to look at, and passes the rest to the console outside it, which is why it carries `console` itself.

```
use std.console (console, input)
use std.list (push)

fn capture[T](body: fn() -> T ! console) -> (T, List[str]) ! console, alloc {
  var out: List[str] = []
  r = console(
    print: fn(text, end, to) {
      match to {
        .stdout { out = push(out, "{text}{end}") }
        .stderr { print(text, end: end, to: .stderr) }
      }
    },
    input: fn(prompt) { input(prompt) },
    body: body,
  )
  (r, out)
}
```
