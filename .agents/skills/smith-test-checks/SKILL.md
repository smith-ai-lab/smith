---
name: smith-test-checks
description: "The checks of smith::test: check_equal, check_less, check_near and the rest, what a failure prints, and expect_abort. Load when choosing a check or reading a failed check's report."
---

# The checks

The checks are generic functions of `smith::test`: the type is the first argument's (`auto a`, smith-lang-generics), so there is nothing to spell. A failure shows the line that called the check and the call chain, read from the stack and the source file, so a check needs no macro.

```smith fragment
test::check(condition)                      // a bool
test::check_equal(a, b)                     // a == b; bool, char, integers, f32, f64 and pointers; both values print with their type
test::check_not_equal(a, b)                 // a != b (a NaN is not equal to anything, so this passes for one)
test::check_less(a, b)                      // also check_less_equal, check_greater, check_greater_equal; a NaN is unordered, so these fail on one
test::check_near(a, b, tolerance)           // |a - b| <= tolerance, f32 or f64; a NaN fails; equal infinities pass
test::check_same_bits(a, b)                 // bitwise identical, f32 or f64: -0 isn't 0, a NaN is itself; prints both in hex
test::expect_abort()                        // from this call on the test must abort (below)
```

- **`b` converts to the type of `a`** (`check_equal(auto a, typeof(a) b)`): `check_equal(count, 3)` is the type of `count`, as C converts. A conversion that changes the value is an error at the call that names the cast: `check_equal(my_u8, 300)`, `check_near(my_f32, 0.1)` (write `0.1f`), a `u32` variable against a `u8` first argument, an `i32` against a `u32`. Cast the argument (`(u8)big`) or the first one. A literal first argument is an `i32` (`check_equal(0, count)` with a `usize` count needs `(usize)0` or the order `check_equal(count, 0)`). A call to a generic is typed by its result; one the compiler cannot type (`__builtin_nan("")`) goes in a variable first.
- Types print as Smith spells them: `i8` to `i64`, `u8` to `u64`, `f32`, `f64`, `bool`, `char`, `ptr`.
- A failed check ends the test.
- **`check(a == b)` shows `a` and `b`.** When a `test::check`, a contract or an `assert` fails, `smith test` attaches smith's own lldb to the stopped test, reads every variable the failing line names (`moves.count`, `expected`, `b->cells[row][col]`, an enum, a pointer and what it points to) and prints them under the line; nothing is captured in the program, and no function is called. `check_equal` and the other comparisons already print their two values, so they are not read. Details in [smith-test-workers](../smith-test-workers/SKILL.md).
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

Also in this family: [smith-test](../smith-test/SKILL.md), [smith-test-modules](../smith-test-modules/SKILL.md), [smith-test-workers](../smith-test-workers/SKILL.md), [smith-test-c](../smith-test-c/SKILL.md), [smith-build-tests](../smith-build-tests/SKILL.md).
