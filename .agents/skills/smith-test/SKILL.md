---
name: smith-test
description: "Using <smith/test.h> and smith_tests, smith's tests: checks that print both values, workers that run the tests, property tests on keys. Load when code includes <smith/test.h>, when a build.c calls smith_tests, when running smith test, or on a question about writing, running or debugging a C test."
---

# Tests: smith's test library

This skill ships in the smith plugin, from `.agents/skills/smith-test/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there. The header is `.smith/include/smith/test.h`; read it first. The build side is in the smith-build skill; random keys are in smith-random.

## Write a test

A test is a plain function in a file build.c names. There is no `TEST()` macro and no registration: the build finds the functions by name, so the definition line must be exactly this (clang-format keeps it):

```c
#include <smith/test.h>

void test_addition(void) {
    SMITH_CHECK(2 + 2, ==, 4);
}

void test_sorted_stays_sorted(SmithKey key) {      // a property test: runs on 100 keys
    SmithArena arena = smith_test_arena(1 << 16);  // this test's memory, given back before each key
    size_t count = (size_t)smith_uniform_int64_sample(smith_key_derive(key, 0), (SmithUniformInt64){.low = 0, .high = 100});
    smith_test_note("count %zu", count);           // printed under a failure
    ...
}
```

- `void test_<name>(void) {` is a test; `void test_<name>(SmithKey key) {` is a property test. `<name>` is `[a-z0-9_]`, the parameter's name `[a-z_]`. A test written another way (`void test_x (void) {`, or on two lines) is a compile error: "no previous prototype". A helper is `static`.
- A property test draws everything from its key (derive one key per use) and is run on `--seeds` keys. The seed in a failure replays it exactly.

## The checks

Write checks as plain functions. A failure shows the line that called the check and the call chain, read from the stack and
the source file, so a check needs no macro.

```c
smith_check(condition)                      // a bool
smith_check_equal_u64(a, b)                 // bool, char, i8 i16 i32 i64, u8 u16 u32 u64, f32, f64, ptr: the type is in the name
smith_check_near_f32(a, b, tolerance)       // |a - b| <= tolerance; also _f64; a NaN fails
smith_check_same_bits_f64(a, b)             // bitwise identical; also _f32
```

`SMITH_CHECK_ABORTS(statement)` stays a macro (C has no closure to hand the statement to a function). The `SMITH_CHECK*`
macros below still work and are deprecated: new tests call the functions above.

```c
SMITH_CHECK(condition)              // a bool, or anything that converts to one
SMITH_CHECK(a, op, b)               // op: == != < <= > >=; both values print with their types
SMITH_CHECK_NEAR(a, b, tolerance)   // |a - b| <= tolerance, float or double; a NaN fails
SMITH_CHECK_SAME_BITS(a, b)         // float or double, bitwise identical: -0 isn't 0, a NaN is itself; prints both in hex
SMITH_CHECK_ABORTS(expression)      // the expression stops the program on purpose: SIGABRT or SIGTRAP, no sanitizer report
```

- A comparison evaluates each argument once and converts `b` to `a`'s type, so a lossy comparison (`SMITH_CHECK(count, ==, 3.5)`) is a `-Wconversion` error. Put the value under test first.
- Pointers compare, and print, as addresses. An array decays; `const` drops.
- A failed check ends the test. A comma outside parentheses inside an argument splits it; parenthesize.
- `SMITH_CHECK_ABORTS` is strict: it passes only when the expression raises SIGABRT or SIGTRAP (an `assert`, `abort()`, `__builtin_trap`, the arena's capacity stop) and its output holds no sanitizer report ("AddressSanitizer", "UndefinedBehaviorSanitizer", "LeakSanitizer", "ThreadSanitizer", "runtime error:"). It fails, showing the expression's output, if it returns, exits, crashes with another signal (SIGSEGV) or is stopped by a sanitizer report, so a memory bug before the assertion cannot pass. The expression runs in the worker's own process (in a child with `--isolate`), so its effects stay: make a variable it changes that you read afterwards `volatile`. A hang is the test's timeout.

## Run them

`smith test`. Its arguments are the runner's own options, so `smith test --test <substring>` runs one test (or every name that contains it), `--seed <n>` a property test on that one seed, `--seeds <n>` sets the keys per property test (100), and there are `--jobs <n>`, `--timeout <seconds>` (10, per test and per key), `--shrink 0|1`, `--shuffle <seed>`, `--isolate`, `--stats` (prints how many forks the run made), `--full` (the heavy checks at full size), `--list` and `--verbose`. `-D` options are build.c's own (`smith test -DFAST`). The runner is `build/tests`, so under a debugger it is `lldb -- build/tests --test name --jobs 1 --isolate`.

### Workers

The runner forks `--jobs` workers once and each runs the tests it is sent, one after another, in its own process. Each test starts with its arenas given back and an empty note; everything else it leaves behind (globals, open files, the heap) stays for the next test on that worker, and which tests share a worker depends on timing unless `--jobs 1`. So a test must not depend on another. `--shuffle <seed>` runs the tests in a shuffled order to find one that does; with `--jobs 1` the same seed replays it. A worker is reused only after a clean pass with no `SMITH_CHECK_ABORTS`. A failed test ends its worker (a failed check unwinds out of arbitrary code and can leave state half-done), a test that ran a death check ends it too, passed or failed (a death check stands for the program ending, so shared state it left, such as a committed GPU command buffer, must not reach the next test), and so do a crash, a timeout, an exit and a sanitizer's report; the runner forks the next. Shrinking reuses one worker across its failing attempts, so the runner reruns the shrunk case once in a fresh worker; if it does not fail the same way you get the unshrunk failure and a line saying state leaked between attempts. A test that leaves global state dirty therefore only matters to the passing tests that follow it. `--isolate` runs each test in a process of its own, as before the workers. This matters most with upstream LLVM's AddressSanitizer, which costs about 40 ms of kernel time for each fork or `posix_spawn` on macOS.

A failure in `smith build test` looks like this; the last line reproduces it. The runner only records the addresses of the stack
(`<smith/trace.h>`); smith turns them into files, lines and source text with LLVM's symbolizer, so the program holds no
symbolizer and the test binary links as quickly as before:

```
FAIL moves_every_step
  check failed at tests/moves_test.c:42
       42 |     smith_check_equal_u64(moves.count, expected);
  called from
    tests/moves_test.c:42  moves_every_step
    .smith/lib/test.c:931  smith_test_execute
    a = 3   (u64)
    b = 4   (u64)
    rerun: ...
```

An `assert` is `aborted at file:line`, a segfault `crashed at file:line`, with inlined frames in the chain. AddressSanitizer and
UBSan stacks have lines too: smith answers their symbolizer protocol. A runner run by hand prints the raw `smith-trace` lines for a
plain-function check; `smith symbolize [--binary <program>] [<log>]` turns a saved log back into the report. The older macro form of the
same failure:

```
FAIL uniform_stays_in_range with seed 6387777158891554393
  tests/test_random.c:12
      SMITH_CHECK(x, <=, range.high);
    x          = 7   (long long)
    range.high = 6   (long long)
    note: count 3
    rerun: smith test --test uniform_stays_in_range --seed 6387777158891554393
```

A test with a heavy check picks its size from `smith_test_full()`, false by default and true with `--full`: `size_t count = smith_test_full() ? 20000 : 200;`. It checks the same properties either way. Use `SMITH_CHECK_SAME_BITS` for determinism, where near is not enough.

A failing property test is shrunk before it is shown: the runner reruns the key, tries each random word it drew as 0 and then as the smallest word that still fails the same check (file and line) or the same signal, within 1,000 runs or 10 s, so a failure shows `x = 500` rather than a random 64-bit draw; the header says `shrunk in 41 runs`. The rerun line names the original seed and shrinks to the same case again; `--shrink 0` shows the unshrunk failure. This works because every distribution in smith-random maps word 0 to its simplest value, so draw everything from the key with smith's distributions. Your own distribution should do the same: spend the key with `smith_key_bits`, `smith_key_unit_float` or `smith_key_unit_double`, and make word 0 the simplest value.

A crash is `FAIL name: killed by SIGSEGV` with the process's stderr under it (a sanitizer's report), a timeout `FAIL name: timed out after 10.0 s` (the time the test ran: a test that calls `alarm` itself replaces the runner's timer, and ends as a timeout after its own time). One crash fails one test, and the rest run on a fresh worker. A passing test's output is hidden.

## Set it up

In build.c (the smith-build skill has the rest):

```c
SmithTests tests = {
    .sources = smith_glob(b, smith_list(b, "tests/test_*.c", nullptr)),
    .objects = (const SmithObject *[]){&arena, &random, &code_under_test, nullptr}, // smith's arena and random are required
    .flags = flags,       // the test files and smith's test library, -I .smith/include included
    .link_flags = link_flags,
    .inputs = smith_list(b, "build/shaders.metallib", nullptr), // files the tests read when they run: built first, a change reruns the tests
};
smith_tests(b, &tests);
```
