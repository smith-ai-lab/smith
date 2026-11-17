---
type: specification
title: main
description: The arguments main may take, and the Exit trait, which turns what main returns into the status a program ends with.
status: partial
built: "`main` with no parameter, or with one, `args: List[str]`, which holds the words after the program's name, as `smith run prog.smith one two` gives them, on every target, and a word that is not valid UTF-8 ends the program before `main` as the page says; any other parameter is refused; not what `main` returns, or `Exit`"
---

# `main`

`main` is where a program starts, as [A first program](first-program.md) shows. It may also take the arguments the program was started with, and its return type decides the status the program ends with.

## Arguments

`main` may take the words after the program's name, as a list.

```
fn main(args: List[str]) ! console, alloc {
  print("started with {args.size} arguments")
}
```

Started with the words `one` and `two` after its name, this prints `started with 2 arguments`. The program's name is not one of them.

A `str` is always valid UTF-8, so a program that takes its words and is started with one that is not valid ends before `main` runs. It writes one line to standard error that says which word, counted from 1, is not valid, as in `argument 2 is not valid UTF-8`, and ends with status 1. No byte is replaced.

## The status

What `main` returns decides the status the program ends with. `main` may return any type that implements `Exit`, whose one function gives the status.

```
trait Exit[T] {
  fn exit(value: T) -> u8 ! console, alloc
}
```

- `()` gives 0, so a `main` that returns nothing ends with status 0.
- A `Result[T, E]` gives what its `T` gives when it is `.ok`. When it is `.err(e)`, it writes `e` to standard error, as [`Fmt`](interpolation.md#fmt) turns it into text, and gives 1.

```
fn main() -> Result[(), str] ! console, alloc {
  print("started")
  .err("could not finish")
}
```

This prints `started` to standard output and `could not finish` to standard error, and the program ends with status 1. Because a failure that `main` returns ends the program, `main` can use [`try`](try.md).

- A program that needs other statuses implements `Exit` for a type of its own.

```
enum Outcome {
  done
  usage
}

impl Exit[Outcome] {
  fn exit(value: Outcome) -> u8 ! console, alloc {
    match value {
      .done { 0 }
      .usage { 2 }
    }
  }
}

fn main() -> Outcome ! console, alloc { .usage }
```

This program ends with status 2.

- Nothing else ends a program. No function stops it from inside a call, so the code after a call always runs.

## The forms

| `main` returns | Status |
| --- | --- |
| Nothing | 0 |
| `Result[T, E]` that is `.ok` | What `T` gives |
| `Result[T, E]` that is `.err(e)` | 1, after `e` is written to standard error |
| A type with its own `Exit` | What its `exit` gives |

Next: [Text and bytes](text.md)
