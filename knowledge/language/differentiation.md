---
type: specification
title: Differentiation
description: grad as a compile-time call that returns the gradient of a function, shown by training an MLP.
---

# Differentiation

`grad(f)` is a compile-time call that returns a new function: the gradient of `f` with respect to its first parameter. The gradient has the same type as that parameter. To differentiate with respect to something else, wrap `f` in a lambda that takes it first, and to differentiate with respect to several values, put them in one struct or tuple. `derive Vector` gives a struct of float arrays element-wise arithmetic and scaling, which is what a gradient step needs.

```
derive Vector for Linear
derive Vector for Mlp

fn mse[n: Pos](pred: [n]f32, target: [n]f32) -> f32 {
  sum(for k { (pred[k] - target[k]) ^ 2 }) / float(n)
}

fn loss[b: Pos, i: Nat, h: Nat, o: Pos](m: Mlp[i, h, o], xs: [b, i]f32, ys: [b, o]f32) -> f32 {
  mean(for n { mse(forward(m, xs[n]), ys[n]) })
}

loss_grad = grad(loss)

fn sgd_step[b: Pos, i: Nat, h: Nat, o: Pos](
  m: Mlp[i, h, o],
  xs: [b, i]f32,
  ys: [b, o]f32,
  lr: f32,
) -> Mlp[i, h, o] {
  g = loss_grad(m, xs, ys)
  m - lr * g
}

fn train[n: Nat](data: Dataset[n, 784, 10], steps: Nat, key: take Key) -> Mlp[784, 256, 10]
  requires n >= 64
{
  var m = init_mlp(key)
  loop step in 0..steps {
    (xs, ys) = batch(data, step, 64)
    m := sgd_step(m, xs, ys, 0.01)
  }
  m
}
```

`Dataset`, `batch`, and `init_mlp` are library pieces not shown here.

- `grad` works on the typed core form of `f`, where every `for` is still visible. Each primitive supplies its derivative rule through a `Differentiable` trait.
- A function can supply its own backward pass through that trait, with the obligation that it equals the derivative.
- `train` is a pure function of the data, the step count, and the key, so its result can be reproduced.
- The weights in `train` are updated in place, because the old `m` is never read after each step.

Next: [Devices](devices.md)
