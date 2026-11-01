---
name: smith-test
description: "Writing tests with std::test (import std::test; test::check_equal): a test is an exported function whose signature says what it touches, pure or a property test on a random::Key; a test takes no io. Load when code imports std::test, when writing or reading a test_ function."
---

# Tests: std::test

## Write a test

A test is a plain function in a file build.c names. There is no `TEST()` macro and no registration: the build finds the functions by name, so the definition line must be exactly one of these (clang-format keeps it). **The signature says what the test may touch:**
- `void test_x(void)` is pure: it touches nothing outside its own memory.
- `void test_x(random::Key key)` is a property test: it runs on 100 keys drawn from a fixed root, and a failure prints the seed that replays it (`SmithKey` in C).
- `void test_x(random::Key key, failing::Failing *failing)` is a property test whose allocator refuses some allocations by the key's draws (`import std::failing;`, `struct SmithFailing *` in C): the runner installs `failing` as the allocator for the call, so code under test meets every refusal path and `--seed` replays it ([smith-failing](../smith-failing/SKILL.md)).
- A test takes no io: a signature with parameters other than these is a build error naming the io-free forms. A test reaches the world through the direct libraries (`std::files`, `std::process`, `std::console`, ...) and replaces them with their fakes ([smith-files](../smith-files/SKILL.md)); the working directory is the project root, so fixtures resolve, and a scratch folder is `scratch::make` in a test of smith itself.

A unit test sits beside its module and is named for it (`primes_test.smith` beside `primes.smith` is `module primes_test;`, `geometry/box_test.smith` is `module geometry::box_test;`), is an ordinary module that sees only what the module exports (test a private function through the exports that use it), and `smith test` finds any `*_test.smith` or `*_test.c` (the imports may be `std::io`, `std::test`; a test file in which it finds no test is an error naming the file, never a silent skip); tests that start the built program or work on whole projects go in `tests/`. Each test is `export`ed: a private function of a module is static, and the runner calls a test from outside. The runner lists `primes_test::the_first_five_primes_add_up_to_28`. `smith new` writes it so, and `smith lint` (`smith-module`) says to add the module line to a file that has none (such a file still builds, and its tests need no `export`). Write the type argument of a check, as `smith new`'s test does (`test::check_equal(primes::sum(5), 28)`); it is inferred when left out.

```smith
module math_test;

import std::arena;
import std::random;
import std::test;

export void test_addition() {
    test::check_equal(2 + 2, 4);
    test::check_near(0.1 + 0.2, 0.3, 1e-9);
}

export void test_sorted_stays_sorted(random::Key key) { // a property test: runs on 100 keys
    arena::Arena a = test::memory(1 << 16); // this test's memory, given back before each key
    random::Key parts[2];
    random::split(key, parts); // a key is used once: split it for each use
    Usize count = (Usize)random::uniform_int64_sample(parts[0], (random::UniformInt64){.low = 0, .high = 100});
    test::note("count %zu", count); // printed under a failure
    Usize items[:] = arena::push_slice(&a, Usize, count);
    random::Key item_keys[:] = arena::push_slice(&a, random::Key, count); // one key per item
    random::split(parts[1], item_keys);
    for (Usize i = 0; i < countof(item_keys); i++) {
        items[i] = (Usize)random::uniform_int64_sample(item_keys[i], (random::UniformInt64){.low = 0, .high = 9});
    }
}
```

- `<name>` is `[a-z0-9_]`, the parameter's name `[a-z_]`. The build finds the property test by the literal text `SmithKey`, `c::SmithKey` or `random::Key` (`<alias>::Key` for `import std::random as <alias>;`). A test written another way (`void test_x (void) {`, or on two lines) is a compile error: "no previous prototype". A helper is `static`. A test file imports the C headers it calls (`import <string.h>;` for `c::strcmp`): the header of `std::test` carries none, so a `c::strcmp` that only worked because that header included `<string.h>` fails with `call to undeclared library function 'strcmp'` until the file imports it.
- A property test draws everything from its key (split it: a key is used once) and is run on `--seeds` keys. The seed in a failure replays it exactly.
- `test::memory(capacity)` is the test's memory (an `arena::Arena`), given back before each test and each key. `test::note(format, ...)` prints under a failure; each note replaces the last. `test::full()` is true when the run was given `--full`.
- The memory is capped at 256 MB (`memory_capacity` in `lib/std/test.smith`): every `test::memory` call of one test, or of one key, shares it, and a call past it stops the test. A test over many files makes one arena and resets it per file (`arena::reset`, or `arena::scope_begin` and `scope_end` per file), not a `test::memory` per file.

- Every test runs under `mem::checked` ([smith-failing](../smith-failing/SKILL.md)): a test that returns with an allocated block not freed fails with `memory leaked` at the line that allocated it, and a double free or a use after free is reported where it happens. The check covers every test, and a `malloc` in a C library the test calls goes through it too (the leak names the C line); the C library's own blocks (stdio buffers, locale) are not leaks, so a test may call `printf`. `--unchecked` turns it off (build.c: `.checked_allocator = build::Off` on `build::AppTests`).
- `test::golden(path, actual)` compares a text with a file in the project (`smith test --update` writes it): [smith-test-checks](../smith-test-checks/SKILL.md).

Also in this family: [smith-test-modules](../smith-test-modules/SKILL.md), [smith-test-checks](../smith-test-checks/SKILL.md), [smith-test-workers](../smith-test-workers/SKILL.md), [smith-test-c](../smith-test-c/SKILL.md), [smith-build-tests](../smith-build-tests/SKILL.md).
