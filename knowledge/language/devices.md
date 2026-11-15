---
type: specification
title: Devices
description: Running the same source on a GPU with with device(gpu).
---

# Devices

The same source runs on a CPU or a GPU. `with device(gpu)` compiles the rest of the block to GPU kernels.

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

- Code inside the scope must have no `alloc` or I/O effects, no recursion, and only fixed-size data. Anything else is a compile error that names the reason.
- On Apple silicon the CPU and GPU share memory, so entering the scope copies nothing. Elsewhere, values are moved to the device on entry and results are moved back on exit.

Next: [Tasks and async](tasks.md)
