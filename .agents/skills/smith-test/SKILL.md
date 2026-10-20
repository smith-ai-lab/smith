---
name: smith-test
description: "Using <smith/test.h> and smith_tests, smith's tests: checks that print both values, a process per test, property tests on keys. Load when code includes <smith/test.h>, when a build.c calls smith_tests, when running smith build test, or on a question about writing, running or debugging a C test."
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
- A property test draws everything from its key (derive one key per use) and is run on `-DSEEDS` keys. The seed in a failure replays it exactly.

## The checks

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
- `SMITH_CHECK_ABORTS` forks and is strict: it passes only when the child dies by SIGABRT or SIGTRAP (an `assert`, `abort()`, `__builtin_trap`, the arena's capacity stop) and its output holds no sanitizer report ("AddressSanitizer", "UndefinedBehaviorSanitizer", "LeakSanitizer", "ThreadSanitizer", "runtime error:"). It fails, showing the child's output, if the child returns, exits non-zero, crashes with another signal (SIGSEGV) or is stopped by a sanitizer report, so a memory bug before the assertion cannot pass. A timeout fails too.

## Run them

`smith build test`, with `-DTEST=<substring>` (one test, or every name that contains it), `-DSEED=<n>` (a property test on that one seed), `-DSEEDS=<n>` (keys per property test, 100), `-DJOBS=<n>`, `-DSHRINK=0|1` and `-DFULL` (the heavy checks at full size). The runner `build/tests` takes `--test`, `--seed`, `--seeds`, `--jobs`, `--timeout <seconds>` (10, per test and per key), `--shrink 0|1`, `--full`, `--list` and `--verbose`, so under a debugger it is `lldb -- build/tests --test name --jobs 1`.

A failure looks like this; the last line reproduces it:

```
FAIL uniform_stays_in_range with seed 6387777158891554393
  tests/test_random.c:12
      SMITH_CHECK(x, <=, range.high);
    x          = 7   (long long)
    range.high = 6   (long long)
    note: count 3
    rerun: smith build test -DTEST=uniform_stays_in_range -DSEED=6387777158891554393
```

A test with a heavy check picks its size from `smith_test_full()`, false by default and true with `-DFULL`: `size_t count = smith_test_full() ? 20000 : 200;`. It checks the same properties either way. Use `SMITH_CHECK_SAME_BITS` for determinism, where near is not enough.

A failing property test is shrunk before it is shown: the runner reruns the key, tries each random word it drew as 0 and then as the smallest word that still fails the same check (file and line) or the same signal, within 1,000 runs or 10 s, so a failure shows `x = 500` rather than a random 64-bit draw; the header says `shrunk in 41 runs`. The rerun line names the original seed and shrinks to the same case again; `-DSHRINK=0` shows the unshrunk failure. This works because every distribution in smith-random maps word 0 to its simplest value, so draw everything from the key with smith's distributions. Your own distribution should do the same: spend the key with `smith_key_bits`, `smith_key_unit_float` or `smith_key_unit_double`, and make word 0 the simplest value.

A crash is `FAIL name: killed by SIGSEGV` with the process's stderr under it (a sanitizer's report), a timeout `FAIL name: timed out after 10.0 s` (the time the test ran: a test that calls `alarm` itself replaces the runner's timer, and ends as a timeout after its own time). Each test runs in a process of its own, so one crash fails one test and the rest run. A passing test's output is hidden.

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
