---
type: concept
title: Reading a cost report
description: When a proof runs out of its budget, and with smith prove -v for every function, the report says where the time went, the slowest queries and what in the code made them costly.
tags: [prove, budget, cost]
---
# Reading a cost report

A proof that runs out of its budget, and `smith prove -v` for every function, say what took the time.

## Example

```text
FAIL  divmod  (division.c:6)
  over its budget of 2 s of CPU time ...
  where the time went (CPU, as charged):
        0.01 s  writing 3 of the 3 queries as SMT
        1.99 s  the solver
  the slowest of 3 queries (CPU time, side by side on up to 8 threads):
       1.99 s CPU  smith_ensures(q * y + r == x)  (division.c:10)  <- the budget ended here
                    its value depends on a 64-bit multiplication of two variables at division.c:10
                    its value depends on a 64-bit unsigned division by a variable at division.c:8
  the code the solver is given: 12 instructions, 0 loads, 0 stores; what makes queries costly in it:
      division.c:8: 64-bit unsigned division by a variable
```

## Rules

- **Where the time went** is the CPU time as charged, by phase: Alive2's symbolic execution of the function, writing the queries (one per undefined-behaviour check and contract; a function with thousands of checks spends its budget here), and the solver.
- **The slowest queries** are named by the contract or instruction they ask about, its line and its CPU time. *Its value depends on* lists the divisions, remainders and multiplications of variables that the value is computed from, found in the IR.
- **What makes queries costly** is read off the IR, by line: a division or remainder by a variable and a multiplication of two variables (each becomes thousands of gates: divide by a constant, or state the property without it); the loads and stores of a line (an unrolled loop of calls makes hundreds); a `memset`, `memcpy` or local object of 128 bytes or more; and each loop with its mode.
- **What to do**: put an `ensures` on the function that does the work in a pass, so callers use it instead of the body (a proof of eight calls beats one of sixty-four). Do not repeat a callee's `requires` as an `invariant` just before the call: both are proof points, and the repeat tripled one function's time. Give a callee only the `ensures` its callers need, since every `ensures` is assumed in every query of every caller; state an exact specification on a function that calls the generator and nothing else calls. A loop with an `invariant` is proved by induction when unrolling costs more than a second.

## Interactions

- The budget and the cache: [proof-budget](proof-budget.md). Loop modes: [prove-loops](prove-loops.md). A contract paid at every call: [prove-calls](prove-calls.md).

## Replaces from C

Guessing why a check is slow.
