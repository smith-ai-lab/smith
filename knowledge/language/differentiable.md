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

This page shows no example. Which functions the trait holds, and how an `impl` of it is written, are not said anywhere, so a program cannot yet be written against it.

Next: [Vector](vector.md)
