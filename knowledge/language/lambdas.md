---
type: specification
title: Lambdas
description: A function with no name, written where it is passed to a call.
status: partial
built: "lambdas passed to a call, after it or by name, and function types on parameters"
---

# Lambdas

A lambda is a function with no name, written where it is passed to a call.

A function that takes another function says so in a parameter's type. A function type is written `(T) -> U`: the types of the parameters, then the type of the result. `twice` takes a function from one float to another, and calls it two times.

```
fn twice(x: f32, f: (f32) -> f32) -> f32 { f(f(x)) }

fn quarter(x: f32) -> f32 {
  twice(x) fn(v) { v * 0.5 }
}
```

The lambda is `fn(v) { v * 0.5 }`: the word `fn`, the names of its parameters, and a block. Its value is the last expression of the block, and the types of its parameters come from the function type. It is the last argument of the call, and it is written after the call's `)`, on the same line.

A lambda uses the names of the function it is written in. This one uses `rate`:

```
fn taxed_twice(x: f32, rate: f32) -> f32 {
  twice(x) fn(v) { v * rate }
}
```

A lambda that takes no parameters is the block alone, `{ e }`.

```
fn unless(skip: bool, otherwise: f32, body: () -> f32) -> f32 {
  if skip { otherwise } else { body() }
}

fn fee(is_member: bool, w: f32, h: f32) -> f32 {
  unless(is_member, 0.0) { area(w, h) * 0.1 }
}
```

When the lambda is the only argument, the call has no parentheses. So a function can read like a part of the language. This one runs a few lines for their value:

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

Only the last argument is written after the call. A call that takes several lambdas passes the others by name, inside the parentheses.

```
fn pick(first: bool, a: () -> f32, b: () -> f32) -> f32 {
  if first { a() } else { b() }
}

fn longer_side(wide: bool, w: f32, h: f32) -> f32 {
  pick(wide, a: { w }) { h }
}
```

Braces are the body of a construct or a lambda, and nothing else.

- A block that stands alone is not an expression.
- After the condition of an `if`, the value of a `match`, the head of a `loop`, or a contract, `{` opens that construct's body. A call there takes a lambda after it only when the call is in parentheses.
- `try` inside a lambda returns from the lambda, and `break` does not reach a `loop` outside it.

A lambda may be passed to a call, and that is all. It cannot be stored in a field or a binding, and it cannot be returned, so it never outlives the function it is written in. This is why a lambda may assign that function's `var`s, and why it is always inlined.

Next: [Function values](function-values.md)
