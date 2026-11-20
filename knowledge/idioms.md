---
type: specification
title: Idiomatic Smith
description: Which form of the language to reach for, with the shape from another language it replaces, for state, loops, numbers, failure, and names.
---

# Idiomatic Smith

A program can be written in Smith in the shape of the language its author knows, and it will compile and be worse for it. This page says which form to reach for. Each feature is taught on its own page; this page only chooses between them.

## State that a family of functions advances is a handler's

A cursor over tokens, a place in a buffer, a counter: when many functions read and move one thing, that thing is the state of a [handler](language/handlers.md), and the functions carry an effect that says so.

```
effect tokens {
  fn peek() -> Token
  fn advance()
}

fn reading[T](toks: List[Token], body: fn() -> T ! tokens) -> T {
  var at: Nat = 0
  tokens(
    peek: { if at < toks.size { toks[at] } else { end_of_file() } },
    advance: { if at + 1 < toks.size { at = at + 1 } },
    body: body,
  )
}

fn name() -> Result[Name, ParseError] ! tokens {
  here = peek()
  match here.tok {
    .ident(text) {
      advance()
      .ok(Name(text: text, pos: here.pos))
    }
    other { .err(expected("a name", here)) }
  }
}
```

`name` gives what it read and nothing else. A failure carries no cursor, as the handler still holds the place. The signature says `! tokens`, so a reader knows the function moves through the tokens.

The shape this replaces is a struct given to every function and given back by it: `fn name(c: take Cursor) -> Result[Got, Failed]`, with a struct for each result to hold the cursor beside the value, and the cursor in the error too. It is the translation of `&mut self`, and where one function is given the wrong cursor it reads a token twice in silence.

## One value changed in place is taken and given back

Where a function changes one value and gives nothing else, it [takes](language/take.md) the value and returns it, and the caller assigns the result to the same name.

```
xs = push(xs, 4)
t = append(t, "more")
```

Nothing is copied: the value is written where it is. Several such steps in a row read in order with a [pipeline](language/pipeline.md).

```
page = text() |> append("<p>") |> append(body) |> append("</p>") |> finish
```

Reach for a handler, as above, when the functions give other results too, or may fail.

## A loop over things is a loop over them

`loop x in xs` gives each element, and `loop i in a..b` each number. A loop that goes on until something is found is an [open-ended loop](language/open-loop.md) with a measure that says why it ends.

```
loop decreases b.size - i {
  if i >= b.size or b[i] != ' ' { break }
  i = i + 1
}
```

The shape this replaces is a counted loop whose count is never reached, kept only so the loop is known to end, with the real test inside it: `loop round in 0..b.size { if i >= b.size { break } ... }`. It tests the size twice for each round.

## A number's type says what it holds

A [range](language/number-types.md) in a signature says what a caller must give and what the body may assume, so nothing is checked twice and nothing is saturated to make the checker quiet.

```
fn pick(xs: List[Nat], i: 0..xs.size) -> Nat { xs[i] }

fn next(i: 0..255) -> Byte { i + 1 }
```

`i + 1` is exact: it is a `1..256`, which a `Byte` holds. The shapes this replaces are `saturating_add(i, 1)` where the sum cannot in fact overflow, a `requires i < xs.size` that the type could say, and a `Nat` where the function means a smaller range. `wrapping_` is for arithmetic that is meant to wrap, as a hash is.

## What may fail gives a Result, and what may be absent an Option

A function that can fail returns [`Result`](language/try.md), and a caller that passes the failure on writes `try`. A value that may not be there is an `Option`. No number or empty text stands for "none" or "failed".

```
fn port(text: str) -> Result[0..65536, str] {
  n = try whole(text)
  if n < 65536 { .ok(n) } else { .err("a port is below 65536") }
}
```

## A name is imported, and written with its module where two meet

A file lists what it uses: `use std.console (console, print)`. Where two modules have a function of one name, the file imports the modules and writes each with its module, as [Qualified names](language/qualified-names.md) says: `jsonrpc.read(text)` beside `json.read(r)`.

## A value given a name is a definition, not a function

A number or a text used in many places is defined once [at the top level of the file](language/structs.md) and used by its name, and is not a function of no parameters that returns it.

## What to do where the form is not built

Each page says in its `status` what the compiler builds. Where the form this page chooses is not built yet, write the nearest form that is, and say so in the pull request with the lines side by side: that report is how this page and the roadmap are kept right. Do not design around a gap in a way that outlives it.
