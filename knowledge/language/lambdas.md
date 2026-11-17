---
type: specification
title: Lambdas
description: A function with no name, written where it is passed to a call.
status: partial
built: "lambdas passed to a call, after it or by name, and function types on parameters"
---

# Lambdas

A lambda is a function with no name, written where it is passed to a call.

## A function that takes a function

A function says that it takes another function in a parameter's type. A function type is written `(T) -> U`: the types of the parameters in parentheses, then the type of the result. The parameter is called like any function.

```
fn twice(x: f32, f: (f32) -> f32) -> f32 { f(f(x)) }
```

`twice` takes a float and a function from one float to another, and calls the function two times.

## Writing a lambda

A lambda is the word `fn`, the names of its parameters in parentheses, and a block. It is written after the call's `)`, on the same line, and it is the call's last argument.

```
fn quarter(x: f32) -> f32 {
  twice(x) fn(v) { v * 0.5 }
}
```

Here `fn(v) { v * 0.5 }` is the `f` of `twice`, so `quarter(8.0)` is `2.0`.

- The types of a lambda's parameters and of its result are never written. They are those of the function type it is passed as, so `v` is an `f32`.
- A lambda has as many parameters as that function type. `twice(x) fn(a, b) { a }` is refused: "this lambda takes 2 parameters, where a `(f32) -> f32` is needed, which takes 1".
- The value of a lambda is the last expression of its block, as for a function. Its block may have several lines.

```
fn nudge(x: f32) -> f32 {
  twice(x) fn(v) {
    half = v * 0.5
    half + 1.0
  }
}
```

A lambda with several parameters names each.

```
fn combine(a: f32, b: f32, f: (f32, f32) -> f32) -> f32 { f(a, b) }

fn total(a: f32, b: f32) -> f32 {
  combine(a, b) fn(x, y) { x + y }
}
```

## Using what is around it

A lambda uses the names of the function it is written in. This one uses `rate`.

```
fn taxed_twice(x: f32, rate: f32) -> f32 {
  twice(x) fn(v) { v * rate }
}
```

It may assign that function's [`var`s](var.md) too. This counts how many times `twice` calls it, and gives `2.0`.

```
fn calls(x: f32) -> f32 {
  var count = 0.0
  twice(x) fn(v) {
    count = count + 1.0
    v
  }
  count
}
```

## A lambda with no parameters

A lambda that takes no parameters is the block alone, `{ e }`, with no `fn` and no parentheses.

```
fn unless(skip: bool, otherwise: f32, body: () -> f32) -> f32 {
  if skip { otherwise } else { body() }
}

fn fee(is_member: bool, w: f32, h: f32) -> f32 {
  unless(is_member, 0.0) { area(w, h) * 0.1 }
}
```

## A lambda as the only argument

When the lambda is the only argument, the call has no parentheses. So a function can read like a part of the language. `do` runs a few lines for their value.

```
fn do[T](body: () -> T) -> T { body() }

fn rect_area(left: f32, right: f32, top: f32, bottom: f32) -> f32 {
  do {
    w = right - left
    h = bottom - top
    w * h
  }
}
```

The same holds for a lambda with parameters.

```
fn at_five(f: (f32) -> f32) -> f32 { f(5.0) }

fn ten() -> f32 { at_five fn(v) { v * 2.0 } }
```

## Several lambdas in one call

Only the last argument is written after the call. A call that takes several lambdas passes the others by name, inside the parentheses.

```
fn pick(first: bool, a: () -> f32, b: () -> f32) -> f32 {
  if first { a() } else { b() }
}

fn longer_side(wide: bool, w: f32, h: f32) -> f32 {
  pick(wide, a: { w }) { h }
}
```

## Where a lambda is written

Braces are the body of a construct or a lambda, and nothing else. Four rules follow.

- **A lambda begins on the line where the call's arguments end.** On the next line it is not part of the call, and it is refused: "a lambda that stands alone is not an expression, as it is only passed to a call".
- **The last lambda goes after the call.** Written inside the parentheses, as in `twice(x, fn(v) { v * 0.5 })`, it means the same, and the formatter moves it after the call, so a program has one form.
- **In the head of an `if`, a `match`, or a `loop`, `{` opens that construct's body.** So `if any { true } { ... }` is refused, since the first `{` is taken as the body of the `if`. A call that takes a lambda there is put in parentheses.

  ```
  fn any(f: () -> bool) -> bool { f() }

  fn sign(x: f32) -> f32 {
    if (any { x > 0.0 }) { 1.0 } else { -1.0 }
  }
  ```
- **A block that stands alone is not an expression.** `x = { 1.0 }` is refused: "a lambda cannot be bound to a name, as it is only passed to a call". A few lines that give a value are passed to a function, as to `do` above.

## What a lambda cannot do

A lambda is passed to a call, and that is all.

- It cannot be bound to a name. `f = fn(v) { v * 0.5 }` is refused with the message above.
- It cannot be stored in a field, and it cannot be returned.

So a lambda never outlives the function it is written in. That is why it may assign that function's `var`s, and why it is always inlined: a lambda costs what its block costs, written where the function calls it. A function that must be kept is one with a name, under [Function values](function-values.md).

Two keywords of later pages stop at a lambda. [`try`](try.md) inside a lambda returns from the lambda, and `break` does not reach a [`loop`](loop.md) outside it.

## The forms

| Written | Is |
| --- | --- |
| `f(a) fn(x) { e }` | A lambda that takes `x`, as the last argument of `f` |
| `f(a) fn(x, y) { e }` | A lambda that takes `x` and `y` |
| `f(a) { e }` | A lambda that takes nothing |
| `f { e }` and `f fn(x) { e }` | A lambda as the only argument |
| `f(a, g: { e }) { e2 }` | Two lambdas: `g` by name, and the last after the call |

Next: [Function values](function-values.md)
