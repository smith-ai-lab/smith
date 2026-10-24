---
name: smith-prove-loops
description: "smith prove on loops: a loop without an invariant is unrolled and its count comes from a constant or a requires, the error when it has no bound, loops that divide to zero, and a loop with smith_invariant (unrolled up to 16 passes, then by induction, --induction and --unroll). Load on `the loop has no bound the prover can see`, or when choosing between a bound and an invariant."
---

# Proving loops

```smith
module tally;

import smith::contract;

export i32 squares_sum(usize count) {
    contract::requires(count <= 5);              // the worst case: the loop is unrolled into at most 5 passes
    constexpr i32 squares[] = {0, 1, 4, 9, 16};
    i32 total = 0;
    for (usize i = 0; i < count; i++) {
        total += squares[i];
    }
    contract::ensures(total <= 30);
    return total;
}
```

A loop over what a caller's array holds proves the same way, whatever its body does with the elements:

```smith
module sums;

import smith::contract;

export usize positives(i32 xs[8], usize count) {
    contract::requires(count <= 8);
    usize hits = 0;
    for (usize i = 0; i < count; i++) {
        if (xs[i] > 0) hits++;
    }
    contract::ensures(hits <= 8);
    return hits;
}
```

The bound must be a parameter compared with a constant. Without the `requires`, the proof says where and why:

```text
FAIL  sum  (primes.smith:8)
  primes.smith:11: cannot be proved: the loop has no bound the prover can see: it runs `count` times
      to prove it: state its worst case, contract::requires(count <= ...), which callers then prove ...
```

- **A loop without an invariant is unrolled, and its count comes from a constant or from a `requires` that bounds it.** That bound is the function's worst case (what real-time code needs). A `contract::requires(count <= 5)` (or `0 <= n && n <= 5`: a parameter compared with a constant, alone or joined by `&&`) before `for (usize i = 0; i < count; i++)` makes the loop run at most 5 passes in the proof, and stays a premise for the rest of the function (`a[n - 1]` after the loop is in range from it); a bound on anything that is not a parameter (a field, a computed value) does not reach the unroller, so give the loop a constant or make the bound a parameter. A loop with no visible bound fails with the loop's line, the variable that decides its count and the fix:
  A loop that divides or shifts a value to zero (`do { v /= 10; } while (v != 0)`) has no count LLVM can derive, whatever `requires` says; the message says how many passes it takes at most (`a 64-bit value divided by 10 ... at most 20`) and the fix is a counter a constant bounds: `for (usize pass = 0; pass < 20; pass++) { if (v == 0) break; v /= 10; }`. 
- **A loop with `smith_invariant` has two modes** (smith-contract): up to 16 passes it is unrolled and the invariant is proved on each pass; with more, or no constant bound, it is proved by induction, one pass whatever the bound. A loop with no invariant is unrolled. Many loads in many passes cost time: a loop of 64 passes over a board takes seconds unrolled and a fraction of a second by induction. `--induction`, `--unroll` and `--induction-from=N` (the project's options too) choose; `-v` names each loop's mode.

See `smith-prove`, `smith-contract` (`invariant`), `smith-contract-proof`.
