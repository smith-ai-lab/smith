---
name: smith-test-workers
description: "Running tests and the runner's workers: --test, --seed, --seeds, --jobs, --shrink, --full, --isolate, how failures replay and shrink. Load on a flaky or failing property test, or to replay a seed."
---

# Run them

`smith test`. Its arguments are the runner's own options, so `smith test --test <substring>` runs one test (or every name that contains it), `--seed <n>` a property test or an io test on that one seed, `--seeds <n>` sets the keys per property test (100), and there are `--jobs <n>`, `--timeout <seconds>` (10 seconds of CPU time per test and per key, so a loaded machine slows a test and never fails it; a test that is blocked, asleep or waiting on a pipe, spends none, so `--wall-timeout <seconds>`, 600 by default, ends it as a hang, never as a timeout), `--shrink 0|1`, `--shuffle <seed>`, `--isolate`, `--in-process` (no fork, for a debugger), `--stats` (prints how many forks the run made), `--full` (the heavy checks at full size), `--list` and `--verbose`. `-D` options are build.c's own (`smith test -DFAST`). The runner is `build/tests`, so under a debugger it is `smith dbg build/tests -- --test name --in-process`. `--in-process` runs the tests one after another in the runner's own process, because lldb on macOS does not follow forks (`--jobs 1` and `--isolate` still fork, so a breakpoint in a test kills its child with SIGTRAP). It gives up isolation: a crash, an abort the test did not expect or a hang ends the whole run, state one test leaves reaches the next, there is no timeout unless `--timeout` is given, and no shrinking (a failing property test prints its seed; replay it with `--seed`). An io test's seed is the run's own unless `--seed` is given, and a failing one prints `rerun: smith test --test <name> --seed <n>` like a property test.

### Workers

The runner forks `--jobs` workers once and each runs the tests it is sent, one after another, in its own process. Each test starts with its arenas given back and an empty note; everything else it leaves behind (globals, open files, the heap) stays for the next test on that worker, and which tests share a worker depends on timing unless `--jobs 1`. So a test must not depend on another. `--shuffle <seed>` runs the tests in a shuffled order to find one that does; with `--jobs 1` the same seed replays it. A worker is reused only after a clean pass that expected no abort. A failed test ends its worker (a failed check unwinds out of arbitrary code and can leave state half-done), a test that called `test::expect_abort` ends it too, passed or failed (an abort stands for the program ending, so shared state it left, such as a committed GPU command buffer, must not reach the next test), and so do a crash, a timeout, an exit and a sanitizer's report; the runner forks the next. Shrinking reuses one worker across its failing attempts, so the runner reruns the shrunk case once in a fresh worker; if it does not fail the same way you get the unshrunk failure and a line saying state leaked between attempts. A test that leaves global state dirty therefore only matters to the passing tests that follow it. `--isolate` runs each test in a process of its own, as before the workers. A sanitized `fork` costs about 2 ms (smith's compiler-rt carries the fast-mmap patch; upstream's cost about 40 ms).

A failure in `smith build test` looks like this; the last line reproduces it. The runner only records the addresses of the stack
(`<smith/trace.h>`); smith turns them into files, lines and source text with LLVM's symbolizer, so the program holds no
symbolizer and the test binary links as quickly as before:

```text
FAIL moves_every_step
  check failed at tests/moves_test.smith:42
       42 |     test::check_equal(moves.count, expected);
            moves.count = 3, expected = 4   (u64)
  called from
    tests/moves_test.smith:42  moves_every_step
    .smith/lib/smith/test.smith:1065   smith_test_execute
```

**Values of the failing line.** For a `test::check(cond)`, a failed contract or an `assert`, `smith test` starts the runner with `--explain <smith>`: when the check fails, the test process waits while `smith --smith-explain` attaches lldb to it, reads the variables the source line names and lets it go, and the report prints them under the line, so `check(moves.count == expected)` shows both:

```text
FAIL moves_every_step
  check failed at tests/moves_test.smith:42
       42 |     test::check(moves.count == expected);
            moves.count = 7
            expected    = 8
```

It reads only: locals, parameters, members, indexes (an index that is a variable is read first) and pointers (address, then the pointee); never a call. An optimized-away variable says `<optimized out>`, a function, a type and a name that is no variable are dropped. It costs about 0.15 s per failure, only on a failure; it reads at most `--explain-max <n>` (5) failures a run and says how many it skipped, and each at most 5 s. A property test is read once, on its shrunk failure. `smith test --no-explain` turns it off. With no debugger (the stage0), or a machine that refuses to debug (`smith-debug`: Developer mode, hardened programs), the report is the same as before with one line, `note: values not shown: ...`. A crash (SIGSEGV) is not read. By hand: `build/tests --trace --explain "$(command -v smith)" | smith symbolize`.

An `assert` is `aborted at file:line`, a segfault `crashed at file:line`, with inlined frames in the chain. AddressSanitizer and
UBSan stacks have lines too: smith answers their symbolizer protocol. A runner run by hand prints the raw `smith-trace` lines for a
failed check; `smith symbolize [--binary <program>] [<log>]` turns a saved log back into the report. A property test says `with seed 6387777158891554393` after FAIL, and its last line is `rerun: smith test --test <name> --seed 6387777158891554393`.

A test with a heavy check picks its size from `test::full()`, false by default and true with `--full`: `usize count = test::full() ? 20000 : 200;`. It checks the same properties either way. Use `test::check_same_bits` for determinism, where near is not enough.

A failing property test is shrunk before it is shown: the runner reruns the key, tries each random word it drew as 0 and then as the smallest word that still fails the same check (the same stack) or the same signal, within 1,000 runs or 10 s, so a failure shows `x = 500` rather than a random 64-bit draw; the header says `shrunk in 41 runs`. The rerun line names the original seed and shrinks to the same case again; `--shrink 0` shows the unshrunk failure. This works because every distribution in smith-random maps word 0 to its simplest value, so draw everything from the key with smith's distributions. Your own distribution should do the same: spend the key with `random::key_bits`, `key_unit_float` or `key_unit_double`, and make word 0 the simplest value. (The runner's keys carry a `tap` that records and replaces the words; a program's own keys carry none.)

A crash is `FAIL name: killed by SIGSEGV` with the process's stderr under it (a sanitizer's report), a busy test over the limit `FAIL name: over its CPU time limit of 10 s (it ran 10.3 s)`, a blocked one `FAIL name: hung after 600.0 s of wall time` (a test that calls `alarm` itself replaces the backstop, and ends as a hang after its own time). One crash fails one test, and the rest run on a fresh worker. A passing test's output is hidden.

Also in this family: [smith-test](../smith-test/SKILL.md), [smith-test-modules](../smith-test-modules/SKILL.md), [smith-test-checks](../smith-test-checks/SKILL.md), [smith-test-c](../smith-test-c/SKILL.md), [smith-build-tests](../smith-build-tests/SKILL.md).
