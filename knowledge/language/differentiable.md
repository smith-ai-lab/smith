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
trait Differentiable[F, out Args, out R]
  requires Function[F, Args, R]
{
  fn backward(args: Args, d: R) -> Args
}

impl Differentiable[sin] {
  fn backward(args: Args, d: f32) -> Args { Args(x: d * cos(args.x)) }
}

impl Differentiable[mul] {
  fn backward(args: Args, d: f32) -> Args { Args(a: d * args.b, b: d * args.a) }
}
```

`Args` and `R` are what [`Function`](traits.md#an-impl-for-a-function) gives for the function: a struct whose fields are its parameters, and the type of its result. So one `backward` is declared for functions of every number of parameters. It takes the arguments and how much the result matters, and gives how much each argument matters, as a struct of the same fields, so each amount stands beside the name of the parameter it is for. `sin` is `fn sin(x: f32) -> f32` and `mul` is `fn mul(a: f32, b: f32) -> f32`, and inside an `impl` for a function `Args` is that function's.

For `sin`, an argument `x` matters `cos(x)` times as much as the result does. `grad` asks, at each call it meets, whether the function that is called has such an `impl`. Where it has, `grad` uses the rule. Where it has none, `grad` goes into the body of the function, which the name in the call holds, as [Functions over code](code.md#what-code-is) says. So `grad` knows the rule of no function, and a program adds a function with its own rule without changing `grad`.

Whoever writes a `backward` takes on the obligation that it equals the derivative. Nothing checks it.

How an argument that is not a number is held in that shape, such as a text or a whole number, which do not matter to a derivative, is not settled.

Next: [Vector](vector.md)
