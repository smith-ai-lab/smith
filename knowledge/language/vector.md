---
type: specification
title: Vector
description: derive Vector, which gives a struct of float arrays addition, subtraction, and scaling by a number.
status: planned
---

# Vector

`derive Vector for T` gives a struct of float arrays addition, subtraction, and scaling by a number. A gradient step needs exactly these, so the gradient of a struct with a `Vector` can be subtracted from it.

```
struct Linear[i: Nat, o: Nat] {
  w: [o, i]f32
  b: [o]f32
}

derive Vector for Linear
```

With it, two `Linear`s of one size are added and subtracted, and a number scales one. `a + b`, `a - b`, and `2.0 * a` are each a `Linear`.

## A struct that holds structs

`Mlp` holds two `Linear`s. Both are derived.

```
struct Mlp[i: Nat, h: Nat, o: Nat] {
  l1: Linear[i, h]
  l2: Linear[h, o]
}

derive Vector for Linear
derive Vector for Mlp
```

A step of gradient descent is then one line, with `g` the gradient of the loss at `m` and `lr` a number: `m - lr * g`, as [Training](training.md) writes it. The number is written before the struct.

## The forms

| Written | Is |
| --- | --- |
| `derive Vector for T` | `+`, `-`, and a number times a `T` |

Next: [Training](training.md)
