---
type: architecture
title: The program
description: What the program does, and what its assertion, its output and its tests promise.
tags: [program]
---
# The program

@NAME@ is the modules `main` (`main.smith`) and `primes` (`primes.smith`), with tests in `tests/primes_test.smith`, that `smith new` wrote as a starting point.

- `primes::sum` adds the first `count` primes. It requires the count to be at most five (`contract::requires`, the
  caller's job) and ensures the total is at most 28 (`contract::ensures`, its promise); `smith prove` proves the promise
  from the precondition, and `smith check` runs it.
- `primes::total` adds the numbers of a slice (`i32 xs[:]`, a pointer and a count; `values[1:3]` slices an array). It is an
  exported function over a slice, which `smith run` compiles in the debug profile with `-fsanitize=function`.
- `main.smith` adds the first five and asserts that the sum is 28.
- `main` takes the program's io (`i32 main(io::Io *io)`: smith makes it before `main` and closes it after, and `main`
  is the only function that gets all of it) and passes `&io->console` to `primes::report`, which prints the sum and
  `hello from @NAME@`; main then returns 0.
- Effects come through parameters from `main`: `report` takes an `io::Console *` and nothing else, so its signature says
  what it can do to the world, and a test gives it a fake console (`io::make_test`, which anything may call).
  `smith lint` (the `smith-effects` check) reports a call such as `c::printf` outside a capability's own module.
- The contracts and the assertion stay on in every build but `fast` (`-DNDEBUG`).
- `tests/primes_test.smith` tests `primes::sum` and `primes::total` with `test::check_equal`, checks that six primes break its contract
  (`test::expect_abort()`) and that `primes::report` prints the sum on a fake console; `smith test` runs it, and `smith
  check` does too. A test's signature says what it may touch: `(void)` is pure, `(random::Key key)` a property test,
  `(io::Io *io)` a real io in a scratch folder.
