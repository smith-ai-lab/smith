---
name: smith-test
description: "Using smith::test (import smith::test; test::check_equal) and smith_tests, smith's tests: checks that print both values, a test's signature that says what it may touch, workers that run the tests, property-based tests on keys with seeds and shrinking, tests with a real io in a scratch folder. Load when code imports smith::test or includes <smith/test.h>, when a build.c calls smith_tests, when running smith test, or on a question about writing, replaying (--seed) or debugging a .smith or C test, property-based testing, shrinking or flaky tests."
---

# Tests: smith::test

This skill ships in the smith plugin, from `.agents/skills/smith-test/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. The library is the module `smith::test` (`import smith::test;`); C includes its generated header `.smith/include/smith/test.h`. The build side is in the smith-build skill; random keys are in smith-random, the io a test may take is in smith-io.

## Write a test

A test is a plain function in a file build.c names. There is no `TEST()` macro and no registration: the build finds the functions by name, so the definition line must be exactly one of these (clang-format keeps it). **The signature says what the test may touch:**
- `void test_x(void)` is pure: it touches nothing outside its own memory.
- `void test_x(random::Key key)` is a property test: it runs on 100 keys drawn from a fixed root, and a failure prints the seed that replays it (`SmithKey` in C).
- `void test_x(io::Io *io)` takes a real io (`SmithIo *io` in C): the runner makes it for that one test, with its key made from the run's seed (so `--seed` replays it: `io->seed` and `io->key`), a fresh arena, and a scratch folder of its own, `test::scratch(io)`, which is also `$TMPDIR` while the test runs. The folder is removed when the test passes and kept, with its path printed, when it fails. The working directory stays the project root, so fixtures resolve. A test makes fakes (`io::make_test`) anywhere, but never a real io: `smith-effects` allows `io::make` only in `main`, so the runner can always seed, sandbox and clean up after a test, and parallel tests never share the real world.

A test file is a module named for what it tests, `tests/primes_test.smith` is `module primes_test;` (`tests/` is a root of its own), and each test is `export`ed: a private function of a module is static, and the runner calls a test from outside. The runner lists `primes_test::the_first_five_primes_add_up_to_28`. `smith new` writes it so, and `smith lint` (`smith-module`) says to add the module line to a file that has none (such a file still builds, and its tests need no `export`). Write the type argument of a check, as `smith new`'s test does (`test::check_equal(i32, primes::sum(5), 28)`); it is inferred when left out.

```smith
module math_test;

import smith::arena;
import smith::io;
import smith::random;
import smith::test;

export void test_addition(void) {
    test::check_equal(i32, 2 + 2, 4);
    test::check_near(f64, 0.1 + 0.2, 0.3, 1e-9);
}

export void test_sorted_stays_sorted(random::Key key) { // a property test: runs on 100 keys
    arena::Arena a = test::memory(1 << 16); // this test's memory, given back before each key
    usize count = (usize)random::uniform_int64_sample(random::key_derive(key, 0), (random::UniformInt64){.low = 0, .high = 100});
    test::note("count %zu", count); // printed under a failure
    usize *items = arena::push_array(&a, usize, count);
    for (usize i = 0; i < count; i++) {
        items[i] = (usize)random::uniform_int64_sample(random::key_derive(key, i + 1), (random::UniformInt64){.low = 0, .high = 9});
    }
}

export void test_writes_a_file(io::Io *io) { // a real io, in a scratch folder that is removed when the test passes
    test::check(test::scratch(io) != nullptr);
    io::Error wrote = io::files_write(&io->files, "scratch.txt", "hi", 2, io::Replace);
    test::check_equal(wrote.code, 0);
}
```

- `<name>` is `[a-z0-9_]`, the parameter's name `[a-z_]`. The build finds the property test by the literal text `SmithKey`, `c::SmithKey` or `random::Key` (`<alias>::Key` for `import smith::random as <alias>;`), and the io test by `SmithIo *`, `c::SmithIo *` or `io::Io *` (`<alias>::Io` for `import smith::io as <alias>;`). A test written another way (`void test_x (void) {`, or on two lines) is a compile error: "no previous prototype". A helper is `static`. A test file imports the C headers it calls (`import <string.h>;` for `c::strcmp`): the header of `smith::test` carries none, so a `c::strcmp` that only worked because that header included `<string.h>` fails with `call to undeclared library function 'strcmp'` until the file imports it.
- A property test draws everything from its key (derive one key per use) and is run on `--seeds` keys. The seed in a failure replays it exactly.
- `test::memory(capacity)` is the test's memory (an `arena::Arena`), given back before each test and each key. `test::note(format, ...)` prints under a failure; each note replaces the last. `test::full()` is true when the run was given `--full`.

### Tests in modules

Name the `.smith` files in `.sources` (`smith_glob(b, smith_list(b, "tests/test_*.smith", "tests/**/*_test.smith", nullptr))`). The same lines are tests:

```smith
// tests/geometry/box_test.smith
module geometry::box_test;

import smith::random;
import smith::test;

export void test_area(void) { // `export`: a private function is static, nothing can call it
    test::check_equal(3 * 2, 6);
}

export void test_area_property(random::Key key) {
    test::check(random::key_bits(key, 0) == random::key_bits(key, 0));
}
```

- Test files are compiled with `-Werror=missing-prototypes`, on purpose: a test the finder does not recognise (a misspelt signature) has no prototype in the generated list and fails the build instead of silently not running. The cost is that a helper function in a test file needs `static` when the file has no `module` line (in a module every non-`export` function is private already): "no previous prototype for function 'helper'" means make it `static`.
- In a file with no `module` line, `void test_x(void) {` is a test with no `export`, named `x`. In a module the test is shown, listed and selected by its name as written, `geometry::box_test::area` (`smith test --test geometry::box_test` runs the module's tests; `--test geometry::box_test::test_area` works too, `test_` as in the source). Two files cannot define the same test (a module's tests are `a::b::test_x`, so only the same module and name collide).
- A failure names the `.smith` line and its source, and the function as written (`tests/geometry/box_test.smith:12  geometry::box_test::test_area_fails`). Frames of the test library (`smith_test_*`, `smith::test::*`) and of check functions are left out of the report.

## The checks

The checks are generic functions of `smith::test`: the type is inferred from the arguments, so there is nothing to spell (smith-lang-generics). A failure shows the line that called the check and the call chain, read from the stack and the source file, so a check needs no macro.

```smith fragment
test::check(condition)                      // a bool
test::check_equal(a, b)                     // a == b; bool, char, integers, f32, f64 and pointers; both values print with their type
test::check_not_equal(a, b)                 // a != b (a NaN is not equal to anything, so this passes for one)
test::check_less(a, b)                      // also check_less_equal, check_greater, check_greater_equal; a NaN is unordered, so these fail on one
test::check_near(a, b, tolerance)           // |a - b| <= tolerance, f32 or f64; a NaN fails; equal infinities pass
test::check_same_bits(a, b)                 // bitwise identical, f32 or f64: -0 isn't 0, a NaN is itself; prints both in hex
test::expect_abort()                        // from this call on the test must abort (below)
```

- **One type for `a` and `b`.** `T` is told from both: a literal adapts (`check_equal(count, 3)` is the type of `count`; so is `2 + 2` or `nullptr` against a pointer), and arithmetic, comparisons and `?:` are typed by C's rules (`check_equal(x * 2, y + 1)`). Two arguments of different types (`u32` against `u64`, `usize` against `i32`) are the error ``conflicting types for `T`: u32 and u64``: cast one side, or write the type first, `test::check_equal(u64, a, b)`. A call to a generic or a builtin (`math::max(u64)`, `__builtin_nan("")`) is not typed: put it in a variable or write the type first.
- Types print as Smith spells them: `i8` to `i64`, `u8` to `u64`, `f32`, `f64`, `bool`, `char`, `ptr`.
- A failed check ends the test.
- **`test::check(condition)` is an assumption to clang's static analyzer** (`smith lint`'s `clang-analyzer-*`, the editor), as `contract::assert` is: after `test::check(p != nullptr)` the analyzer knows `p` is not null, so it reports no null dereference past it and no `__builtin_assume` is needed. It is the one check that tells the analyzer, so write a null or bounds fact as `check(p != nullptr)`, not as `check_not_equal(p, nullptr)`.
- `test::expect_abort()` is a plain function that says "from this call on, the test must abort". Call it, then run the code that aborts, as the test's last statements (`(void)expr;` where an unused result would warn):

  ```smith
  import smith::contract;
  import smith::test;

  static i32 halve(i32 x) {
      contract::requires(x % 2 == 0);
      return x / 2;
  }

  void test_halve_refuses_odd(void) {
      test::expect_abort();
      (void)halve(5);
  }
  ```

  It is strict: the test passes only if it then ends with SIGABRT or SIGTRAP (an `assert`, `abort()`, `__builtin_trap`, the arena's capacity stop, a failed contract) and its output holds no sanitizer report ("AddressSanitizer", "UndefinedBehaviorSanitizer", "LeakSanitizer", "ThreadSanitizer", "runtime error:"). It fails, at the call's file and line and showing the test's output, if the test returns ("expected an abort; the test returned"), exits, crashes with another signal (SIGSEGV), fails a check, or is stopped by a sanitizer report, so a memory bug before the assertion cannot pass. An abort before the call is an ordinary failure. Nothing runs after the abort, so one test holds one expected abort (a second call is harmless); test two aborts in two tests. A hang is the test's timeout (the wall-clock backstop).

## Run them

`smith test`. Its arguments are the runner's own options, so `smith test --test <substring>` runs one test (or every name that contains it), `--seed <n>` a property test or an io test on that one seed, `--seeds <n>` sets the keys per property test (100), and there are `--jobs <n>`, `--timeout <seconds>` (10 seconds of CPU time per test and per key, so a loaded machine slows a test and never fails it; a test that is blocked, asleep or waiting on a pipe, spends none, so `--wall-timeout <seconds>`, 10 times `--timeout` by default, ends it), `--shrink 0|1`, `--shuffle <seed>`, `--isolate`, `--in-process` (no fork, for a debugger), `--stats` (prints how many forks the run made), `--full` (the heavy checks at full size), `--list` and `--verbose`. `-D` options are build.c's own (`smith test -DFAST`). The runner is `build/tests`, so under a debugger it is `smith dbg build/tests -- --test name --in-process`. `--in-process` runs the tests one after another in the runner's own process, because lldb on macOS does not follow forks (`--jobs 1` and `--isolate` still fork, so a breakpoint in a test kills its child with SIGTRAP). It gives up isolation: a crash, an abort the test did not expect or a hang ends the whole run, state one test leaves reaches the next, there is no timeout unless `--timeout` is given, and no shrinking (a failing property test prints its seed; replay it with `--seed`). An io test's seed is the run's own unless `--seed` is given, and a failing one prints `rerun: smith test --test <name> --seed <n>` like a property test.

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

An `assert` is `aborted at file:line`, a segfault `crashed at file:line`, with inlined frames in the chain. AddressSanitizer and
UBSan stacks have lines too: smith answers their symbolizer protocol. A runner run by hand prints the raw `smith-trace` lines for a
failed check; `smith symbolize [--binary <program>] [<log>]` turns a saved log back into the report. A property test says `with seed 6387777158891554393` after FAIL, and its last line is `rerun: smith test --test <name> --seed 6387777158891554393`.

A test with a heavy check picks its size from `test::full()`, false by default and true with `--full`: `usize count = test::full() ? 20000 : 200;`. It checks the same properties either way. Use `test::check_same_bits` for determinism, where near is not enough.

A failing property test is shrunk before it is shown: the runner reruns the key, tries each random word it drew as 0 and then as the smallest word that still fails the same check (the same stack) or the same signal, within 1,000 runs or 10 s, so a failure shows `x = 500` rather than a random 64-bit draw; the header says `shrunk in 41 runs`. The rerun line names the original seed and shrinks to the same case again; `--shrink 0` shows the unshrunk failure. This works because every distribution in smith-random maps word 0 to its simplest value, so draw everything from the key with smith's distributions. Your own distribution should do the same: spend the key with `random::key_bits`, `key_unit_float` or `key_unit_double`, and make word 0 the simplest value. (The runner's keys carry a `tap` that records and replaces the words; a program's own keys carry none.)

A crash is `FAIL name: killed by SIGSEGV` with the process's stderr under it (a sanitizer's report), a busy test over the limit `FAIL name: over its CPU time limit of 10 s (it ran 10.3 s)`, a blocked one `FAIL name: timed out after 100.0 s` (the wall time it ran: a test that calls `alarm` itself replaces the backstop, and ends as a timeout after its own time). One crash fails one test, and the rest run on a fresh worker. A passing test's output is hidden.

## From C

The same tests in C include the header and use the C names, which are the generics' named instances: `smith_check(c)`, `smith_check_equal_<type>(a, b)` with `<type>` one of `bool`, `char`, `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64` and `ptr` (as `<math.h>` names `sinf` and `sin`), `smith_check_near_f32` and `_f64`, `smith_check_same_bits_f32` and `_f64`, `smith_test_expect_abort()`, `smith_test_note`, `smith_test_arena`, `smith_test_full` and `smith_test_scratch`. There are no ordering checks in C until a C caller needs one, and no macros: the old `SMITH_CHECK`, `SMITH_CHECK_NEAR` and `SMITH_CHECK_SAME_BITS` are gone (`SMITH_CHECK(a, ==, b)` is `test::check_equal(a, b)`, `<` is `check_less`, a lone condition is `test::check(c)`).

```c
#include <smith/test.h>

void test_addition(void) {
    smith_check_equal_i32(2 + 2, 4);
}

void test_sorted_stays_sorted(SmithKey key) {      // a property test: runs on 100 keys
    SmithArena arena = smith_test_arena(1 << 16);  // this test's memory, given back before each key
    size_t count = (size_t)smith_uniform_int64_sample(smith_key_derive(key, 0), (SmithUniformInt64){.low = 0, .high = 100});
    smith_test_note("count %zu", count);           // printed under a failure
}

void test_reads_a_fixture(SmithIo *io) {           // a real io, in a scratch folder
    smith_check(smith_test_scratch(io) != nullptr);
}
```

Code under test that is still C is reached from a `.smith` test with `import <smith/json.h>;` and `c::` names, or `import "v2.h";` and `c::v2_normalize(...)`, and becomes `v2::normalize` when that file is a module.

## Set it up

In build.c (the smith-build skill has the rest):

```c
SmithTests tests = {
    .sources = smith_glob(b, smith_list(b, "tests/test_*.smith", "tests/test_*.c", nullptr)), // .smith or C files
    .objects = (const SmithObject *[]){&code_under_test, nullptr}, // the code under test; nullptr for none. smith_tests links the arena, random, trace, contract and io libraries itself
    .flags = flags,       // the test files and smith's test library, -I .smith/include included
    .link_flags = link_flags,
    .inputs = smith_list(b, "build/shaders.metallib", nullptr), // files the tests read when they run: built first, a change reruns the tests
};
smith_tests(b, &tests);
```

smith::test is a `.smith` module, so `smith_tests` needs a smith with clang inside (a stage0 cannot compile it).
