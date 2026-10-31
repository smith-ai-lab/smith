---
type: architecture
title: The program
description: What the program does, and what its assertion, its output and its tests promise.
tags: [program]
---
# The program

@NAME@ is `main.c` and `primes.c`, that `smith new` wrote as a starting point.

- `primes_sum` (`primes.c`) adds the first `count` primes. It requires the count to be at most five (`smith_requires`,
  the caller's job) and ensures the total is at most 28 (`smith_ensures`, its promise); `smith prove` proves the
  promise from the precondition, and `smith check` runs it.
- `main.c` adds the first five and asserts that the sum is 28.
- `main` calls `report`, which prints the sum and `hello from @NAME@`; main then returns 0.
- `report` calls `printf` directly; effects are inferred from what a function calls (#207), and a test controls the console with `smith_console_set_handler`.
- The contracts and the assertion stay on in every build, `fast` included; only a proof removes one.
- `primes_test.c` tests `primes_sum`; `smith test` runs it, and `smith check` does too.
