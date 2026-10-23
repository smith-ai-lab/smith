---
type: design
title: Tests, smith_tests and smith::test
description: smith_tests links the test files with the libraries they import into a runner whose workers run the tests, with seeds, shrinking and isolation.
tags: [test, build]
---
# Tests, smith_tests and smith::test

Tests are written once and done well, in smith: a plain function per test, checks that print both values with their
types, workers that run the tests in parallel, and a runner that is the library's `main`. The library is the module
`smith::test` (`lib/smith/test.smith`, issue #45); C includes its generated header `<smith/test.h>`. `smith_tests` (the
call is in the API below, with the exact commands) builds the runner from the project's test files and adds the task
`test` (a task with `.test`, which `smith test` runs). It needs a smith with clang inside, since the library is a
`.smith` module.

```smith
// tests/test_random.smith
import smith::random;
import smith::test;

void test_addition(void) {                        // a test: found by name, no registration; pure
    test::check_equal(2 + 2, 4);
}

void test_uniform_stays_in_range(random::Key key) { // a property test: runs on 100 keys
    random::UniformInt64 range = {.low = 0, .high = 6};
    i64 x = random::uniform_int64_sample(key, range);
    test::check_less_equal(x, range.high);
}
```

```c
// build.c
SmithTests tests = {
    .sources = smith_glob(b, smith_list(b, "tests/test_*.smith", "tests/test_*.c", nullptr)),
    .objects = (const SmithObject *[]){&code_under_test, nullptr},
    .flags = flags,                                  // for the test files and smith's test library
    .link_flags = link_flags,
};
smith_tests(b, &tests);                              // tests.step is the task "test"
```

- **A test's signature says what it may touch.** `void test_x(void)` is pure; `void test_x(random::Key key)` (`SmithKey`
  in C) is a property test, with the key from the run's seed; `void test_x(io::Io *io)` (`SmithIo *io` in C) gets a
  real io made for that one test by the runner (below). A test makes fakes (`io::make_test`) anywhere, but never a
  real io: `smith-effects` allows `io::make` only in `main`, so the runner can always seed, sandbox and clean up
  after a test.
- **Checks are generic functions.** A failure shows the line that called the check and the call chain, read from the
  call stack and the source file (see "Failure reports"), so a check needs no macro to capture its text or its place.
  A test is a plain function too; there is no `TEST()` macro and no registration. `test::check(c)`,
  `test::check_equal(a, b)`, `check_not_equal`, `check_less`, `check_less_equal`, `check_greater`,
  `check_greater_equal`, `check_near(a, b, tolerance)`, `check_same_bits(a, b)` and `expect_abort()`. The type is
  `a`'s (`auto a`, smith-lang-generics) and `b` converts to it by C's rules, a conversion that changes the value being an
  error at the call, fixed by a cast (`test::check_equal(my_u8, (u8)big)`). They check `bool`,
  `char`, integers, `f32`, `f64` and pointers. A failed one prints `a` and `b` (and the tolerance and the distance of
  `near`) with the sized type name: `a = 3   (u64)`. A NaN is unordered, so every comparison but `check_not_equal` fails
  on one. The macros `SMITH_CHECK`, `SMITH_CHECK_NEAR` and `SMITH_CHECK_SAME_BITS` are gone, and the generated header
  defines no macro.
- **C keeps its API as named instances:** `smith_check(bool)`; `smith_check_equal_<type>(a, b)` for `bool`, `char`,
  `i8` `i16` `i32` `i64`, `u8` `u16` `u32` `u64`, `f32`, `f64` and `ptr`; `smith_check_near_f32(a, b, tolerance)` and
  `_f64`; `smith_check_same_bits_f32(a, b)` and `_f64`; and `smith_test_expect_abort()`. The type is in the name, as
  `<math.h>` names `sinf` and `sin`. C has the ordering checks only when a C caller needs one.
- **Found by name, in the files build.c names.** build.c says which files are tests and how they compile and link,
  because only build.c knows that. A step scans them for lines that are exactly `void test_<name>(void) {`,
  `void test_<name>(SmithKey key) {` or `void test_<name>(SmithIo *io) {` (`<name>` in `[a-z0-9_]`, the parameter's name
  in `[a-z_]`) and writes `tests_list.h` (prototypes) and `tests_list.c` (the table, `{name, plain, property, with_io}`)
  beside the runner. Each test file compiles with
  `-include <tests_list.h> -Werror=missing-prototypes`, so a `test_` function the scan missed because it is formatted
  another way (`void test_x (void) {`) is a compile error. A `static` helper is not a test. A name defined twice is an
  error that names both files; a file with no tests is fine.
  - **Tests in `.smith` files** (name the files in `.sources`, `tests/**/*_test.smith`): in a file with no `module` line
    the same lines are tests, with the same names. In a module (`module geometry::box_test;`) a test is
    `export void test_<name>(void) {`, `export void test_<name>(random::Key key) {` or
    `export void test_<name>(io::Io *io) {` (`c::SmithKey` is the same type as `random::Key` for a file that imports
    `<smith/test.h>` or `<smith/random.h>`, and `c::SmithIo` the same as `io::Io`; with `import smith::io as sio;` the
    type is `sio::Io`); an `export` is needed, since a private function is `static` and nothing can
    call it. Its C name is `geometry_box_test_test_area` and its symbol the export's `geometry::box_test::test_area`, so
    `tests_list.h` declares it with that `__asm__` label. The runner shows and `--test` selects a module's test by its name
    as written: `geometry::box_test::area` (the written `geometry::box_test::test_area`, `test_` of the last part dropped as
    for a C test); `--test` also takes the written form, `--test geometry::box_test::test_area` (or `test_area` for a C
    test). Two tests with the same C name are an error that names both files.
- **A test that takes `SmithIo *io`** is run with a real io the runner makes for that test alone, in the worker: its key
  and `seed` come from the run's seed (`--seed N` replays it; without `--seed` the seed is the runner's own, printed by
  a failure as `with seed N` and `rerun: smith test --test name --seed N`), its arena is fresh, and it has a scratch
  folder of its own, `test::scratch(io)` (`smith_test_scratch` in C), named by the runner's pid and the test's row under
  `$TMPDIR` (else `/tmp`) and also set as `$TMPDIR` and `$SMITH_TEST_SCRATCH` while the test runs. The runner removes
  the folder (with `rm -rf`, through its io's `processes`) when the test passed and keeps it, printing
  `scratch folder (kept): <path>` under the failure, when it failed. The working directory stays the project root, so
  fixtures resolve. The runner can make the io because `main` hands it `io::make` as a value (`Runner.make_io`).
- **The runner's own effects are a capability.** The runner is `smith::test`'s `main`: it makes its io once
  (`io::make`, in `main`) and hands `run_tests` a `Runner` (`smith_test_run`), a type marked as a capability
  whose implementation is the module, so the forked workers, their pipes and sockets, the signals they catch and the
  memory they share are the module's own calls, and `smith lint` (smith-effects) checks them like any other module's:
  every exported function that reaches one takes the `Runner`. What io has a part in goes through it: the runner's
  console prints the report, its clock times the tests, its arguments hold `$TMPDIR` and `$NO_COLOR`, and its processes
  remove a scratch folder. The checks reach the running test's state (the shared block the runner reads after the test)
  through module variables, since a check takes no context parameter; they are the only state a test can reach.
- **The shrinking hook is a field of the key.** `random::Key` has a `tap` (`nullptr` for every key a program makes) and
  `key_derive` copies it. The runner gives the keys of its shrinking worker a tap whose `word` records each
  `(key, n)` a property test draws and replaces it from an override table in shared memory; no global of
  `smith::random` is set.
- **`test::expect_abort()`** (`smith_test_expect_abort()` in C) says that from this call on the running test must stop the program on purpose, and is
  a plain function: the test calls it and then runs the code that aborts,
  as its last statements. It records a flag and the call's stack in the block the worker shares with the runner, and the
  runner judges how the test ended. The test passes when it ends with `SIGABRT` or `SIGTRAP` (an `assert`, `abort()`,
  `__builtin_trap`, the arena's capacity stop, a failed contract or library check) and its output holds no sanitizer
  report. It fails, at the call's file and line from its stack, when the test returns ("the test returned"), exits ("it
  exited with status N"), crashes with another signal such as `SIGSEGV` ("it crashed"), fails a check ("a check failed,
  which is not an abort"), or is stopped by a sanitizer report ("AddressSanitizer", "UndefinedBehaviorSanitizer",
  "LeakSanitizer", "ThreadSanitizer" or "runtime error:" in its output: a bug, not the stop the test expected). So a
  memory bug before the assertion cannot make the test pass. An abort before the call is an ordinary failure, and a hang
  is the test's timeout. Nothing in the test runs after the abort, so there is no jump buffer: the signal ends the
  worker (or the child of `--isolate`), whose end the runner reads, as for any crash; `--in-process`, which has no
  process to end, jumps to its loop. A second call in a test is harmless. The test's output is shown only for a failure,
  as always.
- **`test::note(format, ...)`** (`smith_test_note`) prints under a failure, whether a failed check, a crash or a timeout; each note
  replaces the last. **`test::memory(capacity)`** (`smith_test_arena`) is the test's memory, from a block the runner reserved once with
  `mmap`, given back before each test and each key.
- **The runner** forks `--jobs` workers (the cores by default), once each and when one is needed, and sends each the
  tests to run, one after another, in its own process. Before each test and each key of a property test the worker gives
  the arenas back, clears the note and starts two clocks, a CPU time limit (`setitimer(ITIMER_PROF)`, `--timeout`) and a wall-clock backstop (`ITIMER_REAL`, `--wall-timeout`); it clears both when the test ends. A failed check
  unwinds with `siglongjmp` to the worker's loop and the result goes back through shared memory, so the worker lives
  on. A crash, a timeout, an exit, output past 16 MiB or a sanitizer's report (in the output of a failed or a passing
  test) ends the worker: the runner records the failure as it always has, forks a fresh worker and goes on, so a
  crash fails only its own test. A worker also ends after any failed test, because a failed check unwinds out of
  arbitrary code and can leave state half-done, and after any test that called `smith_test_expect_abort`, passed or failed,
  because an abort stands for the program ending: a worker is reused only after a clean pass that expected no abort
  check (the shrinking worker is reused across its failing attempts, so the runner reruns the final shrunk case once in a fresh worker: if that does not fail the same way, the failure is shown unshrunk with a line saying state leaked between attempts). What a test leaves in global variables, files
  and the process stays for the next test on that worker after such a pass, and which tests share a worker depends on timing unless `--jobs 1`; `--shuffle <seed>` runs the
  tests in a shuffled order to find such a dependence (it replays with `--jobs 1`), and `--isolate` keeps the old
  process per test, exactly. `--stats` prints how many forks the run made. Results are
  printed in table order, in either mode. Output is captured and
  shown only for a failing test (the first 64 KiB; a worker that writes 16 MiB in one test is killed). The options are the runner's arguments, which `smith test` gives it
  (`smith_args`): `--test <substring>` (a test named exactly that, else every test whose name contains it; a name written with `test_`, as in the source, selects the same test), `--seed <n>`
  (each property test on that one seed), `--seeds <n>` (keys per property test, 100 by default), `--jobs <n>` (default:
  the cores), `--timeout <seconds>` (10 seconds of CPU time, per test and per key: a loaded machine slows a test and never fails it, as for `smith prove`), `--wall-timeout <seconds>` (the backstop for a test that is blocked and so spends no CPU time: 10 times `--timeout` by default), `--shrink 0|1`, `--shrink-seconds <seconds>` (10, the
  time one shrink may take), `--shuffle <seed>`, `--isolate`, `--in-process` (every test in the runner's own process with no fork, for a debugger that does not follow forks; a crash, an abort the test did not expect or a hang ends the whole run, state a test leaves reaches the next, and there is no timeout unless `--timeout` is given and no shrinking), `--stats`, `--trace`, `--color`, `--full`, `--list` and `--verbose`. The run ends with `N passed, M failed in T s` and the five
  slowest tests; `--verbose` lists every test.
- **Full size: `test::full()`** (`smith_test_full()`). It is false by default and true when the run was given `--full`. A test with a heavy check
  picks its size from it, such as 200 meshes by default and 20,000 with `--full`, and checks the same properties either
  way, so `smith test` stays fast and `smith test --full` is the slow pass. It replaces a second task such as game's
  `test-slow`: that becomes `smith test --full`.
- **`.inputs`: files the tests read when they run**, such as compiled shaders, baked meshes and fixtures. They are the
  test task's inputs after the runner, so they are built first and a change to one reruns the tests. The tests read
  them by their path, relative to the project's root, where `smith build` runs them.
- **A property test's keys** are `smith_key_make(seed)`, with the seeds drawn from a fixed root, so a printed seed
  replays exactly.
- **A failure prints** the test, the seed of a property test or an io test, the call chain, the values with their
  types, the note, the kept scratch folder, the test's output, and the command that reruns it. The runner on its own
  (by hand) prints the raw trace of a failed check, then the values; `smith build test` prints the report of "Failure
  reports" in its place:

  ```
  FAIL uniform_stays_in_range with seed 6387777158891554393, shrunk in 41 runs
    check failed at tests/test_random.smith:12
         12 |     test::check_less_equal(x, range.high);
              x = 7, range.high = 6   (i64)
    called from
      tests/test_random.smith:12  test_uniform_stays_in_range
      note: <the note, if any>
      <the test's output, indented, if any>
      rerun: smith test --test uniform_stays_in_range --seed 6387777158891554393
  ```

  A crash is `FAIL name: killed by SIGSEGV` with the captured stderr (a sanitizer's report), a test over its CPU limit is
  `FAIL name: over its CPU time limit of 10 s (10.3 s of wall time passed; threads add CPU time)` (the CPU time is the process's, every thread's, so the wall time can be shorter), and a blocked one `FAIL name: timed out after 100.0 s` (the wall time it ran: a test that calls `alarm` itself replaces the backstop, and ends as a timeout after its own time). Color goes on only when stdout is a terminal and `NO_COLOR` is unset.
- **A failing property test is shrunk.** The runner reruns the failing key in a worker of its own whose keys carry the runner's
  `random::Tap`, which records each `(key, n)` the test asks `random::key_bits` for, in first-use order, in
  shared memory (at most 65,536 words; past that it stops recording). Every distribution maps word 0 to its simplest
  value (0 or the bound nearest it for an integer, about the mean for a normal, false, the identity permutation). For
  each recorded word in order the runner tries 0; if the test then stops failing it binary-searches between 0 and the
  word for the smallest one that still fails. Each try is a run in that worker, which reads an override table from shared memory; a try that kills the worker costs a fork, and nothing else does.
  A try is kept only when the failure is the same: the same check (the same stack), or the same signal or exit status;
  a failure that moves to another check is not kept. Passes repeat until none improves, within 1,000 runs or 10
  seconds, whichever comes first, and the same seed gives the same shrunk case (when the runs limit ends it; the
  seconds limit depends on the machine). The report is the shrunk failure, with its values, its note and its output,
  and `shrunk in k runs`; the rerun line names the original seed, and rerunning it shrinks to the same case again.
  `--shrink 0` shows the unshrunk failure. Shrinking starts only when every other test has finished (a shrinkable
  failure and the results after it are held until then, and printed in order), so a result never depends on another
  test: nothing stops reading the other workers' pipes or judging their timeouts. A timeout is not shrunk (each try would take the whole timeout), and a failure
  that does not reproduce when its key is run alone (it depended on an earlier key, or on chance) is shown as it was.
- **Failure reports.** A failure is a stack of addresses, and smith turns it into the failing line and the call chain.
  This is `<smith/trace.h>` in the program and `smith symbolize` in smith.
  - **In the program (`lib/trace.c`, `smith_library(b, "trace")`; `smith_tests` compiles it itself, as the contract library):** on a failed
    check, a failed contract (`smith_failure(kind)`), an `assert` or a fatal signal, the program walks its stack by frame
    pointers, which test builds keep (Apple's ABIs always do; sanitized builds pass `-fno-omit-frame-pointer`). A
    signal's walk starts from the `ucontext` the handler is given, with the exact instruction first. It writes the raw
    addresses to the runner's output as lines that start with `smith-trace`: a heading, the Mach-O UUID, `__TEXT`
    address, slide and size of each image a frame falls in, and the frames (with the nearest symbol's name when the
    writer is not a signal handler). A signal handler's part is async-signal-safe (`write(2)` and hand-formatted hex). No
    symbolizer lives in the program, so a test binary stays as small and as quick to link as before. The walk ends at
    `smith_test_execute`'s frame (`smith_trace_stop`), so a stack is the same however the runner reached the test, which
    shrinking compares. The runner prints the block when it is given `--trace` (the test task passes it when smith can
    symbolize), and for a failed check always, since a check has no file and line of its own.
  - **In smith (`runtime/symbolize.c`, `shim/symbolize.cpp`):** `smith build test` pipes the runner's output through
    `smith symbolize` (the stage0 has no LLVM, so it runs the runner as before). The filter copies its input and replaces
    each block with the report:

    ```
    FAIL moves_every_step
      check failed at tests/moves_test.c:42
           42 |     smith_check(moves.count == expected);
      called from
        tests/moves_test.c:42  moves_every_step
        .smith/lib/smith/test.smith:1065  smith_test_execute
    ```

    The failing frame is the first that is not smith's own machinery (a function whose name starts with `smith_test_`,
    `smith_check`, `smith_requires`, `smith_ensures`, `smith_invariant`, `smith_failure`, `smith_contract_` or
    `smith_trace_`, or `smith::test::`, `smith::contract::` and `smith::trace::` for the libraries as modules, and the older
    `smith.test.` and `smith.contract.`) and not in a system library, so libc's
    abort path (`__pthread_kill`, `abort`, `__assert_rtn`) is skipped with no annotation. Inlined frames are listed as the
    debug info has them, innermost first. A `.smith` function is shown as written (`geometry::box::area`; the debug names
    of issue #27), a module's test as `geometry::box_test::test_area_fails`. The chain ends at the first frame of the runner or at `main`. The source line
    is read from the file; if the file is newer than the program a note says the line may have changed. A stack with no
    debug info says to build with `-g`. A crash is
    `FAIL name: killed by SIGSEGV` and then `crashed at file:line`; an `assert` is `aborted at file:line` with libc's
    message under it. A sanitizer's report is not given a trace of its own: its stack is the report.
  - **`smith symbolize [--binary <program>] [<file>]`** does the same for a trace saved from a run anywhere (a log, a crash
    report, standard input). `--binary` names the program when the path in the trace is gone. A program that is not the
    one that ran (the UUID differs) is a note and no lines, never a wrong line. `smith symbolize --available` exits 0 in
    the full smith.
  - **Sanitizers.** smith run under the name `llvm-symbolizer` speaks that program's protocol (`CODE "<module>[:arch]"
    <address>` per line; the function and `file:line:column` of each frame, inlined frames first, then an empty line;
    `--relative-address` adds `__TEXT`'s link address to an offset). `smith build test` makes `.smith/bin/llvm-symbolizer`,
    a link to smith, and sets `ASAN_SYMBOLIZER_PATH` and `UBSAN_SYMBOLIZER_PATH` to it for the runner (unless the
    environment names one already). AddressSanitizer and UBSan stacks then come from the same code as the test report,
    with lines and inlined frames, and without `atos` (which needs a dSYM to give lines).
  - **Where the DWARF is: the debug map, not a dSYM.** On macOS the linker leaves the DWARF in the object files, and
    the binary holds a debug map (stab entries: `N_OSO` names an object and its modification time, `N_FUN` gives each
    function's address and size). LLVM's symbolizer reads a dSYM, which `dsymutil` makes by reading every object and
    linking its DWARF. smith reads the debug map: find the function an address is in, find the same symbol in its object
    file, and ask the symbolizer about the object, which has its own DWARF. Measured on this machine (clean cache, 5
    runs, fastest): a failure costs 6 to 10 ms beyond smith's 8 ms of start, and each further failure of the same run
    about 0.1 ms (game's sanitized tests, 10 MB, 62 objects: 18 ms for one failure and 28 ms for 100; learner's, 5 MB, 57
    objects: 14 ms and 23 ms). `dsymutil` on the same binaries takes 81 to 146 ms (game) and 36 to 75 ms (learner); made at
    every link it adds that to every test build, passing or not, and writes a bundle a clean has to remember. A dSYM beside
    the program, when its UUID matches, is used first, so a project that makes one gains nothing and loses nothing. The
    price of the debug map is that the objects must be there and unchanged: an object that is gone, newer than the link
    or inside an archive is a note ("the object file ... has changed since the program was linked"), never a guess. The
    objects of a test build are in `build/`, where the build put them, which is where they are when a test fails. lld
    (smith's default linker) and Apple's `ld` write the same stabs, and both are tested.
  - **For `.smith` and #8:** `smith_failure(kind)` (`<smith/trace.h>`) is the entry point `smith_requires`, `smith_ensures`
    and `smith_invariant` call when their condition is false. In a test it fails the test with the report above; in a
    program it writes the heading and the trace block to standard error and calls `abort()`, so a log holds what `smith
    symbolize` needs. The caller's frame is the failing one: a wrapper whose name starts with a prefix above is skipped.
- **`smith_tests` links the libraries the runner and `<smith/contract.h>` need.** The test library uses the arena and
  random, a failure's report uses trace, and code under test that makes a contract calls the contract library. Each is
  compiled by `smith_tests` with the test flags, unless `.objects` holds the project's own object of it (compiled from
  `smith_library(b, "arena")` and so on), so nothing is defined twice and a test of code with contracts links with
  nothing added. The libraries the test files import or include (`import smith::inflate;`, `<smith/json.h>`), and those
  they need, are linked the same way. The explicit part is `.objects`, where a library only the code under test uses goes.
- The test files get `compile_commands.json` entries through `smith_object`, so clangd reads them with the project's
  flags (`smith lsp --check` on a test file reports no errors).

**A worker stopped by a timer before it began the test did not run it.** The worker marks its block `began` when it arms the clocks for a test. A worker that dies of SIGPROF or SIGALRM with `began` clear (learner's `prop_recur_ssm_run_is_deterministic` once reported `over its CPU time limit of 10 s (it ran 0.0 s)` with seed 0: the worker, idle between tests, was killed by a timer and the death was charged to the test dispatched next) is not the test's: the runner runs the test again in a fresh worker, up to three times, then reports `the test did not run: its worker was stopped by SIGPROF 4 times before the test began`. `SMITH_TEST_KILL_WORKERS=n` makes the next n commands kill their worker before it begins (with one job), which tests/test.sh uses. The kernel's reason for the early signal was not found.
