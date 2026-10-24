---
name: smith-prove-cost
description: "Reading smith prove -v's cost report: wall, CPU and charged time, the slowest query and what made it costly. Load when a proof is slow, times out or exceeds its budget."
---

# Reading a cost report

A proof that runs out of its budget, and `-v` for every function, say what took the time:

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

- **Where the time went** is the CPU time as charged, by phase: Alive2's symbolic execution of the function, writing the queries (one per undefined-behaviour check and contract; a function with thousands of checks spends its budget here, and the budget ends the writing, not only the solving), and the solver.
- **The slowest queries** are named by the contract or the instruction they ask about and its line, with their own CPU time. *Its value depends on* lists the division, remainder and multiplication of variables that its value is computed from, found in the IR (a data dependence, not a guess).
- **What makes queries costly** is read off the IR the solver is given, by line, never estimated: a division or remainder by a variable and a multiplication of two variables (the solver turns each into thousands of gates: divide by a constant, or state the property without it), the loads and stores of a line (an unrolled loop of calls makes hundreds, each a read or write of a memory array), a `memset`/`memcpy` or local object of 128 bytes or more (a by-value zero-init, a copy of a board), and each loop with its mode (`a loop unrolled into 64 passes`, or `a loop proved by induction: one pass of its body, whatever its bound`: give a loop with a `smith_invariant` that mode when unrolling it costs more than a second).
- **What to do:** put a `smith_ensures` on the function that does the work in a pass, so that callers use it instead of the body (`fill_square` for `fill_row` for `fill_valid_moves`: a proof of eight calls beats one of sixty-four). Do not repeat a callee's `smith_requires` as a `smith_invariant` just before the call: both are proof points, and the repeat tripled one function's time. Give a callee only the `smith_ensures` its callers need: every `smith_ensures` is assumed in every query of every caller, so an exact specification of a move generator on the generator makes its callers slow; state it on a function that calls the generator and nothing else calls (a proof in the source), as the exact spec of checkers' generators is.

Also in this family: [smith-contract](../smith-contract/SKILL.md), [smith-contract-c](../smith-contract-c/SKILL.md), [smith-contract-proof](../smith-contract-proof/SKILL.md), [smith-prove](../smith-prove/SKILL.md), [smith-prove-trusted](../smith-prove-trusted/SKILL.md).
