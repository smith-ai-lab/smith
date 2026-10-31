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
// random_test.smith, beside random.smith
import smith::random;
import smith::test;

void test_addition() {                        // a test: found by name, no registration; pure
    test::check_equal(2 + 2, 4);
}

void test_uniform_stays_in_range(random::Key key) { // a property test: runs on 100 keys
    random::UniformInt64 range = {.low = 0, .high = 6};
    i64 x = random::uniform_int64_sample(key, range);
    test::check_less_equal(x, range.high);
}
```

**Where tests live (issue #294).** A unit test sits beside the module it tests, `<module>_test.smith` (`lib/smith/arena.smith`
has `lib/smith/arena_test.smith`, `module smith::arena_test;`; a C project has `<name>_test.c`), and the convention finds any
`*_test.smith` and `*_test.c` below the root, never linking one into a program. A test module is an ordinary module with
no special access: it sees only the module's exports, a private function is tested through the exports that use it, and
contracts and `smith prove` check the inside (a helper that needs tests of its own is its own module). `tests/` holds the
integration tests, the ones that start the built program or work on whole projects, with their fixtures and helpers; the same
name rule finds them there. `smith new` writes `primes_test.smith` beside `primes.smith` and no `tests/` folder.

```c
// build.c
SmithTests tests = {
    .sources = smith_glob(b, smith_list(b, "**/*_test.smith", "**/*_test.c", nullptr)),
    .objects = (const SmithObject *[]){&code_under_test, nullptr},
    .flags = flags,                                  // for the test files and smith's test library
    .link_flags = link_flags,
};
smith_tests(b, &tests);                              // tests.step is the task "test"
```

- **A test's signature says what it may touch.** `void test_x(void)` is pure; `void test_x(random::Key key)` (`SmithKey`
  in C) is a property test, with the key from the run's seed. A test takes no io: a signature with any other
  parameter list is a build error naming the io-free forms, and a test reaches the world through the direct libraries
  (`files`, `process`, `console`, ...) and replaces them with their fakes (owner, #727: "there is no region and no io
  for a test").
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
  `void test_<name>(SmithKey key) {` (`<name>` in `[a-z0-9_]`, the parameter's name
  in `[a-z_]`) and writes `tests_list.h` (prototypes) and `tests_list.c` (the table, `{name, plain, property, with_failing}`)
  beside the runner. Each test file compiles with
  `-include <tests_list.h> -Werror=missing-prototypes`, so a `test_` function the scan missed because it is formatted
  another way (`void test_x (void) {`) is a compile error. A `static` helper is not a test. A name defined twice is an
  error that names both files; a file with no tests is fine.
  - **Tests in `.smith` files** (name the files in `.sources`, `tests/**/*_test.smith`): in a file with no `module` line
    the same lines are tests, with the same names. In a module (`module geometry::box_test;`) a test is
    `export void test_<name>(void) {`, `export void test_<name>(random::Key key) {` (`c::SmithKey` is the same type as `random::Key` for a file that
    imports `<smith/test.h>` or `<smith/random.h>`); an `export` is needed, since a private function is `static` and nothing can
    call it. Its C name is `geometry_box_test_test_area` and its symbol the export's `geometry::box_test::test_area`, so
    `tests_list.h` declares it with that `__asm__` label. The runner shows and `--test` selects a module's test by its name
    as written: `geometry::box_test::area` (the written `geometry::box_test::test_area`, `test_` of the last part dropped as
    for a C test); `--test` also takes the written form, `--test geometry::box_test::test_area` (or `test_area` for a C
    test). Two tests with the same C name are an error that names both files.
- **The runner's own effects are a capability.** The runner is `smith::test`'s `main`: it hands `run_tests` a `Runner` (`smith_test_run`, which holds the command line), a type marked as a capability
  whose implementation is the module, so the forked workers, their pipes and sockets, the signals they catch and the
  memory they share are the module's own calls, and `smith lint` checks them like any other module's:
  every exported function that reaches one takes the `Runner`. The runner prints the report through `console`, times the tests with
  `clock`, reads `$NO_COLOR` with `env` and lists processes with `popen`. The checks reach the running test's state (the shared block the runner reads after the test)
  through module variables, since a check takes no context parameter; they are the only state a test can reach.
- **The shrinking hook is a field of the key.** `random::Key` has a `tap` (`nullptr` for every key a program makes) and
  `random::split` copies it. The runner gives the keys of its shrinking worker a tap whose `word` records each
  `(key, n)` a property test draws and replaces it from an override table in shared memory; no global of
  `smith::random` is set.
- **`test::expect_abort()`** (`smith_test_expect_abort()` in C) says that from this call on the running test must stop the program on purpose, and is
  a plain function: the test calls it and then runs the code that aborts,
  as its last statements. It records a flag and the call's stack in the block the worker shares with the runner, and the
  runner judges how the test ended. The test passes when it ends with `SIGABRT` or `SIGTRAP` (`SIGILL` for `__builtin_trap` on x86_64) (an `assert`, `abort()`,
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
- **`--explain <smith>`, `--explain-max <n>`, `--no-explain`.** `smith test` starts the runner with `--explain <smith>` so that
  a failed `test::check`, contract or `assert` shows the variables its line names, read from the stopped test with lldb
  ([failure reports](failure-reports-and-tests.md)); the wait for lldb is 5 s of the debugger's CPU time (a 120 s wall backstop reports "the debugger hung"); `--explain-max` (default 5) caps the failures read in a run, and the
  runner prints how many it left; `--no-explain` (to `smith test`) turns it off. `--stats` adds `explained: N failures in
  S s`. With `--in-process` the runner explains itself, so a test run under `smith dbg` does not pass `--explain`.
- **`test::note(format, ...)`** (`smith_test_note`) prints under a failure, whether a failed check, a crash or a timeout; each note
  replaces the last. **`test::memory(capacity)`** (`smith_test_arena`) is the test's memory, from a block the runner reserved once with
  `mmap`, given back before each test and each key.
- **The runner** forks `--jobs` workers (the cores by default), once each and when one is needed, and sends each the
  tests to run, one after another, in its own process. Before each test and each key of a property test the worker gives
  the arenas back, clears the note and starts two clocks, a CPU time limit (`setitimer(ITIMER_PROF)`, `--timeout`) and a wall-clock backstop for a hang (`ITIMER_REAL`, `--wall-timeout`, 600 s); it clears both when the test ends. A failed check
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
  the cores), `--timeout <seconds>` (10 seconds of CPU time, per test and per key: a loaded machine slows a test and never fails it, as for `smith prove`), `--wall-timeout <seconds>` (the backstop for a test that is blocked and so spends no CPU time: 600 s by default, reported as a hang), `--idle-timeout <seconds>` (180 by default, 0 for none: every few seconds, once a test has run a while, the runner lists the processes with `ps`, adds up the CPU time of the test's worker and every process below it, and a test whose tree used less than 1% of the wall time for that long is killed and reported "used no CPU time for N s ...; it was waiting for: <the command of its deepest process>", or "the test itself" when it had none; the kill and every signal reach only the test's own process group: each worker and each `--isolate` child leads one, so a group signal from a test, or the kernel's SIGHUP to an orphaned group, never reaches the runner or the `smith build` that runs it; #754), `--shrink 0|1`, `--shrink-seconds <seconds>` (10, the
  time one shrink may take), `--shuffle <seed>`, `--shard <i>/<n>` (part i of n of the chosen tests, for CI: a test file stays whole in one part, files go heaviest first to the lightest part by the CPU seconds `--costs` names, 0.4 for a test it does not, so every part of one run agrees on the split, and the tests of a part start unknown first, then the heaviest first (`--cheapest` reverses that: `smith test --changed --cheapest-first` meets the cheap breakage first), so the longest test never starts last (#754); `--list` and the run show only that part), `--macos-only` / `--skip-macos` (only the macOS-only tests, or all the others: a test is macOS-only when its shown name has the word `macos`, a `test_macos_...` function or a `..._macos_test` module, so CI runs the others on Linux and these on a Mac; the name is the declaration, there is no list; #754; a filter that names only the other half's tests, a pull request's `--changed` selection on the Mac part, runs nothing and exits 0, while one that names no test at all is still `no test matches`), `--isolate`, `--in-process` (every test in the runner's own process with no fork, for a debugger that does not follow forks; a crash, an abort the test did not expect or a hang ends the whole run, state a test leaves reaches the next, and there is no timeout unless `--timeout` is given and no shrinking), `--stats`, `--trace`, `--color`, `--full`, `--update` (golden files, below), `--list` and `--verbose`. The run ends with `N passed, M failed in T s` and the five
  slowest tests; `--verbose` lists every test.
- **Golden files: `test::golden(path, actual)`** (`lib/smith/test.smith`; owner-decided API, issue #292). The expected output of a case lives in a file in the repository and the test compares its text with it, so a corpus is one file per case and a change of output is a diff in version control, never a script's `UPDATE=1`. The path is relative to the project root (the working directory of the tests). A mismatch is a failed check with a unified diff (expected on the left, 3 lines of context, 40 lines at most, then `... N more lines`, last line `run smith test --update to accept`); a missing file is a failure that names the way to write it. A corpus case of the language (`tests/smith-lang/cases/<name>`) whose output depends on the machine (`u64` is `unsigned long` on Linux and `unsigned long long` on macOS, a program may read `__x86_64__`) keeps the macOS file in `expect/` and the Linux one at the same path in `expect-linux/`, which wins on Linux; the symbols of `expect/symbols` are macOS's (a leading underscore, `T` for a weak definition, `S` for zero-filled data) and Linux's `nm` is rewritten to them. `smith test --update` makes every golden that runs write its file when it differs and pass; the runner lists `wrote <path>` for each. `test::updating()` lets a test that checks a variable set of files write all it made. The diff is made in `smith::test` (a table of the longest common subsequence after the common head and tail are cut; a change too large for the table is shown whole), and `smith symbolize` keeps up to 48 detail lines for it.
- **Full size: `test::full()`** (`smith_test_full()`). It is false by default and true when the run was given `--full`. A test with a heavy check
  picks its size from it, such as 200 meshes by default and 20,000 with `--full`, and checks the same properties either
  way, so `smith test` stays fast and `smith test --full` is the slow pass. It replaces a second task such as game's
  `test-slow`: that becomes `smith test --full`.
- **`.inputs`: files the tests read when they run**, such as compiled shaders, baked meshes and fixtures. They are the
  test task's inputs after the runner, so they are built first and a change to one reruns the tests. The tests read
  them by their path, relative to the project's root, where `smith build` runs them.
- **A property test's keys** are `smith_key_make(seed)`, with the seeds drawn from a fixed root, so a printed seed
  replays exactly.
- **A failure prints** the test, the seed of a property test, the call chain, the values with their
  types, the note, the test's output, and the command that reruns it. The runner on its own
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
  `FAIL name: over its CPU time limit of 10 s (10.3 s of wall time passed; threads add CPU time)` (the CPU time is the process's, every thread's, so the wall time can be shorter), and a blocked one `FAIL name: hung after 600.0 s of wall time (...)` (never a timeout: wall time says nothing about a test on a loaded machine; a test that calls `alarm` itself replaces the backstop, and ends as a hang after its own time). Color goes on only when stdout is a terminal and `NO_COLOR` is unset.
- **A failing property test is shrunk.** The runner reruns the failing key in a worker of its own whose keys carry the runner's
  `random::Tap`, which records each `(key, n)` the test's draws ask for, in first-use order, in
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

**A worker stopped by a timer before it began the test did not run it.** The worker marks its block `began` when it arms the clocks for a test. A worker that dies of SIGPROF or SIGALRM with `began` clear (learner's `prop_recur_ssm_run_is_deterministic` once reported `over its CPU time limit of 10 s (it ran 0.0 s)` with seed 0: the worker, idle between tests, was killed by a timer and the death was charged to the test dispatched next) is not the test's: the runner runs the test again in a fresh worker, up to three times, then reports `the test did not run: its worker was stopped by SIGPROF 4 times before the test began`. `SMITH_TEST_KILL_WORKERS=n` makes the next n commands kill their worker before it begins (with one job), which tests/test_runner_workers_test.smith uses. The kernel's reason for the early signal was not found.

**The front end has a runner of its own** (#1115, track B of #1112). The units runner (`tests/units/build.c`) would be relinked against static LLVM on every test change, so the new front end's tests (`tests/front_*_test.smith`, with every `front/*.smith`) are a second runner, a project of its own like the units runner: `tests/front_units/build.c` in `<folder>/front-project`, whose `front`, `tests` and `lib` are links to the repository's, built and run by the smith under test. `build_front_units` in `build.c` makes the tasks `front-build` and `test-front` (a `.test = true` step, so `smith test` and `smith check` run it) and gives it `SMITH_REPO` and `SMITH_LLVM`, the LLVM install smith links. It has the units runner's flags, LLVM's headers with `-I` (an `-isystem` folder would make `smith-ownership` ask libc's table about every handle LLVM returns), every `libclang[A-Z]*.a` and `libLLVM*.a` in a group with the C++ library, every `shim/cimport*.cpp` (track C's edge to clang, none yet) compiled as C++ with the shim flags under the sanitizers and linked in, and, on macOS, `-dead_strip` with `-u ___asan_default_options` (without the root, dead-stripping removes `smith_asan_options.o`, which nothing references, and ASan aborts at start-up with "interceptors not installed"), and `mem::checked` off: LLVM's pass registry and targets are allocated once for the process and never freed, which the per-test leak check would blame on the first test (ASan's leak check stays on). The units runner leaves `tests/front_*` out. `tests/front_emit_test.smith` is the wiring's proof: it calls LLVM's C API from `.smith` through `c::` (a context, a module, `i32 answer(void)`, `LLVMVerifyModule`) and runs the function through Orc's LLJIT.
