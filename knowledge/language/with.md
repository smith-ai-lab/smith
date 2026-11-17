---
type: specification
title: with
description: with, which passes the rest of a block to a function as a lambda.
status: built
---

# `with`

`with f(args)` passes the rest of the enclosing block to `f` as a [lambda](lambdas.md), added as the last argument. It keeps code flat. With `unless` from [Lambdas](lambdas.md):

```
fn fee(is_member: bool, w: f32, h: f32) -> f32 {
  with unless(is_member, 0.0)
  size = area(w, h)
  size * 0.1
}
```

The body of `fee` means exactly this:

```
unless(is_member, 0.0) {
  size = area(w, h)
  size * 0.1
}
```

When the function gives its lambda a value, `with x = f(args)` names it. It means `f(args) fn(x) { ... }`, with the rest of the block inside the braces. Here `then` runs its lambda on what a `Result` holds, and only when it holds one:

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
then(parse_int(a)) fn(x) {
  then(parse_int(b)) fn(y) {
    .ok((x, y))
  }
}
```

- `with` always fills a call's last parameter. So a final lambda parameter may come after parameters that have defaults, and a call can leave those out: with `fn repeat(times: i32 = 2, body: () -> f32) -> f32`, the line `with repeat()` uses the default.
- The lambda is inlined and cannot escape, so `with` costs nothing at run time.

Next: [`try`](try.md)
