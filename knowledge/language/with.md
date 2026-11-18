---
type: specification
title: with
description: with, which passes the rest of a block to a function as a lambda.
status: built
---

# `with`

`with f(args)` passes the rest of the enclosing block to `f` as a [lambda](lambdas.md), added as the last argument. It keeps code flat. With `unless` from [Lambdas](lambdas.md):

```
fn unless(skip: bool, otherwise: f32, body: fn() -> f32) -> f32 {
  if skip { otherwise } else { body() }
}

fn fee(is_member: bool, w: f32, h: f32) -> f32 {
  with unless(is_member, 0.0)
  size = area(w, h)
  size * 0.1
}
```

The body of `fee` means exactly this:

```
fn fee(is_member: bool, w: f32, h: f32) -> f32 {
  unless(is_member, 0.0) {
    size = area(w, h)
    size * 0.1
  }
}
```

## What comes before and after

The lines before `with` run as they are written, and the names they define may be used after it. The rest of the block is the lambda's block, and its last expression is the lambda's value.

A function that runs its lambda between two things of its own shows the order. `wrap` runs what it is given, then prints `after`.

```
fn wrap(body: fn()) ! console {
  body()
  print("after")
}

fn main() ! console {
  print("before")
  with wrap()
  print("in")
  print("in again")
}
```

This prints `before`, `in`, `in again`, and `after`: the lines under `with` run where `wrap` calls `body`.

When nothing follows the `with`, the lambda has no lines. The `main` below prints `after`.

```
fn main() ! console {
  with wrap()
}
```

Two `with`s nest, the second inside the lambda of the first, so the lines under the second run inside both. This prints `in`, then `after` twice.

```
fn main() ! console {
  with wrap()
  with wrap()
  print("in")
}
```

## Naming what the lambda is given

When the function gives its lambda a value, `with x = f(args)` names it. It means `f(args) fn(x) { ... }`, with the rest of the block inside the braces.

```
fn lend(name: str, body: fn(str) -> str) -> str { body(name) }

fn greeting() -> str ! alloc {
  with who = lend("Ana")
  "hello {who}"
}
```

`greeting()` is `hello Ana`.

Here `on_ok` is a function the page defines. It runs its lambda on what a `Result` holds, and only when it holds one. `Result` is the prelude's.

```
fn on_ok[T, U, E](r: Result[T, E], body: fn(T) -> Result[U, E]) -> Result[U, E] {
  match r {
    .ok(x) { body(x) }
    .err(e) { .err(e) }
  }
}
```

With it, a function that takes two steps reads in a line each:

```
fn parse_pair(a: str, b: str) -> Result[(i32, i32), ParseError] {
  with x = on_ok(parse_int(a))
  with y = on_ok(parse_int(b))
  .ok((x, y))
}
```

The body of `parse_pair` means exactly this:

```
on_ok(parse_int(a)) fn(x) {
  on_ok(parse_int(b)) fn(y) {
    .ok((x, y))
  }
}
```

If `parse_int(a)` is an `.err`, `on_ok` gives that `.err` without calling the lambda, so `y` is never parsed. [`try`](try.md) is not another way to write this: it leaves the function, and the lines after it are no lambda.

## The last parameter

`with` always fills a call's last parameter, so a final lambda parameter may come after parameters that have defaults, and a call can leave those out.

```
fn repeat(times: str = "two", body: fn(str) -> str) -> str { body(times) }

fn run() -> str {
  with x = repeat()
  x
}
```

`run()` is `two`, since `repeat()` uses the default. Naming one of the arguments sets it, and `with x = repeat("three")` makes `run()` give `three`.

## What is refused

`with` needs a call whose last parameter takes a function, and a lambda with as many parameters as that function type has.

- The call's last parameter takes no function.

  ```
  fn plain(a: str) -> str { a }

  fn run() -> str {
    with plain("a")
    "b"
  }
  ```

  This is refused: "a lambda after a call is for its last parameter, and `a`, the last parameter of `plain`, takes no function".
- What follows `with` is not a call.

  ```
  with "a"
  ```

  This is refused: "`with` passes the rest of the block to a call, and this is not one".
- The lambda that `with` makes takes a different number of parameters than the function type does. `with wrap()` where `wrap` gives its lambda a `str` is refused: "this lambda takes 0 parameters, where a `fn(str) -> str` is needed, which takes 1". `with x = wrap()` where `wrap` gives it nothing is refused: "this lambda takes 1 parameter, where a `fn() -> str` is needed, which takes 0".
- A name defined under `with` is assigned.

  ```
  with wrap()
  y = "a"
  y = "b"
  ```

  This is refused: "`y` is not a `var`, so it cannot be assigned to".

The lambda is inlined and cannot escape, so `with` costs nothing at run time.

## The forms

| Written | Means |
| --- | --- |
| `with f(a)` then lines | `f(a) { lines }`, a lambda that takes nothing |
| `with x = f(a)` then lines | `f(a) fn(x) { lines }` |
| `with f(a)` with nothing after | `f(a)` given a lambda with no lines |
| `with repeat()` | The lambda fills the last parameter, and the defaults before it are used |

Next: [`try`](try.md)
