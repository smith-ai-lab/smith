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

**The values of the failing line are read from the stopped test, not captured** (issue #17). A check that prints no
values (`test::check`, a contract, an `assert`) fails with its process whole, so the worker starts `smith
--smith-explain --pid <its pid>` (`runtime/explain.c`, `shim/lldb_explain.cpp`: lldb's SB API in smith's process) and
waits; the child attaches, takes the frame `smith symbolize` would report (the first that is not check machinery,
searching from the machinery's own frames outward, so a sanitizer's wrapper of `poll` is never chosen), reads the
variables its source line names by path as `frame variable` does (`moves.count`, `b->cells[row][col]` with an index
that is a variable read first; no expression, no call), formats them with lldb's formatters (a struct as `(count = 7,
...)`, a pointer as its address and pointee, an enum by name), detaches, and prints `@ name = value` lines (`@! why not`)
that join the failure's `smith-trace detail` lines. **Why a child of the failing process and not a rerun under
`smith dbg`:** the process is the failure itself, so shrunk cases, `--isolate`, a worker's state and the real scratch
folder are all right by construction; a rerun would need the draws, the seed and the order. Only a property test that is
shrunk is run again, once, on its final failure (the key and the override table replay it), since the attempts must stay
fast. Each failure is read for at most 5 s (about 0.15 s measured), at most `--explain-max` (5) a run, the rest counted
and said. `smith test` passes `--explain <smith>` when `smith --smith-explain --available` says the smith has lldb and
the arguments have no `--no-explain`; a runner started by hand reads nothing unless asked. A refusal (the account may
not debug, a hardened program) or a smith without lldb is one `note: values not shown: ...` line and the report as it was;
a crash is never read. Enumerators of a `.smith` enum print as lowered (`boom_test_Green`): the debug names of #27 cover
types, functions and variables only.

`smith symbolize [--binary <program>] [<log>]` turns the `smith-trace` lines of a failure (a log, a pipe, standard
input) into the failing line and the call chain; it needs the full smith.
