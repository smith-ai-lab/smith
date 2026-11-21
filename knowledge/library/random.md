---
type: specification
title: Random
description: Random bits from a key, distributions as ordinary structs with the Sample and LogProb traits, and how a program adds its own.
status: partial
built:
  - "`std.random` with `Key`, `bits`, `split`, `seeded`, and `fresh`, and the generator of [The generator](#the-generator), which a test checks against the vectors Random123 publishes and against a model of it written in Rust"
  - "not built: `uniform`, `standard_normal`, `Sample`, `LogProb`, and `split_n`, since no float type is built yet"
---

# Random

Randomness comes from a [`Key`](../language/take.md), which is used at most once, so the same key never gives the same number twice by accident and a program's random results can be repeated. One function turns a key into bits, and everything else in this module is written in Smith on top of it.

```
fn bits(key: take Key) -> u64
```

`uniform(key)` gives an `f32` from 0 up to but not including 1, and `standard_normal(key)` gives one from the normal distribution with mean 0 and deviation 1. Both are arithmetic on `bits`.

## Where a key comes from

A `Key` made from a seed gives the same bits each run, so a program that starts from one repeats, which a test and a model's training need. A `Key` made from [`entropy`](entropy.md) does not repeat, so a function that makes one lists `entropy`.

```
fn seeded(seed: u64) -> Key
fn fresh() -> Key ! entropy, alloc
fn split(key: take Key) -> (Key, Key)
```

`seeded` makes a `Key` from a number, `fresh` asks `entropy` for the 16 bytes of one, and `split` gives two keys from one, which a program uses to draw from two places that must not give the same bits. A `Key` is used once, so each of these functions that takes one has none left for the caller.

A secret, such as a token or the challenge of a passkey, is taken from `entropy` itself and never from a `Key`. A `Key` is a way to repeat or split randomness, and a secret is what no one can repeat.

## The generator

A `Key` holds two numbers of 64 bits, `(k0, k1)`, and `bits` and `split` are one function of it, Threefry-2x64 with 20 rounds from Salmon, Moraes, Dror, and Shaw, "Parallel random numbers: as easy as 1, 2, 3", which Random123 publishes. `threefry(counter, key)` takes a counter and a key, each two numbers of 64 bits, and gives two numbers of 64 bits. Every number is read as an unsigned number of 64 bits, and `+` wraps.

A third word of the key is `k2 = k0 xor k1 xor 0x1BD11BDAA9FC1A22`. The state starts as `x0 = c0 + k0` and `x1 = c1 + k1`. Round `r`, from 0 to 19, is `x0 = x0 + x1`, then `x1` rotated left by the `r` mod 8 th of 16, 42, 12, 31, 16, 32, 24, 21, then `x1 = x1 xor x0`. After each fourth round, the one numbered `r = 3, 7, 11, 15, 19`, with `s = (r + 1) / 4`, the key is added: `x0 = x0 + ks[s mod 3]` and `x1 = x1 + ks[(s + 1) mod 3] + s`, where `ks` is `(k0, k1, k2)`. The result is `(x0, x1)`.

What a key gives is fixed by the counter that is put in, so the numbers a program draws are these, and no other:

| Function | Is |
| --- | --- |
| `seeded(seed)` | The key `threefry((3, 0), (seed, 0))` |
| `bits(key)` | The first number of `threefry((0, 0), (k0, k1))`, as a `u64` |
| `split(key)` | The keys `threefry((1, 0), (k0, k1))` and `threefry((2, 0), (k0, k1))`, in that order |
| `fresh()` | The key whose `k0` is the first 8 bytes `entropy` filled and `k1` the next 8, each read with its first byte the least |

A counter that begins with 0, 1, 2, or 3 never meets another use of the generator, so `bits`, `split`, and `seeded` of one key do not give the same numbers. The numbers of `threefry((0, 0), (0, 0))` are `c2b6e3a8c2c69865` and `6f81ed42f350084d`, as Random123 publishes.

## Distributions

A distribution is an ordinary struct that holds its parameters. What it can do is said by traits, and drawing a value from it is `Sample`, whose `out` type is what a draw gives.

```
trait Sample[D, out T] {
  fn sample(d: D, key: take Key) -> T
}

struct Normal {
  mean: f32
  std: f32
}

impl Sample[Normal, f32] {
  fn sample(d: Normal, key: take Key) -> f32 {
    d.mean + d.std * standard_normal(key)
  }
}

x = sample(Normal(mean: 0.0, std: 1.0), key)
```

- Nothing about `Normal` is built in. A distribution a program defines is a struct and an `impl`, written the same way and used the same way.
- `sample` for `Normal` is plain arithmetic on `mean` and `std`, so [`grad`](../language/differentiation.md) differentiates through a draw with respect to them, with nothing more written.
- A distribution built from others, such as a mixture, is a struct with type parameters and its own `impl`.
- A call of a trait's function is chosen when the program is built, so drawing through `Sample` costs what the arithmetic costs.

Many draws are a [`for`](../language/random-arrays.md) over split keys, which runs in parallel like any other.

```
fn samples[D, T, n: Nat](d: D, key: take Key) -> [n]T {
  keys = split_n(key)
  for i { sample(d, keys[i]) }
}
```

Each other thing a distribution may offer is a trait of its own, so a distribution implements what it has and a function asks for what it needs. `LogProb` gives the logarithm of the probability of a value.

```
trait LogProb[D, T] {
  fn log_prob(d: D, x: T) -> f32
}
```

## Distributions and tests

[`Random`](../language/random.md) and `Sample` do different work. `Random[T]` gives a value of a type that is good at finding mistakes, so it reaches for 0, the greatest value, and the empty text more often than chance would. `Sample` draws from exactly the distribution it is given. An implementation of `Random` is written with distributions.

Every draw takes its randomness through `bits`. A failing test is made smaller by replaying it with smaller bits, so a value drawn from any distribution, one a program defines included, is made smaller with nothing written for it.
