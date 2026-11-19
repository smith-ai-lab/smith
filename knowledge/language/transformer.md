---
type: specification
title: A transformer
description: A decoder-only transformer in under a hundred lines, built from everything before it.
status: planned
---

# A transformer

Everything in the files before this one combines into a decoder-only transformer in under a hundred lines. This version uses one attention head to keep it short.

```
fn softmax[n: Pos](x: [n]f32) -> [n]f32 {
  top = max(x)
  z = sum(for i { exp(x[i] - top) })
  for i { exp(x[i] - top) / z }
}

struct Norm[d: Nat] {
  gain: [d]f32
  bias: [d]f32
}

fn layer_norm[d: Pos](n: Norm[d], x: [d]f32) -> [d]f32 {
  mu = mean(x)
  spread = mean(for c { (x[c] - mu) ^ 2 })
  for c { n.gain[c] * (x[c] - mu) / sqrt(spread + 1e-5) + n.bias[c] }
}
```

Attention has a binding for each intermediate. The causal mask is an `if` on the indices, not a separate array, and `inf` is float infinity.

```
struct Attention[d: Nat] {
  q: Linear[d, d]
  k: Linear[d, d]
  v: Linear[d, d]
  o: Linear[d, d]
}

fn attend[t: Pos, d: Pos](a: Attention[d], x: [t, d]f32) -> [t, d]f32 {
  q = for i { apply(a.q, x[i]) }
  k = for i { apply(a.k, x[i]) }
  v = for i { apply(a.v, x[i]) }
  scale = 1.0 / sqrt(float(d))
  scores = for i, j {
    if j <= i { q[i] @ k[j] * scale } else { -inf }
  }
  w = for i { softmax(scores[i]) }
  mixed = for i, c { sum(for j { w[i, j] * v[j, c] }) }
  for i { apply(a.o, mixed[i]) }
}
```

A block is pre-norm attention and a feed-forward layer, each with a residual connection. The feed-forward layer is the MLP's `forward`, reused unchanged.

```
struct Block[d: Nat, h: Nat] {
  n1: Norm[d]
  attn: Attention[d]
  n2: Norm[d]
  ff: Mlp[d, h, d]
}

fn block[t: Pos, d: Pos, h: Nat](b: Block[d, h], x: [t, d]f32) -> [t, d]f32 {
  normed = for i { layer_norm(b.n1, x[i]) }
  y = x + attend(b.attn, normed)
  renormed = for i { layer_norm(b.n2, y[i]) }
  y + for i { forward(b.ff, renormed[i]) }
}
```

The model embeds tokens, runs the blocks in order, and projects to the vocabulary. Token ids have type `0..v`, so the embedding lookup needs no bounds check.

```
struct Gpt[v: Nat, t: Nat, d: Nat, h: Nat, l: Nat] {
  tok: [v, d]f32
  pos: [t, d]f32
  blocks: [l]Block[d, h]
  out: Linear[d, v]
}

fn logits[v: Nat, t: Pos, d: Pos, h: Nat, l: Nat](m: Gpt[v, t, d, h, l], ids: [t]0..v) -> [t, v]f32 {
  var x = for i, c { m.tok[ids[i], c] + m.pos[i, c] }
  loop n in 0..l { x = block(m.blocks[n], x) }
  for i { apply(m.out, x[i]) }
}
```

No line of this says where memory comes from or how loops run. The compiler stores each intermediate that is read repeatedly, fuses the rest, and compiles the contractions as blocked kernels. Training follows [Training](training.md): a cross-entropy loss over `logits`, its `grad`, and a step function, inside `with device(gpu)`.
