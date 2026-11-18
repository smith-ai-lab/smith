---
type: specification
title: try
description: try, which continues a function only when a step succeeded, and the Try trait that gives it its meaning.
status: partial
built:
  - "`try` as a statement, after a binding, and inside an expression, `Option`, `Result`, and the `Try` trait from the prelude, and a type of a program's own that implements `Try`"
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

## A step that can fail

A function that can fail gives a `Result`, and `try` is written in a function whose own result is a `Result`. Here `find` fails with the name it looked for.

```
fn find(name: str, have: Option[str]) -> Result[str, str] {
  match have {
    .some(value) { .ok(value) }
    .none { .err(name) }
  }
}

fn both(a: Option[str], b: Option[str]) -> Result[(str, str), str] {
  x = try find("a", a)
  y = try find("b", b)
  .ok((x, y))
}
```

`both(.some("1"), .some("2"))` is `.ok(("1", "2"))`. `both(.some("1"), .none)` is `.err("b")`, since `find("b", b)` failed and `both` returned its failure. `both(.none, .none)` is `.err("a")`: the second `find` never ran.

## Where try is written

`x = try e` binds the value. A statement that gives no value to bind is written `try e`, and only the failure matters.

```
fn check(a: Option[str]) -> Result[str, str] {
  try find("a", a)
  .ok("done")
}
```

`check(.some("1"))` is `.ok("done")`, and `check(.none)` is `.err("a")`.

`try` also works inside a larger expression, and the formatter always writes `try e`.

```
fn pair(a: Option[str], b: Option[str]) -> Result[(str, str), str] {
  .ok((try find("a", a), try find("b", b)))
}
```

The two `try`s run from left to right, as the arguments of a call do, so `pair(.none, .none)` is `.err("a")`.

## A failure is a value

An error is an ordinary value. Nothing unwinds: `try` only returns a value, so code after a call always runs. A function that matches on the `Result` instead of writing `try` goes on with the next line whatever the result was.

```
fn report(a: Option[str]) -> str {
  match find("a", a) {
    .ok(value) { value }
    .err(name) { "no {name}" }
  }
}
```

A statement that gives a `Result` and does nothing with it is an error, so a failure is never dropped by leaving `try` out.

```
fn check(a: Option[str]) -> Result[str, str] {
  find("a", a)
  .ok("done")
}
```

This is refused: "this gives a `Result[str, str]`, which nothing uses, so a failure would be dropped", with the fix "write `try` before it, or match on it".

## What try means

`x = try e` means `with x = then(e)`, so this is the `parse_pair` of [`with`](with.md), written with a keyword because continuing only on success is so common.

```
fn parse_pair(a: str, b: str) -> Result[(i32, i32), ParseError] {
  with x = then(parse_int(a))
  with y = then(parse_int(b))
  .ok((x, y))
}
```

The keyword is fixed and its meaning is extensible. `then` is the one function of the `Try` trait, which relates a container `C`, the final result `R`, and the value inside the container, `T`, and `try` works on any type that has an implementation. The `then` of [`with`](with.md) is the one for `Result`. An `impl` takes its own type parameters in brackets, and this is the one for `Option`:

```
trait Try[C, R, out T] {
  fn then(c: C, body: fn(T) -> R) -> R
}

impl[T, U] Try[Option[T], Option[U], T] {
  fn then(c: Option[T], body: fn(T) -> Option[U]) -> Option[U] {
    match c {
      .some(x) { body(x) }
      .none { .none }
    }
  }
}
```

The trait and its implementations for `Option` and `Result` are the prelude's, so a program writes none of them to use `try` on those types. A function that gives an `Option` writes `try` on one.

```
fn first(o: Option[str]) -> Option[str] {
  x = try o
  .some(x)
}
```

`first(.some("a"))` is `.some("a")` and `first(.none)` is `.none`.

## What try needs

`try` needs an implementation of `Try` for the type it is applied to and the result type of the function it is written in. Where there is none, the program is refused. Using `try` on an `Option` inside a function that returns a `Result` does not compile, because no implementation relates the two, so the conversion is written out.

```
fn first(o: Option[str]) -> Result[str, str] {
  x = try o
  .ok(x)
}
```

This is refused: "there is no implementation of `Try` for `Option[str]` and `Result[str, str]`". A function that returns `str` is refused the same way, "for `Option[str]` and `str`", and so is a `try` inside a [lambda](lambdas.md), which returns from the lambda and not from the function around it: its result type is the lambda's.

```
fn each(f: fn(str) -> str) -> str { f("q") }

fn go() -> Result[str, str] {
  s = each() fn(v) {
    x = try find(v, .some(v))
    x
  }
  .ok(s)
}
```

This is refused: "there is no implementation of `Try` for `Result[str, str]` and `str`".

Any type joins by adding an `impl`. A type of a program's own that holds a value or nothing:

```
enum Maybe {
  some: str
  none
}

impl Try[Maybe, Maybe, str] {
  fn then(c: Maybe, body: fn(str) -> Maybe) -> Maybe {
    match c {
      .some(x) { body(x) }
      .none { .none }
    }
  }
}

fn go(m: Maybe) -> Maybe {
  x = try m
  .some(x)
}
```

`go(.some("a"))` is `.some("a")`, and `go(.none)` is `.none`.

## Failures of different types

A failure of one type becomes a failure of another on its way out, where the first [converts](conversion.md) to the second. So a function gathers the failures of the steps it takes into one enum, which is given a `Convert` from each of them.

```
enum PortError {
  number: ParseError
  range: RangeError
}

fn port(text: str) -> Result[u16, PortError] {
  n = try parse_int(text)
  p = try in_range(n)
  .ok(p)
}
```

`parse_int` fails with a `ParseError` and `in_range` with a `RangeError`, and `port` returns either as a `PortError`. Nothing about this is built in. It is one more implementation of `Try`, for two `Result`s whose failures differ:

```
impl[T, U, E, F] Try[Result[T, E], Result[U, F], T]
  requires Convert[E, F]
{
  fn then(c: Result[T, E], body: fn(T) -> Result[U, F]) -> Result[U, F] {
    match c {
      .ok(x) { body(x) }
      .err(e) { .err(convert(e)) }
    }
  }
}
```

Without a `Convert` from the step's failure to the function's, there is no such implementation, and the program is refused the same way as above: "there is no implementation of `Try` for `Result[str, A]` and `Result[str, B]`".

Only `try` converts. A function's own result is written out, as `.ok(p)` is.

## The forms

| Written | Means |
| --- | --- |
| `x = try e` | The value inside `e`, or return the failure |
| `try e` | The same, for a step with no value to keep |
| `.ok((try a, try b))` | `try` inside an expression, left to right |
| `x = try e` | Another way to write `with x = then(e)` |
| `impl Try[C, R, T]` | How a type joins, with its own `then` |
| `Result[T, E]` into `Result[U, F]` | Allowed when `Convert[E, F]` exists |

Next: [`return`](return.md)
