---
type: concept
title: Proving slices and arrays
description: A slice carries its count so an index is proved against it; an array parameter is N objects; a pointer view p[a:b] is held to what p is known to point at.
tags: [prove, slices]
---
# Proving slices and arrays

A slice carries its count, so an index is proved against it with no contract; a loop over it needs an invariant.

## Example

```smith
import std.contract

pub fn largest(xs: []i32) -> i32:
    contract.requires(countof(xs) > 0)
    mut best := xs[0]
    mut i: usize = 1
    while i < countof(xs):
        contract.invariant(1 <= i and i <= countof(xs))
        if xs[i] > best:
            best = xs[i]
        i += 1
    best
```

In a project `smith prove -v` says `ok  stats::largest  (stats.smith:5)  0.03 s wall` and `a loop proved by induction`.

## Rules

- **An array parameter is N objects** (`xs: [4]i32`): `xs[i]` is proved in range by `requires(i < 4)` alone. In C, `T xs[static N]` says the same; a plain `T xs[N]` is a pointer to one.
- **A slice parameter is `countof(xs)` objects**: `if i < countof(xs): xs[i]` is proved with no `requires`; a loop up to `countof(xs)` is proved with the invariant above, or unrolled under `requires(countof(xs) <= 8)` (a large bound unrolls past the budget).
- An index or a slice (`xs[i]`, `xs[a:b]`) that may be out of range fails as `assert(an index or a slice is in range) may fail` at its line, in the proved function and in what it inlines.
- **A view of a pointer, `p[a:b]`**, is held to what `p` is known to point at when its elements are read: `*T` is one object, so `p[0:2]` read at `[1]` fails; an array parameter of four makes `p[0:4]` fine. A `p[a:b]` never read is not yet checked.
- **A slice of rows** (`[][2]f32`, `[][]f32`) points at its rows the same way: `if r < countof(m) and c < countof(m[r]): m[r][c]` is proved with no contract (about 2 s), and an inner index past its own count fails at its line. A `requires` may not index a slice of rows (`countof(m[0])`: use `countof(m)`), and a read through a row view takes about 8 s of CPU.
- Each dimension and the whole are assumed under 2^20 elements; the count of a plain slice is assumed at most 2^32. A slice that arrives in memory (a field, a pointer to one) is not assumed to hold anything.

## Interactions

- The invariant is proved by induction above 16 passes ([prove-loops](prove-loops.md)).
- `restrict` on a slice is checked at each call ([prove-calls](prove-calls.md)).

## Replaces from C

A pointer and a separate count with `assert(i < n)`.
