---
type: specification
title: Lending and take
description: A parameter lends its argument unless it is marked take, which gives the callee ownership, and so a random Key cannot be used twice.
status: partial
built:
  - "`take` before a parameter's type, and before a parameter of a function type, which a lambda passed to it is held to"
  - "A lent value, or a part of one, refused where it is returned, stored, or passed to a `take` parameter, unless its type has `Copy`"
  - "An argument of a `take` parameter given away, so that a use of it after the call is refused"
  - "a lambda that gives away a value of the function it is written in, passed to a parameter that takes its function, which spends the value from the call, and refused with one error at the lambda where the parameter lends its function"
  - "a function marked `take` is spent by its first call or by being passed to a parameter that takes its function, so a second call, on any path or in a later round of a loop, is refused"
  - "a lambda that gives away a value without `Discard` is refused where the function it is passed to takes its function, as that function may not call it"
  - "a function that is lent is called any number of times, and is passed to a parameter that lends its function and not to one that takes it"
  - "a type parameter has `Copy` and `Discard` only where a bound or a `requires` line asks, so a value of one is used at most once and must be taken, and a call, or an implementation it chooses, is given only a type that has what the bounds ask"
---

# Lending and take

Passing an argument lends it: the callee cannot keep, store, or return it. A parameter marked `take` receives ownership, and the caller cannot use the value again.

```
fn split(k: take Key) -> (Key, Key)
fn uniform(k: take Key) -> f32
```

`split` and `uniform` own the key they are given. A caller that has passed its `Key` to either has no key left.

A function that changes one value and gives nothing else takes it and returns it, and the caller assigns the result to the same name, as `xs = push(xs, 4)`. Nothing is copied. When the functions give other results too, or may fail, the value is state for a [handler](handlers.md).

## Lending

A parameter that is not marked `take` lends. The callee uses the value, and the caller still owns it afterwards.

```
fn keep(k: Key) -> Key { k }
```

This is refused: `k` is lent to `keep`, and a lent value cannot be returned. Written `fn keep(k: take Key) -> Key { k }` it is accepted, as `keep` owns `k`.

No part of a lent value is returned either, and there is no exception. The check is made inside one function at a time, and no result is tied to how long an argument lasts.

## A function that hands on what it is given

A function that gives back what it was passed, or passes it to a lambda that keeps it, takes it. A function type marks a parameter `take` as a function does. `lend_once` passes its key to its lambda, which keeps it, so the parameter of the lambda's type is marked.

```
use std.bits (u64)

fn lend_once(k: take Key, body: fn(take Key) -> u64) -> u64 { body(k) }
```

A call of `lend_once` gives it a key nothing else holds, so the caller may not use it again. And the lines after `with k = lend_once(key)` own `k`, so they may give it away, as `uniform(k)` does. A function that is made of such a call, as the `split` of [`try`](try.md) is, takes what it splits.

## A function that is called once

A function is a value like any other, and calling it takes it. So how often a function may be called is said by the two traits that say it of every value: one that has [`Copy`](ownership.md) is called again and again, and one that has not is spent by its first call.

A lambda is lent the names around it that it reads or assigns, and takes the ones it gives away. A lambda that takes a value without `Copy` has no `Copy` itself. This one gives `acc` to `append`, so it can be called once.

```
use std.bits (u64)

fn finish(xs: take List[Byte], f: take fn(take List[Byte]) -> u64) -> u64 { f(xs) }

fn collect(acc: take List[Byte], xs: take List[Byte]) -> u64 {
  finish(xs) fn(ys) { append(acc, ys).size }
}
```

`finish` marks `f` with `take`, so it owns the function and its one call spends it. A second `f(xs)` in `finish` is refused, as a second use of any value that was given away is.

A parameter that is not marked `take` lends the function, as it lends any value, and a lent value is only taken when it has `Copy`. So a lent function is one that can be called again, and a lambda that gives a value away is refused there.

```
use std.bits (u64)

fn twice(xs: take List[Byte], f: fn(take List[Byte]) -> u64) -> u64 { f(xs) }

fn collect(acc: take List[Byte], xs: take List[Byte]) -> u64 {
  twice(xs) fn(ys) { append(acc, ys).size }
}
```

This is refused at the lambda: "this lambda gives away `acc`, and the function it is passed to may call it more than once". `twice` may call its function any number of times, and the lambda can give `acc` away once.

A lambda that gives away a `var` and assigns it again before it ends has given nothing away. The name holds a value when the lambda returns, as it did when the lambda began, so the function may be called any number of times.

```
fn gather(parts: List[str]) -> str ! alloc {
  var page = text()
  each(parts) fn(part) { page = append(page, part) }
  finish(page)
}
```

`append` takes `page`, and the same statement assigns what it gives back. This is accepted when every path through the lambda assigns the name after it is given away, a `return` and a `try` that leaves among them. A path that leaves with the name given away is refused with the error above. A [handler](handlers.md) keeps a value that is owned this way: its function takes the value its `var` holds and puts the next one there.

A value the lambda gives away is spent in `collect` from the call the lambda is passed to: a use of `acc` after the call is refused, as a use after any give-away is, and a use before it is not. A second lambda of the same call that gives `acc` away is refused at the second use.

A function that takes its function calls it at most once, and may leave it uncalled. So a lambda that takes a value without [`Discard`](resources.md) is refused there, as the value is dropped when the function is not called. A function that must run exactly once is written with a type parameter, as the end of this section says.

A function that is taken is passed on only to a parameter that takes its function, and one that is lent only to a parameter that lends it. A lent function given to a `take` parameter is refused as any lent value is, and a taken function given to a lent parameter is refused, as that parameter may call it again.

The short form of a function type says one of two things, by whether its parameter is lent or taken. Anything else is said with a type parameter and `requires`, as for any type: `Function[F, Args, R]` for a function that is called once, with no `Discard` bound on `F` when it must be called exactly once, and `Copy[F]` beside it for one that is called again.

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
| `fn(take T) -> R` | A function type whose function owns its argument |
| `f: fn(T) -> R` | `f` is lent: it is called any number of times, and is a function that has `Copy` |
| `f: take fn(T) -> R` | The callee owns `f`, and calling it spends it |

Next: [Values used exactly once](resources.md)
