---
type: specification
title: Training
description: A loss, its gradient, a step of gradient descent, and a loop of steps, shown by training an MLP.
status: planned
---

# Training

Training an MLP is four things in a row: a loss, its `grad`, a step that moves the weights against the gradient, and a loop of steps. It uses [`grad`](differentiation.md) and the [`Vector`](vector.md) of `Linear` and `Mlp`, with `forward` from [Array buffers](buffers.md).

## The loss

The loss of a batch is the mean over its rows of the squared error of the MLP on that row.

```
fn mse[n: Pos](pred: [n]f32, target: [n]f32) -> f32 {
  sum(for k { (pred[k] - target[k]) ^ 2 }) / float(n)
}

fn loss[b: Pos, i: Nat, h: Nat, o: Pos](m: Mlp[i, h, o], xs: [b, i]f32, ys: [b, o]f32) -> f32 {
  mean(for n { mse(forward(m, xs[n]), ys[n]) })
}
```

## Its gradient

The weights `m` are the first parameter of `loss`, so `grad(loss)` is the gradient with respect to them, an `Mlp`.

```
loss_grad = grad(loss)
```

## One step

A step subtracts the gradient, scaled by a learning rate, from the weights.

```
fn sgd_step[b: Pos, i: Nat, h: Nat, o: Pos](
  m: Mlp[i, h, o],
  xs: [b, i]f32,
  ys: [b, o]f32,
  lr: f32,
) -> Mlp[i, h, o] {
  g = loss_grad(m, xs, ys)
  m - lr * g
}
```

## Many steps

`train` takes a batch of the data for each step and starts from weights that a key makes.

```
fn train[n: Nat](data: Dataset[n, 784, 10], steps: Nat, key: take Key) -> Mlp[784, 256, 10]
  requires n >= 64
{
  var m = init_mlp(key)
  loop step in 0..steps {
    (xs, ys) = batch(data, step, 64)
    m = sgd_step(m, xs, ys, 0.01)
  }
  m
}
```

`Dataset`, `batch`, and `init_mlp` are library pieces not shown here.

- `train` is a pure function of the data, the step count, and the key, so its result can be reproduced. Called twice with the same data, steps, and a key made the same way, it gives the same weights.
- The weights in `train` are updated in place, because the old `m` is never read after each step.

## The forms

| Written | Is |
| --- | --- |
| `loss_grad = grad(loss)` | The gradient of the loss with respect to the weights |
| `m - lr * g` | One step of gradient descent |
| `loop step in 0..steps { m = sgd_step(...) }` | The steps, one batch each |

Next: [Devices](devices.md)
