---
type: specification
title: check
description: check tests outside data when the program runs and gives a proof when the test passes, and how a failure is returned and never panics.
status: planned
---

# check

`check` tests a statement about data that is known only when the program runs, and gives a proof of it when the test passes. It is the one place a proof comes from a test.

```
fn token[v: Nat](raw: Nat) -> Option[Fin[v]] {
  match check(raw < v) {
    .some(p) { .some(Fin(value: raw, below: p)) }
    .none { .none }
  }
}
```

`check(raw < v)` is `.some(p)` where `p` is a proof of `raw < v` when it holds, and `.none` when it does not. In the first arm `p` is the proof the [`Fin`](proof-fields.md) needs. With `v` as 10, a `raw` of 7 gives `.some` of a `Fin[10]`, and a `raw` of 12 gives `.none`.

## What check is for

A proof that `arith` cannot build from what is in scope comes from `check`, run on the data when the program runs. Where an `if` can prove the same thing for a block, as in `if raw < v { ... }`, nothing needs a value to hold the proof. `check` is for a proof that must be kept: returned, passed on, or held in a field.

```
fn fill(rows: Nat, cols: Nat, limit: Nat) -> Nat
  requires rows * cols <= limit
{
  limit - rows * cols
}

fn fill_checked(rows: Nat, cols: Nat, limit: Nat) -> Option[Nat] {
  match check(rows * cols <= limit) {
    .some(p) { .some(fill(rows, cols, limit)) }
    .none { .none }
  }
}
```

In the first arm `p` is a proof that `rows * cols <= limit`, which `fill` requires of its arguments, and the same proof proves that the subtraction fits. `fill_checked(2, 3, 10)` is `.some(4)`, and `fill_checked(4, 4, 10)` is `.none`.

`arith` never proves `rows * cols <= limit` from facts about `rows` and `cols` alone, so a caller whose sizes are known only when the program runs gets it from a `check`, as [What arith proves](arith.md) says.

## A failure is returned

There is no `panic`. A `check` that fails returns `.none`, and a failure that can really happen is returned as a `Result`. A function with none of the `alloc`, `console`, and `diverge` effects therefore returns a value for every input its `requires` lines allow. The first two can [end the task](tasks.md) when the system runs out of what they stand for.

```
fn index_of(raw: Nat, size: Nat) -> Result[Fin[size], str] {
  match check(raw < size) {
    .some(p) { .ok(Fin(value: raw, below: p)) }
    .none { .err("index out of range") }
  }
}
```

The caller decides what a failure means, with [`try`](try.md) or `match`.

## Where it is built in

`check` and [`hole`](hole.md) are built in, because nothing written in Smith could make a proof from nothing. Every other proof starts from `arith` or from a `check` that passed.

## The forms

| Written | Is |
| --- | --- |
| `check(p)` | `.some(proof)` when `p` holds, and `.none` when it does not |
| `match check(p) { .some(q) { ... } .none { ... } }` | The two ways it can go, with `q` the proof in the first |

Next: [Proofs as functions](proof-functions.md)
