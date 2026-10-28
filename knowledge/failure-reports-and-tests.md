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
`llvm-symbolizer` serves the sanitizers. The checks are the generic functions of `std::test` (`test::check_equal(a, b)`, `check_less`, `check_near`, ...; the
`SMITH_CHECK` macros are gone), and a test that must abort calls `test::expect_abort()`. **A test's signature says what it
may touch** (issue #45): `void test_x(void)` is pure, `void test_x(random::Key key)` a property test (the key from the
run's seed), `void test_x(io::Io *io)` gets a real io the runner makes for it alone (its key from the run's seed, a
scratch folder removed when it passes and kept, named, when it fails, the working directory still the project root);
a test makes fakes anywhere and never a real io (`smith-effects`). The runner is `std::test`'s `main`; its own
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
a crash is never read. An enumerator of a `.smith` enum is lowered to `<module>_<name>` in the debug info, so the shim writes it as its
type is named (`board::DARK_P1` of `board::Cell`); a one-byte integer prints as a number (and its character when it
prints), not lldb's `'\x02'`.

On Linux the same lines are written: `lib/trace.c` finds the images with `dl_iterate_phdr` and names each by its ELF
build-id (`smith_program` links every program with `--build-id=md5` and `--export-dynamic`, so a raw frame has its
name), a signal's frames start at the context's `rip` (x86-64) and the call site above libc's abort path is found on
the stack, and the symbolizer reads the DWARF of the executable itself (no debug map).

`smith symbolize [--binary <program>] [<log>]` turns the `smith-trace` lines of a failure (a log, a pipe, standard
input) into the failing line and the call chain; it needs the full smith.

**A failure at compile time has the same form** (issue #103). A failed `contract::requires`, `ensures`, `invariant`, `assert` or `test::check` inside a call clang evaluates where C requires a constant is a compile error at the outermost call. The mechanism is clang's `annotate` attribute, recognized by the evaluator (`patches/clang-constexpr-c.patch`, gated like the rest): `smith_failure` is `smith_compile_error`, and `smith_contract_*` and `smith_test_check_condition` are `smith_compile_check` (a true first argument is a no-op, false is the failure), so contracts stay plain function calls. clang's notes are one `in call to` per call, innermost first; the `Presenter` in `shim/hook.cpp` skips the library's machinery as the run-time report does (the names `smith_failure` skips) and turns it into one note at the contract's line, `requires failed in call to 'contract::requires(false)'`, followed by the stack as clang prints it, each with file, line and arguments. The step-limit and depth errors are the same shape: a note for the reason, then the stack. Corpus: `error-constexpr-contract`, `error-constexpr-contract-deep` (under `-fmodules`).
