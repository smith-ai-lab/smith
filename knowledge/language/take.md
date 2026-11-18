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

No part of a lent value is returned either, and there is no exception. The check is made inside one function at a time, and no result is tied to how long an argument lasts.

## A function that hands on what it is given

A function that gives back what it was passed, or passes it to a lambda that keeps it, takes it. A function type marks a parameter `take` as a function does. `then`, which [`try`](try.md) is made of, is such a function: it gives the failure back and passes the value to its lambda.

```
trait Try[C, R, out T] {
  fn then(c: take C, body: (take T) -> R) -> R
}
```

A `try` gives `then` a value nothing else holds, so no call changes. And the lines after `with t = then(begin(db))` own `t`, so they may `commit` it. Until ownership is built, `Try` is written without `take`, as [try](try.md) shows it.

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
| `(take T) -> R` | A function type whose function owns its argument |

Next: [Values used exactly once](resources.md)
