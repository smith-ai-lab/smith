---
type: specification
title: Array buffers
description: When an array has a buffer and when it fuses into the code that reads it, by one fixed rule about how each array is read.
status: planned
---

# Array buffers

Array code never asks for memory. The compiler decides which arrays need a buffer, by a fixed rule about how each one is read.

- **An array fuses into its consumer** when the consumer reads each element once. No buffer exists.
- **An array is stored** when its elements are read repeatedly, or when more than one consumer uses it.

## An array that fuses

```
fn squares_plus_five[n: Nat](xs: [n]f32) -> f32 {
  ys = for i { xs[i] * xs[i] }
  zs = for i { ys[i] + 5.0 }
  sum(zs)
}
```

This compiles to a single loop with no buffers, because `zs` reads each `ys[i]` once and `sum` reads each `zs[i]` once.

## An array that is stored

The layers of [Linear algebra](linear-algebra.md) show the other case in an MLP. Every output of the second layer reads every hidden value, so `hidden` is stored, and the `relu` fuses into the loop that fills it.

```
struct Mlp[i: Nat, h: Nat, o: Nat] {
  l1: Linear[i, h]
  l2: Linear[h, o]
}

fn forward[i: Nat, h: Nat, o: Nat](m: Mlp[i, h, o], x: [i]f32) -> [o]f32 {
  hidden = map(relu, apply(m.l1, x))
  apply(m.l2, hidden)
}
```

`apply(m.l2, hidden)` reads all of `hidden` for each of its `o` outputs, so `hidden` cannot be read once and dropped.

## Which, in a table

| How the array is read | Result |
| --- | --- |
| Each element once, by one consumer | Fused: no buffer |
| Elements read repeatedly | Stored |
| Used by more than one consumer | Stored |

Nothing in the source says which happens.

Next: [Arenas](arenas.md)
