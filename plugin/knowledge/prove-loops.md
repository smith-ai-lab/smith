---
type: concept
title: Proving loops
description: A loop without an invariant is unrolled and needs a bound from a constant or a requires; a loop with contract.invariant is unrolled up to 16 passes and proved by induction beyond.
tags: [prove, loops]
---
# Proving loops

A loop without an invariant is unrolled, and its count comes from a constant or from a `requires` that bounds a parameter. That bound is the function's worst case. A loop with `contract.invariant` has two modes.

## Example

```smith
import std.contract

pub fn squares_sum(count: usize) -> i32:
    contract.requires(count <= 5)
    squares := [0, 1, 4, 9, 16]
    mut total: i32 = 0
    mut i: usize = 0
    while i < count:
        total += squares[i]
        i += 1
    contract.ensures(total <= 30)
    total
```

Without the `requires` the proof says where and why:

```text
FAIL  sum  (primes.smith:8)
  primes.smith:11: cannot be proved: the loop has no bound the prover can see: it runs `count` times
      to prove it: state its worst case, contract::requires(count <= ...), which callers then prove ...
```

## Rules

- **A bound** is a parameter compared with a constant, alone or joined by `and` (`0 <= n and n <= 5`), in a `requires` before the loop. It also stays a premise after the loop. A bound on a field or a computed value does not reach the unroller: make it a parameter or give the loop a constant.
- **A loop that divides or shifts a value to zero** (`v /= 10` until 0) has no count LLVM can derive; the message gives the most passes it takes (`... at most 20`). Use a counter a constant bounds: loop at most 20 times and `break` when `v` is zero.
- **With `invariant`**: up to 16 passes the loop is unrolled and `c` is proved on each pass, so `c` need not be inductive. With more passes or no constant bound it is proved by induction, one pass whatever the bound.
- **Induction** proves `c` on entry; then, with every loop variable and every object the loop writes arbitrary, assumes `c` and the test, runs one pass and proves `c` again. `c` is read where the test is, so it holds when the loop exits too (`i <= n and total == 3 * i`) and must say all the proof needs of what a pass writes.
- A weak invariant fails `smith_invariant(c) may be false after a pass of the loop`, with the pass's values and the names the body writes that `c` does not mention. One false at the start fails `when the loop is entered`.
- If induction fails and every loop has at most 128 passes, unrolling is tried next.
- `--induction`, `--unroll` and `--induction-from=N` (default 16) choose the mode; `smith prove -v` names each loop's mode, and why a loop was not cut (a write the prover cannot trace to an object of known size, a call writing memory it cannot name, a pointer loop variable, an inner loop before the invariant).

## Interactions

- Many loads in many passes cost time: 64 passes over a board take seconds unrolled and a fraction of a second by induction ([prove-cost](prove-cost.md)).
- A loop over a slice needs the invariant `1 <= i and i <= countof(xs)` ([prove-slices](prove-slices.md)).
- The invariant is the first statement of the body ([contracts](contracts.md)).

## Replaces from C

A comment saying why a loop stays in range, and `assert` inside it.
