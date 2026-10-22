---
type: architecture
title: The program
description: What the program does, and what its assertion, its output and its tests promise.
tags: [program]
---
# The program

@NAME@ is `main.c` and `primes.c`, that `smith new` wrote as a starting point.

- `primes_sum` (`primes.c`) adds the first `count` primes, at most five, and asserts the count is in range.
- `main.c` adds the first five and asserts that the sum is 28.
- `main` makes the program's io once (`smith_io_make`, which only `main` may call) and passes `&io.console` to `report`,
  which prints the sum and `hello from @NAME@`; main then returns 0.
- Effects come through parameters from `main`: `report` takes a `SmithConsole *` and nothing else, so its signature says
  what it can do to the world, and a test gives it a fake console (`smith_io_make_test`, which anything may call).
  `smith lint` (the `smith-effects` check) reports a call such as `printf` outside a capability's own module.
- The assertions stay on in every build but `fast` (`-DNDEBUG`).
- `tests/test_primes.c` tests `primes_sum`; `smith test` runs it, and `smith check` does too.
