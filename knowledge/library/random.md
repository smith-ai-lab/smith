---
type: specification
title: Random
description: Random bits from a key, distributions as ordinary structs with the Sample and LogProb traits, and how a program adds its own.
---

# Random

Randomness comes from a [`Key`](../language/take.md), which is used at most once, so the same key never gives the same number twice by accident and a program's random results can be repeated. One function turns a key into bits, and everything else in this module is written in Smith on top of it.

```
fn bits(key: take Key) -> u64
```

`uniform(key)` gives an `f32` from 0 up to but not including 1, and `standard_normal(key)` gives one from the normal distribution with mean 0 and deviation 1. Both are arithmetic on `bits`.

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

[`Random`](../language/tests.md) and `Sample` do different work. `Random[T]` gives a value of a type that is good at finding mistakes, so it reaches for 0, the greatest value, and the empty text more often than chance would. `Sample` draws from exactly the distribution it is given. An implementation of `Random` is written with distributions.

Every draw takes its randomness through `bits`. A failing test is made smaller by replaying it with smaller bits, so a value drawn from any distribution, one a program defines included, is made smaller with nothing written for it.
