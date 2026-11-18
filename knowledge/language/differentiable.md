---
type: specification
title: Differentiable
description: The Differentiable trait, through which each primitive supplies its derivative rule and a function may supply its own backward pass.
status: planned
---

# Differentiable

`Differentiable` is the trait through which [`grad`](differentiation.md) learns how to differentiate.

- Each primitive supplies its derivative rule through `Differentiable`.
- A function can supply its own backward pass through the same trait. It takes on the obligation that the pass equals the derivative.

A rule is an `impl` of `Differentiable` for the function, written with the function's name where a type goes, as [Traits](traits.md#an-impl-for-a-function) describes. Its `backward` takes what the function takes, and how much the result matters, and gives how much each argument matters.

```
impl Differentiable[sin] {
  fn backward(x: f32, d: f32) -> f32 { d * cos(x) }
}
```

For `sin`, an argument `x` matters `cos(x)` times as much as the result does. `grad` asks, at each call it meets, whether the function that is called has such an `impl`. Where it has, `grad` uses the rule. Where it has none, `grad` goes into the body of the function, which the name in the call holds, as [Functions over code](code.md#what-code-is) says. So `grad` knows the rule of no function, and a program adds a function with its own rule without changing `grad`.

Whoever writes a `backward` takes on the obligation that it equals the derivative. Nothing checks it.

How `Differentiable` itself is declared, so that one trait states a `backward` for functions of every number and type of arguments, is not settled, and a program cannot yet be written against it.

Next: [Vector](vector.md)
