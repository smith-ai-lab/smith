---
type: architecture
title: The program
description: What the program does, and what its assertion, its output and its tests promise.
tags: [program]
---
# The program

@NAME@ is the modules `main` (`main.smith`) and `primes` (`primes.smith`), with tests in `primes_test.smith` beside `primes.smith`, that `smith new` wrote as a starting point.

- `primes::sum` adds the first `count` primes. It requires the count to be at most five (`contract::requires`, the
  caller's job) and ensures the total is at most 28 (`contract::ensures`, its promise); `smith prove` proves the promise
  from the precondition, and `smith check` runs it.
- `primes::total` adds the numbers of a slice (`i32 xs[:]`, a pointer and a count; `values[1:3]` slices an array). It is an
  exported function over a slice, which `smith run` compiles in the debug profile with `-fsanitize=function`.
- `main.smith` adds the first five and asserts that the sum is 28.
- `main` is `i32 main()` and passes the sum to `primes::report`, which prints it and `hello from @NAME@`; main then
  returns 0.
- Effects are called directly (`console::print`) and inferred from what a function calls (#207); a test stands in for
  the console with a handler (`console::set_handler`) and reads what `report` printed.
- The contracts and the assertion stay on in every build, `fast` included; only a proof removes one.
- `primes_test.smith` tests `primes::sum` and `primes::total` with `test::check_equal`, checks that six primes break its contract
  (`test::expect_abort()`) and that `primes::report` prints the sum on a fake console; `smith test` runs it, and `smith
  check` does too. A test's signature says what it may touch: `()` is pure and `(random::Key key)` a property test.
