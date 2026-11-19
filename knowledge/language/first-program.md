---
type: specification
title: A first program
description: A program that starts at main, prints a line, and is run with smith run, which every later page uses to show what its examples print.
status: built
---

# A first program

A program is a file with a function named `main`, where it starts. It ends when `main` returns.

## Printing a line

`print` takes a `str` and writes it, and a line break, to standard output.

```
use std.console (console, print)

fn main() ! console {
  print("hello world")
}
```

This prints `hello world`. A `main` that prints twice prints two lines, in order.

```
use std.console (console, print)

fn main() ! console {
  print("one")
  print("two")
}
```

This prints `one`, then `two`. `print` is in scope in [every file](prelude.md), and the rest of the console is in [std.console](../library/console.md).

## `! console`

`! console` after the parentheses says that `main` may print. It is an [effect](effects.md), which a function must list for everything it does outside itself, and `print` needs `console`. Without it the program is refused:

```
use std.console (print)

fn main() {
  print("hi")
}
```

This gives "`print` needs the `console` effect, which `main` does not declare", with the fix "write `fn main() ! console`".

A program with no `main` is refused too: "there is no function named `main`, where the program starts".

## A string that holds an expression

A string may hold an expression in braces. Its value is written into the text where the braces are.

```
use std.console (console, print)

fn main() ! console, alloc {
  print("hi {2 + 3}")
}
```

This prints `hi 5`. A string with an expression in it needs the `alloc` effect as well, so `main` lists both. Leaving it out is refused: "a string that holds an expression needs the `alloc` effect, which `main` does not declare", with the fix "write `fn main() ! console, alloc`". [Strings that hold expressions](interpolation.md) says the rest.

## Running it

`smith run` builds the program in `main.smith`, or in the file it is given, and runs it. Its output is the program's.

```
smith run hello.smith
```

With the first program above in `hello.smith`, this prints `hello world`. Both `smith run` and the other commands are in [The command line](command-line.md).

Next: [Functions](functions.md)
