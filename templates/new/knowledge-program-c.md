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
- Effects are the direct modules: `report` writes with `smith_console_write`, and a test installs a fake console with `smith_console_set_handler`.
  `smith lint` (the `smith-effects` check) reports a call such as `printf` outside a capability's own module.
- The contracts and the assertion stay on in every build, `fast` included; only a proof removes one.
- `primes_test.c` tests `primes_sum`; `smith test` runs it, and `smith check` does too.
