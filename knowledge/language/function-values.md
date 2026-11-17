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

fn quarter(x: f32) -> f32 { twice(x, half) }
```

Unlike a [lambda](lambdas.md), it can also be stored and returned, because it holds nothing that belongs to another function.

```
struct Pricing {
  discount: (f32) -> f32
}

sale = Pricing(discount: half)
```

- When a stored function needs data to work with, the data is stored beside it and passed to it. Nothing is captured.
- A default belongs to a function's name and not to its type, so a function passed as a value takes every argument.

Next: [`with`](with.md)
