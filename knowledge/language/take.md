---
type: specification
title: Lending and take
description: A parameter lends its argument unless it is marked take, which gives the callee ownership, and so a random Key cannot be used twice.
status: partial
built:
  - "`take` before a parameter's type, and before a parameter of a function type, which a lambda passed to it is held to"
  - "A lent value, or a part of one, refused where it is returned, stored, or passed to a `take` parameter, unless its type has `Copy`"
  - "An argument of a `take` parameter given away, so that a use of it after the call is refused"
  - "not a value without `Copy` given away inside a lambda that the lambda did not define or take, which is refused, as nothing says how often a lambda is called"
  - "not a function's type parameter given a type without `Copy`, nor an implementation chosen for one, which are refused, as no bound says that a type parameter has `Copy`"
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

## A function that is called once

A function is a value like any other, and calling it takes it. So how often a function may be called is said by the two traits that say it of every value: one that has [`Copy`](ownership.md) is called again and again, and one that has not is spent by its first call.

A lambda is lent the names around it that it reads or assigns, and takes the ones it gives away. A lambda that takes a value without `Copy` has no `Copy` itself. This one gives `acc` to `append`, so it can be called once.

```
fn finish(xs: take List[u8], f: take (take List[u8]) -> u64) -> u64 { f(xs) }

fn collect(acc: take List[u8], xs: take List[u8]) -> u64 {
  finish(xs) fn(ys) { append(acc, ys).size }
}
```

`finish` marks `f` with `take`, so it owns the function and its one call spends it. A second `f(xs)` in `finish` is refused, as a second use of any value that was given away is.

A parameter that is not marked `take` lends the function, as it lends any value, and a lent value is only taken when it has `Copy`. So a lent function is one that can be called again, and a lambda that gives a value away is refused there.

```
fn twice(xs: take List[u8], f: (take List[u8]) -> u64) -> u64 { f(xs) }

fn collect(acc: take List[u8], xs: take List[u8]) -> u64 {
  twice(xs) fn(ys) { append(acc, ys).size }
}
```

This is refused: `twice` may call its function any number of times, and the lambda can give `acc` away once. Ownership of a lambda's names is not built, so no words are quoted.

A lambda that takes a value without [`Discard`](resources.md) has no `Discard` either, so the function it is given to must call it: a function that must run exactly once needs no word of its own.

The short form of a function type says one of two things, by whether its parameter is lent or taken. Anything else is said with a type parameter and `requires`, as for any type: `Function[F, Args, R]` for a function that is called once, and `Copy[F]` beside it for one that is called again.

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
| `f: (T) -> R` | `f` is lent: it is called any number of times, and is a function that has `Copy` |
| `f: take (T) -> R` | The callee owns `f`, and calling it spends it |

Next: [Values used exactly once](resources.md)
