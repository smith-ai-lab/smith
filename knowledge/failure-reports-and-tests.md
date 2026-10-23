---
type: design
title: Failure reports are read from the call stack, and a test's signature says what it touches
description: A failing check prints frame addresses that smith symbolize turns into the failing line, so checks and contracts are plain functions, and a test's parameters decide its io.
tags: [trace, test, symbolize]
---
# Failure reports are read from the call stack, and a test's signature says what it touches

**A failure's report is read from its call stack, so checks and contracts are plain functions.** The program walks its
stack by frame pointers and writes the addresses with each image's UUID and slide (`<smith/trace.h>`); it holds no
symbolizer. smith turns them into the failing line, its source and the call chain with LLVM's symbolizer, reading
the debug map of the binary (its object files), not a dSYM: a failure costs about 8 ms beyond smith's start, `dsymutil`
40 to 150 ms at every link. `smith build test` pipes the runner through `smith symbolize`; smith run as
`llvm-symbolizer` serves the sanitizers. The checks are the generic functions of `smith::test` (`test::check_equal(a, b)`, `check_less`, `check_near`, ...; the
`SMITH_CHECK` macros are gone), and a test that must abort calls `test::expect_abort()`. **A test's signature says what it
may touch** (issue #45): `void test_x(void)` is pure, `void test_x(random::Key key)` a property test (the key from the
run's seed), `void test_x(io::Io *io)` gets a real io the runner makes for it alone (its key from the run's seed, a
scratch folder removed when it passes and kept, named, when it fails, the working directory still the project root);
a test makes fakes anywhere and never a real io (`smith-effects`). The runner is `smith::test`'s `main`; its own
effects (forked workers, pipes, signals, shared memory) are calls in the module, which defines the `Runner` capability,
and its console, clock, arguments and processes are its io's. The shrinking hook is a field of the key (`random::Key.tap`),
not a global.

`smith symbolize [--binary <program>] [<log>]` turns the `smith-trace` lines of a failure (a log, a pipe, standard
input) into the failing line and the call chain; it needs the full smith.
