---
type: specification
title: Random arrays
description: A random function fills a whole array from one key, split_n gives each iteration of a for a key of its own, and reading keys[i] is allowed when the loop's own index picks the key.
status: planned
---

# Random arrays

Random arrays follow the key rule from [Lending and take](take.md): a random `Key` is used at most once. A random function fills a whole array from one key, and the expected type gives the shape, as it does for a literal.

```
struct Linear[i: Nat, o: Nat] {
  w: [o, i]f32
  b: [o]f32
}

fn init_linear[i: Nat, o: Nat](key: take Key) -> Linear[i, o] {
  (kw, kb) = split(key)
  Linear(w: normal(kw), b: uniform(kb))
}
```

`Linear` is the layer of [Linear algebra](linear-algebra.md). `normal(kw)` fills the `[o, i]f32` that the field `w` expects, and `uniform(kb)` the `[o]f32` of `b`. Each uses one key, and `split` made two from one.

## A key for each iteration

When each iteration needs its own key, `split_n` turns one key into an array of keys.

```
fn dropout[n: Nat](x: [n]f32, rate: f32, key: take Key) -> [n]f32 {
  keys = split_n(key)
  for i {
    if uniform(keys[i]) < rate { 0.0 } else { x[i] }
  }
}
```

`keys` has `n` keys, since the `for` is written for the result `[n]f32`, and `i` takes its size from that type, as [`for`](for.md#where-an-index-gets-its-size) says.

## Reading `keys[i]`

Reading `keys[i]` uses up that key. It is allowed inside a `for` when the body takes `keys[i]` at most once and `i` is the loop's own index, so every iteration touches a different key. `dropout` does.

Key `i` is computed directly from the parent key and `i`, so the array of keys never exists in memory and the loop still runs in parallel.

A body that takes `keys[i]` twice is refused, since the second use is of a key already used.

```
fn twice[n: Nat](key: take Key) -> [n]f32 {
  keys = split_n(key)
  for i { uniform(keys[i]) + uniform(keys[i]) }
}
```

An index that is not the loop's own is refused, since every iteration would touch the same key.

```
fn same[n: Nat](key: take Key) -> [n]f32 {
  keys = split_n(key)
  for i { uniform(keys[0]) }
}
```

The compiler does not build either refusal, so the page quotes no words.

## The forms

| Written | Is |
| --- | --- |
| `normal(k)`, `uniform(k)` where an array is expected | The whole array, from one key, shaped by the type |
| `split_n(k)` | An array of keys, one for each index |
| `keys[i]` in `for i` taken once | Allowed: each iteration has its own key |
| `keys[i]` taken twice | Refused |
| `keys[0]`, or any index but the loop's own | Refused |

Next: [`loop`](loop.md)
