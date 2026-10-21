---
type: architecture
title: The program
description: What main.c does, and what its assertion and its output promise.
tags: [program]
---
# The program

@NAME@ is one file, `main.c`, that `smith new` wrote as a starting point.

- It adds the first five primes, asserts that the sum is 28, and prints the sum.
- It then prints `hello from @NAME@` and returns 0.
- The assertion stays on in every build but `fast` (`-DNDEBUG`).
