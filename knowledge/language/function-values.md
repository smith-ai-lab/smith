---
type: specification
title: Function values
description: A function with a name used as a value, which can be passed, stored, and returned.
status: partial
built: "a function with a name passed to a call"
---

# Function values

A function with a name is a value. Its name is passed wherever a function type is expected.

```
fn half(x: f32) -> f32 { x * 0.5 }

fn twice(x: f32, f: (f32) -> f32) -> f32 { f(f(x)) }

fn quarter(x: f32) -> f32 { twice(x, half) }
```

`half` is written as it is named, with no parentheses, and nothing is called. `twice` receives it as `f`, so `quarter(8.0)` is `2.0`.

## What a name is worth as a value

A function's type is the types of its parameters and of its result, written as in [Lambdas](lambdas.md). The type of `half` is `(f32) -> f32`, and a name is accepted where that type is expected and nowhere else.

```
fn positive(x: f32) -> bool { x > 0.0 }

fn wrong(x: f32) -> f32 { twice(x, positive) }
```

This is refused: "this gives a `(f32) -> bool`, where a `(f32) -> f32` is needed". A function that takes two floats is refused the same way, as `(f32, f32) -> f32`.

Writing the call instead of the name passes what the call gives. `twice(x, half(1.0))` is refused: "this gives a `f32`, where a `(f32) -> f32` is needed".

## A function that needs more than it is given

A function value is called with the arguments of its type and no others, so the function must take exactly those.

```
fn off(x: f32, rate: f32) -> f32 { x - rate }

fn price(x: f32, rate: f32, discount: (f32, f32) -> f32) -> f32 {
  discount(x, rate)
}
```

`price(10.0, 3.0, off)` is `7.0`. The function that receives `off` is the one that holds `rate` and passes it, since the value `off` holds nothing of its own. Nothing is captured: a function sees its parameters and what it defines, and nothing of the function it is passed from.

```
fn rated(x: f32) -> f32 { x - rate }
```

This is refused: "there is no name `rate` defined before this in `rated`". A [lambda](lambdas.md) is the way to use the names around a call, and a function with a name is the way to pass something that stands alone.

When a stored function needs data to work with, the data is stored beside it and passed to it, as `rate` is above.

## Defaults

A default belongs to a function's name and not to its type, so a function passed as a value takes every argument.

```
fn scale(x: f32, by: f32 = 2.0) -> f32 { x * by }
```

The type of `scale` is `(f32, f32) -> f32`, so `twice(8.0, scale)` is refused: "this gives a `(f32, f32) -> f32`, where a `(f32) -> f32` is needed". A lambda gives the default, since the call inside it is an ordinary call.

```
fn quarter_scale(x: f32) -> f32 { twice(x) fn(v) { scale(v) } }
```

## Storing and returning a function

Unlike a lambda, a function with a name can be stored and returned, because it holds nothing that belongs to another function.

```
struct Pricing {
  discount: (f32) -> f32
}

sale = Pricing(discount: half)
```

A function that returns a function says so in its return type, which is a function type.

```
fn chosen() -> (f32) -> f32 { half }
```

## The forms

| Written | Is |
| --- | --- |
| `half` | The function named `half`, as a value of type `(f32) -> f32` |
| `half(1.0)` | A call, which gives what `half` gives, not a function |
| `twice(x, half)` | A function with a name passed to a call |
| `discount: half` | A function with a name stored in a field |
| `-> (f32) -> f32` | A return type that is a function |
| `fn(v) { scale(v) }` | A lambda, the way to pass a function that has defaults |

Next: [`with`](with.md)
