---
type: architecture
title: The program
description: What the program does, and what its assertion, its output and its tests promise.
tags: [program]
---
# The program

@NAME@ is `main.c` and `primes.c`, that `smith new` wrote as a starting point.

- `primes_sum` (`primes.c`) adds the first `count` primes, at most five, and asserts the count is in range.
- `main.c` adds the first five, asserts that the sum is 28, and prints the sum.
- It then prints `hello from @NAME@` and returns 0.
- The assertions stay on in every build but `fast` (`-DNDEBUG`).
- `tests/test_primes.c` tests `primes_sum`; `smith test` runs it, and `smith check` does too.
