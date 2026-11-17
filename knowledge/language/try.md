---
type: specification
title: try
description: try, which continues a function only when a step succeeded, and the Try trait that gives it its meaning.
status: partial
built: "`try` as a statement, after a binding, and inside an expression, `Option`, `Result`, and the `Try` trait from the prelude, and a type of a program's own that implements `Try`"
---

# `try`

`try e` gives the value inside `e` when `e` succeeded, and returns from the function with the failure when it did not.

```
fn parse_pair(a: str, b: str) -> Result[(i32, i32), ParseError] {
  x = try parse_int(a)
  y = try parse_int(b)
  .ok((x, y))
}
```

If `parse_int(a)` fails, `parse_pair` returns that failure and the remaining lines do not run.

`x = try e` means `with x = then(e)`, so this is the `parse_pair` of [`with`](with.md), written with a keyword because continuing only on success is so common.

- An error is an ordinary value. Nothing unwinds: `try` only returns a value, so code after a call always runs.
- `try` also works inside a larger expression, as in `.ok((try parse_int(a), try parse_int(b)))`, and as a statement when there is no value to bind. The formatter always writes `try e`.
- A statement that gives a `Result` and does nothing with it is an error, so a failure is never dropped by leaving `try` out.

The keyword is fixed and its meaning is extensible. `then` is the one function of the `Try` trait, which relates a container `C`, the final result `R`, and the value inside the container, `T`, and `try` works on any type that has an implementation. The `then` of [`with`](with.md) is the one for `Result`. An `impl` takes its own type parameters in brackets, and this is the one for `Option`:

```
trait Try[C, R, out T] {
  fn then(c: C, body: (T) -> R) -> R
}

impl[T, U] Try[Option[T], Option[U], T] {
  fn then(c: Option[T], body: (T) -> Option[U]) -> Option[U] {
    match c {
      .some(x) { body(x) }
      .none { .none }
    }
  }
}
```

Any type joins by adding an `impl`. Using `try` on an `Option` inside a function that returns a `Result` does not compile, because no implementation relates the two, so the conversion is written out.

Next: [Effects](effects.md)
