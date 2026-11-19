---
type: specification
title: Pipeline
description: "`|>`, which writes a call after its first argument, so that a value passed through several calls reads in the order they run, `_` for another place, and `try` on a step."
status: built
---

# Pipeline

`x |> f(a, b)` is `f(x, a, b)`. The value on the left becomes the first argument of the call on the right. A value that is handed to one call after another is then written in the order the calls run.

```
fn put(w: take Out, b: u8) -> Out ! alloc

fn newline(w: take Out) -> Out ! alloc {
  w |> put('\\') |> put('n')
}
```

The body is `put(put(w, '\\'), 'n')`, written from the inside out. `|>` is another way to write a call and nothing more: what is checked, what is lent and taken, the effects, and the code that is built are those of the call it stands for.

A chain is read from the left, so `x |> f() |> g()` is `g(f(x))`. A line that starts with `|>` goes on from the line before it.

```
server = Server(name: "calc", version: "1")
  |> tool(add)
  |> tool(divide)
```

## Another place

An argument written `_` is where the value goes, when that is not the first place.

```
fn scale(by: f32, x: f32) -> f32 { x * by }

fn half(x: f32) -> f32 { x |> scale(0.5, _) }
```

`half(8.0)` is `scale(0.5, 8.0)`, which is `4.0`. With no `_`, the value is the first argument, so `x |> f(a)` and `x |> f(_, a)` are the same call. The `_` may be an argument with a name, as `x |> f(a, to: _)`, which is `f(a, to: x)`.

A call has at most one `_`.

```
fn twice(x: f32) -> f32 { x |> scale(_, _) }
```

This is refused: "a call has one `_`, and this is one more". The value on the left is one value, and writing it in two places would copy it without a word.

The `_` is a whole argument and no part of one.

```
fn wrong(x: f32) -> f32 { x |> scale(0.5, half(_)) }
```

This is refused: "`_` is a whole argument, and this one is inside another expression". The value goes to the call that `|>` stands before, which is `scale`, and a `_` inside another call would hide which call receives it. Written out, it is `x |> half() |> scale(0.5, _)`.

## What stands on the right

The right side is a call. Whatever can be called can stand there: a function by its name, a function of a module, as `xs |> list.push(1)`, and a call that ends with a [lambda](lambdas.md), as `xs |> each() fn(v) { print(v) }`.

```
fn wrong(x: f32) -> f32 { x |> half }
```

This is refused: "the right side of `|>` is not a call", with the fix "write `half()`". `half` is a function and not a call of one. It is written `x |> half()`.

## How it groups

`|>` binds more loosely than every other operator, so each side is a whole expression: `a + b |> f(c)` is `f(a + b, c)`. To use what a chain gives in a larger expression, the chain is put in parentheses or bound to a name.

## A step that can fail

[`try`](try.md) is written on the step it belongs to, before the call.

```
fn parse(text: str) -> Result[Point, ParseError]
fn scale(p: Point, by: i64) -> Point
fn check(p: Point) -> Result[Point, ParseError]

fn read(text: str) -> Result[Point, ParseError] {
  point = text |> try parse() |> scale(2) |> try check()
  .ok(point)
}
```

The first line of the body is `point = try check(scale(try parse(text), 2))`. Each `try` stands beside the call that can fail, and a step without one cannot leave the function.

`try` keeps the one meaning it has everywhere. Before the first value of a chain it covers that value alone, as it does before the left side of any operator, so `try a |> f()` is `f(try a)`.

## The forms

| Written | Is |
| --- | --- |
| `x \|> f(a, b)` | `f(x, a, b)` |
| `x \|> f(a, _, b)` | `f(a, x, b)` |
| `x \|> f(a, to: _)` | `f(a, to: x)` |
| `x \|> f() \|> g(a)` | `g(f(x), a)` |
| `x \|> try f(a)` | `try f(x, a)` |
| `a + b \|> f(c)` | `f(a + b, c)` |
| `x \|> f(_, _)`, `x \|> f(g(_))`, `x \|> f` | Refused |

Next: [`return`](return.md)
