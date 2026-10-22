---
type: architecture
title: The program
description: What the program does, and what its assertion, its output and its tests promise.
tags: [program]
---
# The program

@NAME@ is `main.smith` and the module `primes` (`primes.smith`), that `smith new` wrote as a starting point.

- `primes::sum` adds the first `count` primes, at most five, and asserts the count is in range.
- `main.smith` adds the first five, asserts that the sum is 28, and prints the sum.
- It then prints `hello from @NAME@` and returns 0.
- The assertions stay on in every build but `fast` (`-DNDEBUG`).
- `tests/test_primes.smith` tests `primes::sum`; `smith test` runs it, and `smith check` does too.
