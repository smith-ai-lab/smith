---
type: specification
title: Lambdas, with, and try
description: Lambdas, with to pass the rest of a block to a function, and try to continue only on success.
---

# Lambdas, `with`, and `try`

A lambda is `fn(x) { e }`, and a function type is written `(T) -> U`. A default belongs to a function's name and not to its type, so a function passed as a value takes every argument.

A lambda is passed like any other argument.

```
fn unless(skip: bool, otherwise: f32, body: () -> f32) -> f32 {
  if skip { otherwise } else { body() }
}

fn fee(is_member: bool, w: f32, h: f32) -> f32 {
  unless(is_member, 0.0, fn() { area(w, h) * 0.1 })
}
```

`with x = f(args)` passes the rest of the enclosing block to `f` as a lambda taking `x`, added as the last argument. It is sugar for a call, and it keeps code flat.

```
enum Result[T, E] {
  ok: T
  err: E
}

fn then[T, U, E](r: Result[T, E], body: (T) -> Result[U, E]) -> Result[U, E] {
  match r {
    .ok(x) { body(x) }
    .err(e) { .err(e) }
  }
}

fn parse_pair(a: str, b: str) -> Result[(i32, i32), ParseError] {
  with x = then(parse_int(a))
  with y = then(parse_int(b))
  .ok((x, y))
}
```

The body of `parse_pair` means exactly this:

```
then(parse_int(a), fn(x) {
  then(parse_int(b), fn(y) {
    .ok((x, y))
  })
})
```

`with` always fills a call's last parameter. So a final lambda parameter may come after parameters that have defaults, and a call can leave those out: with `fn repeat(times: i32 = 2, body: () -> f32) -> f32`, the line `with repeat()` uses the default.

The binding is optional: `with f(args)` passes a lambda that takes no parameters. The lambda is inlined and cannot escape, so `with` costs nothing at run time.

Continuing only on success is common enough to have a keyword. `x = try e` means `with x = then(e)`, so `parse_pair` is written like this:

```
fn parse_pair(a: str, b: str) -> Result[(i32, i32), ParseError] {
  x = try parse_int(a)
  y = try parse_int(b)
  .ok((x, y))
}
```

An error is an ordinary value. If `parse_int(a)` fails, `parse_pair` returns that failure and the remaining lines do not run. Nothing unwinds: `try` only returns a value, so code after a call always runs. `try` also works inside a larger expression, as in `.ok((try parse_int(a), try parse_int(b)))`, and as a statement when there is no value to bind. The formatter always writes `try e`.

The keyword is fixed and its meaning is extensible. `then` is the one function of the `Try` trait, which relates a container `C`, the final result `R`, and the value inside the container, `T`, and `try` works on any type that has an implementation. The function above is the one for `Result`. An `impl` takes its own type parameters in brackets, and this is the one for `Option`:

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
