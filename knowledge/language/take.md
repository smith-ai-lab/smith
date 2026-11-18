---
type: specification
title: Lending and take
description: A parameter lends its argument unless it is marked take, which gives the callee ownership, and so a random Key cannot be used twice.
status: planned
---

# Lending and take

Passing an argument lends it: the callee cannot keep, store, or return it. A parameter marked `take` receives ownership, and the caller cannot use the value again.

```
fn split(k: take Key) -> (Key, Key)
fn uniform(k: take Key) -> f32
```

`split` and `uniform` own the key they are given. A caller that has passed its `Key` to either has no key left.

## Lending

A parameter that is not marked `take` lends. The callee uses the value, and the caller still owns it afterwards.

```
fn keep(k: Key) -> Key { k }
```

This is refused: `k` is lent to `keep`, and a lent value cannot be returned. Written `fn keep(k: take Key) -> Key { k }` it is accepted, as `keep` owns `k`.

## Types that have Copy

For a type that has [`Copy`](ownership.md) the difference between lending and `take` is invisible.

## A Key is used at most once

A random `Key` has `Discard` and not `Copy`. Every random function takes one, so a key is spent by the function that uses it.

```
fn two_numbers(key: take Key) -> (f32, f32) {
  (k1, k2) = split(key)
  (uniform(k1), uniform(k2))
}
```

`split` turns one key into two, and each is used once. Using `k1` twice is refused.

```
fn same_twice(key: take Key) -> (f32, f32) {
  (k1, k2) = split(key)
  (uniform(k1), uniform(k1))
}
```

So a random key cannot be reused. Randomness needs no effect, because every random function takes a key. [Random](../library/random.md) lists them.

## The forms

| Written | Is |
| --- | --- |
| `fn f(x: T)` | `x` is lent: `f` cannot keep, store, or return it |
| `fn f(x: take T)` | `f` owns `x`, and the caller cannot use it again |

Next: [Values used exactly once](resources.md)
