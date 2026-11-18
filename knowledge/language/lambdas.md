---
type: specification
title: Lambdas
description: A function with no name, written where it is passed to a call.
status: partial
built:
  - "lambdas passed to a call, after it or by name, and function types on parameters"
  - "a lambda that writes the types of its parameters and of its result"
  - "a lambda that gives away a value of the function it is written in, passed to a parameter that takes its function"
---

# Lambdas

A lambda is a function with no name, written where it is passed to a call.

## One function, written three ways

A lambda is a function without its name, and a function type is a function without the names of its parameters and without its block.

```
// a function
fn double(v: f32) -> f32 { v * 2.0 }

// a lambda: the function without its name
fn(v: f32) -> f32 { v * 2.0 }

// its type: without the parameters' names and the block
fn(f32) -> f32
```

## A function that takes a function

A function says that it takes another function in a parameter's type. A function type is written `fn(T) -> U`: the types of the parameters in parentheses, then the type of the result. The parameter is called like any function.

```
fn twice(x: f32, f: fn(f32) -> f32) -> f32 { f(f(x)) }
```

`twice` takes a float and a function from one float to another, and calls the function two times.

A function that gives nothing has no arrow, as `fn(f32)`, and one that takes nothing is `fn() -> f32`. The effects of a function type follow its result, as a function writes them, `fn(f32) -> f32 ! console`, and a parameter the function owns is `fn(take f32) -> f32`. A list of types in parentheses without `fn` is always a [tuple](tuples.md).

## Writing a lambda

A lambda is the word `fn`, the names of its parameters in parentheses, and a block. It is written after the call's `)`, on the same line, and it is the call's last argument.

```
fn quarter(x: f32) -> f32 {
  twice(x) fn(v) { v * 0.5 }
}
```

Here `fn(v) { v * 0.5 }` is the `f` of `twice`, so `quarter(8.0)` is `2.0`.

- The types of a lambda's parameters and of its result are those of the function type it is passed as, so `v` is an `f32`. A lambda may write them as a function does, `fn(v: f32) -> f32 { v * 0.5 }`, each one or none. A type that is written and is not the one the function type needs is refused at the type.
- A lambda uses the type parameters of the function it is written in, and may write its own, as [Where a lambda may go](#where-a-lambda-may-go) shows. When nothing decides the type of its parameters, it writes them.
- A lambda has as many parameters as that function type. `twice(x) fn(a, b) { a }` is refused: "this lambda takes 2 parameters, where a `fn(f32) -> f32` is needed, which takes 1".
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
fn combine(a: f32, b: f32, f: fn(f32, f32) -> f32) -> f32 { f(a, b) }

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

A lambda may give away a value of the function it is written in, as an argument of a `take` parameter. It can then be called once, and is passed only to a parameter that takes its function, as [A function that is called once](take.md#a-function-that-is-called-once) says.

## A lambda with no parameters

A lambda that takes no parameters is the block alone, `{ e }`, with no `fn` and no parentheses.

```
fn unless(skip: bool, otherwise: f32, body: fn() -> f32) -> f32 {
  if skip { otherwise } else { body() }
}

fn fee(is_member: bool, w: f32, h: f32) -> f32 {
  unless(is_member, 0.0) { area(w, h) * 0.1 }
}
```

## A lambda as the only argument

When the lambda is the only argument, the call has no parentheses. So a function can read like a part of the language. `do` runs a few lines for their value.

```
fn do[T](body: fn() -> T) -> T { body() }

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
fn at_five(f: fn(f32) -> f32) -> f32 { f(5.0) }

fn ten() -> f32 { at_five fn(v) { v * 2.0 } }
```

## Several lambdas in one call

Only the last argument is written after the call. A call that takes several lambdas passes the others by name, inside the parentheses.

```
fn pick(first: bool, a: fn() -> f32, b: fn() -> f32) -> f32 {
  if first { a() } else { b() }
}

fn longer_side(wide: bool, w: f32, h: f32) -> f32 {
  pick(wide, a: { w }) { h }
}
```

## Where a lambda is written

Braces are the body of a construct or a lambda, and nothing else. Four rules follow.

- **A lambda begins on the line where the call's arguments end.** On the next line it is not part of the call, and a block there is refused, as a block that stands alone is not an expression.
- **The last lambda goes after the call.** Written inside the parentheses, as in `twice(x, fn(v) { v * 0.5 })`, it means the same, and the formatter moves it after the call, so a program has one form.
- **In the head of an `if`, a `match`, or a `loop`, `{` opens that construct's body.** So `if any { true } { ... }` is refused, since the first `{` is taken as the body of the `if`. A call that takes a lambda there is put in parentheses.

  ```
  fn any(f: fn() -> bool) -> bool { f() }

  fn sign(x: f32) -> f32 {
    if (any { x > 0.0 }) { 1.0 } else { -1.0 }
  }
  ```
- **A block that stands alone is not an expression.** `x = { 1.0 }` is refused. A few lines that give a value are passed to a function, as to `do` above.

## Where a lambda may go

A lambda is a value, so it goes where a value goes: it is passed to a call, bound to a name, stored in a field, and returned. One rule decides which lambdas may leave the function they are written in, and it is the rule for every value: [a lent value is not stored or returned](take.md#lending).

A lambda is lent the names around it that it reads or assigns. So a lambda that reads or assigns a name of its function holds something lent, and it is passed to a parameter of a function type and nowhere else. The function that receives it may call it, or pass it on to such a parameter of another function, and may not store or return it, whether its parameter lends the lambda or takes it. So the lambda never outlives the names it uses, and what it does happens inside the call it is written at.

```
fn scaled(x: f32, rate: f32) -> f32 {
  twice(x) fn(v) { v * rate }
}
```

A lambda that uses no name around it, or only names it [takes](take.md#a-function-that-is-called-once), holds nothing lent, and may be kept. Here `halve` is bound, then passed.

```
fn quarter(x: f32) -> f32 {
  halve = fn(v: f32) -> f32 { v * 0.5 }
  twice(x, halve)
}
```

A lambda may write type parameters in brackets after `fn`, as a function writes them after its name, and each use settles them, as each call of a function does.

```
fn both() -> (i64, str) {
  same = fn[T](x: T) -> T { x }
  (same(1), same("a"))
}
```

`both()` is `(1, "a")`: `T` is `i64` in the first call and `str` in the second. A lambda passed where a function type is needed has its type parameters settled by that type. No parameter asks for a function whose type parameters are still open, so no function type writes brackets.

A lambda that uses no name around it is a function with a name in all but the name, so it goes where a [function value](function-values.md) goes, as into a field whose type is a function type. A lambda that takes names holds them, and has a type of its own, so a field that holds it has a type parameter for it. A type parameter never stands for a lambda that holds something lent.

```
struct Step[F] {
  run: F
}
```

A lambda that is bound, stored, or returned is written with `fn`, also when it takes nothing: `fn() { 1.0 }`. The block alone is a lambda only as the argument of a call, so `x = { 1.0 }` is still refused, as a block that stands alone is not an expression.

Binding, storing, and returning a lambda, and a lambda's own type parameters, are not built, so every example of this section but the first is refused today, and no words are quoted.

A lambda that is passed to a call is inlined: it costs what its block costs, written where the function calls it.

Two keywords of later pages stop at a lambda. [`try`](try.md) inside a lambda returns from the lambda, and `break` does not reach a [`loop`](loop.md) outside it.

## The forms

| Written | Is |
| --- | --- |
| `f(a) fn(x) { e }` | A lambda that takes `x`, as the last argument of `f` |
| `f(a) fn(x, y) { e }` | A lambda that takes `x` and `y` |
| `f(a) fn(x: T) -> R { e }` | A lambda that writes the type of `x` and of its result |
| `fn(T) -> R` | The type of a function that takes a `T` and gives an `R` |
| `f(a) { e }` | A lambda that takes nothing |
| `f { e }` and `f fn(x) { e }` | A lambda as the only argument |
| `f(a, g: { e }) { e2 }` | Two lambdas: `g` by name, and the last after the call |

Next: [Function values](function-values.md)
