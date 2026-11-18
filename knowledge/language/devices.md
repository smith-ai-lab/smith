---
type: specification
title: Devices
description: Running the same source on a GPU with with device(gpu), what the scope may hold, and where the data lives.
status: planned
---

# Devices

The same source runs on a CPU or a GPU. `with device(gpu)` compiles the rest of the block to GPU kernels, and a function that opens a device carries the [`gpu` effect](provided-effects.md).

```
fn doubled[n: Nat](xs: [n]f32) -> [n]f32 ! gpu {
  with device(gpu)
  for i { xs[i] * 2.0 }
}
```

The `for` is the rest of the block, so it is compiled to a kernel. A function with no `with device(gpu)` runs on the CPU, with the same source.

`train` of [Training](training.md) runs on the GPU in the same way, with no change to its source:

```
fn train_on_gpu[n: Nat](
  data: Dataset[n, 784, 10],
  steps: Nat,
  key: take Key,
) -> Mlp[784, 256, 10] ! gpu
  requires n >= 64
{
  with device(gpu)
  train(data, steps, key)
}
```

## What the scope may hold

Code inside the scope must have no `alloc` or I/O effects, no recursion other than a [tail call](recursion.md), and only fixed-size data. Anything else is a compile error that names the reason. None of these refusals is built, so no words are quoted.

- An `alloc` or I/O effect is refused: the scope may have none. `print` carries `console`, which is I/O.

  ```
  fn noisy(xs: [3]f32) -> [3]f32 ! gpu, console {
    with device(gpu)
    print("running")
    for i { xs[i] * 2.0 }
  }
  ```
- Recursion that is not a tail call is refused. `power` of [Recursion](recursion.md) may run in the scope, and `power_slow` may not. `power_slow` also carries `alloc`.

  ```
  fn cubed(x: f32) -> f32 ! gpu, alloc {
    with device(gpu)
    power_slow(x, 3)
  }
  ```
- Data of a size known only at run time is refused: only fixed-size data may be in the scope. A `List` is such data.

  ```
  fn collected(xs: [3]f32) -> List[f32] ! gpu, alloc {
    with device(gpu)
    push([], xs[0])
  }
  ```

## Where the data lives

On Apple silicon the CPU and the GPU share memory, so entering the scope copies nothing. Elsewhere, values are moved to the device on entry and the results are moved back on exit. For `doubled`, `xs` is moved on entry and the array it gives on exit, except on Apple silicon, where nothing is copied.

## The forms

| Written | Is |
| --- | --- |
| `with device(gpu)` | The rest of the block runs on the GPU |
| `! gpu` | The signature of a function that opens a device |

Next: [Tasks](tasks.md)
