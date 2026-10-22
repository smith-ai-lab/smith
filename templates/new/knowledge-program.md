---
type: architecture
title: The program
description: What the program does, and what its assertion, its output and its tests promise.
tags: [program]
---
# The program

@NAME@ is `main.smith` and the module `primes` (`primes.smith`), that `smith new` wrote as a starting point.

- `primes::sum` adds the first `count` primes, at most five, and asserts the count is in range.
- `main.smith` adds the first five and asserts that the sum is 28.
- `main` makes the program's io once (`smith_io_make`, which only `main` may call) and passes `&io.console` to `primes::report`,
  which prints the sum and `hello from @NAME@`; main then returns 0.
- Effects come through parameters from `main`: `report` takes a `SmithConsole *` and nothing else, so its signature says
  what it can do to the world, and a test gives it a fake console (`smith_io_make_test`, which anything may call).
  `smith lint` (the `smith-effects` check) reports a call such as `c::printf` outside a capability's own module.
- The assertions stay on in every build but `fast` (`-DNDEBUG`).
- `tests/test_primes.smith` tests `primes::sum` with `test::check_equal`, checks that six primes break its contract
  (`test::expect_abort()`) and that `primes::report` prints the sum on a fake console; `smith test` runs it, and `smith
  check` does too. A test's signature says what it may touch: `(void)` is pure, `(random::Key key)` a property test,
  `(c::SmithIo *io)` a real io in a scratch folder.
