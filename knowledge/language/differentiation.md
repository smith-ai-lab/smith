---
type: specification
title: Differentiation
description: grad, a function over code that returns the gradient of a function with respect to its first parameter.
status: planned
---

# Differentiation

`grad(f)` returns the gradient of `f` with respect to its first parameter. `grad` is a library [function over code](code.md), so it runs when the program is built and returns a new function. The gradient has the same type as that parameter.

```
fn square(x: f32) -> f32 { x * x }

square_grad = grad(square)

fn slope() -> f32 { square_grad(3.0) }
```

`slope()` is `6.0`. `square_grad` takes the arguments that `square` takes and gives the gradient with respect to `x`, an `f32` like `x`.

## The gradient of a struct

When the first parameter is a struct, the gradient is a struct of the same type.

```
struct Point {
  x: f32
  y: f32
}

fn norm_squared(p: Point) -> f32 { p.x * p.x + p.y * p.y }

point_grad = grad(norm_squared)
```

`point_grad(Point(x: 3.0, y: 4.0))` is `Point(x: 6.0, y: 8.0)`.

## Several values at once

`grad` differentiates with respect to the first parameter only, so to differentiate with respect to several values, put them in one struct or tuple. `norm_squared` above does it with two numbers in a `Point`.

To differentiate with respect to something else, `f` is wrapped in a lambda that takes it first. The compiler does not build this, and this page shows no example, as it does not say how the lambda is written.

## What grad reads

`grad` works on the typed core form of `f`, where every [`for`](for.md) is still visible, so a function written with `for` is differentiated through it.

```
fn sum_squares(v: [3]f32) -> f32 { sum(for k { v[k] ^ 2 }) }

sum_squares_grad = grad(sum_squares)
```

`sum_squares_grad([1.0, 2.0, 3.0])` is `[2.0, 4.0, 6.0]`.

## The forms

| Written | Is |
| --- | --- |
| `grad(f)` | The function that gives the gradient of `f` with respect to its first parameter |
| One struct or tuple as the first parameter | Differentiating with respect to several values |

Next: [Differentiable](differentiable.md)
