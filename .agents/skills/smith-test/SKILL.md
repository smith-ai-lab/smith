---
name: smith-test
description: "Writing tests with smith::test (import smith::test; test::check_equal): a test is an exported function whose signature says what it touches, pure, a property test on a random::Key, or one with a real io. Load when code imports smith::test, when writing or reading a test_ function."
---

# Tests: smith::test

## Write a test

A test is a plain function in a file build.c names. There is no `TEST()` macro and no registration: the build finds the functions by name, so the definition line must be exactly one of these (clang-format keeps it). **The signature says what the test may touch:**
- `void test_x(void)` is pure: it touches nothing outside its own memory.
- `void test_x(random::Key key)` is a property test: it runs on 100 keys drawn from a fixed root, and a failure prints the seed that replays it (`SmithKey` in C).
- `void test_x(io::Io *io)` takes a real io (`SmithIo *io` in C): the runner makes it for that one test, with its key made from the run's seed (so `--seed` replays it: `io->seed` and `io->key`), a fresh arena, and a scratch folder of its own, `test::scratch(io)`, which is also `$TMPDIR` while the test runs. The folder is removed when the test passes and kept, with its path printed, when it fails. The working directory stays the project root, so fixtures resolve. A test makes fakes (`io::make_test`) anywhere, but never a real io: `smith-effects` allows `io::make` only in `main`, so the runner can always seed, sandbox and clean up after a test, and parallel tests never share the real world.

A test file is a module named for what it tests, `tests/primes_test.smith` is `module primes_test;` (`tests/` is a root of its own), and each test is `export`ed: a private function of a module is static, and the runner calls a test from outside. The runner lists `primes_test::the_first_five_primes_add_up_to_28`. `smith new` writes it so, and `smith lint` (`smith-module`) says to add the module line to a file that has none (such a file still builds, and its tests need no `export`). Write the type argument of a check, as `smith new`'s test does (`test::check_equal(primes::sum(5), 28)`); it is inferred when left out.

```smith
module math_test;

import smith::arena;
import smith::io;
import smith::random;
import smith::test;

export void test_addition(void) {
    test::check_equal(2 + 2, 4);
    test::check_near(0.1 + 0.2, 0.3, 1e-9);
}

export void test_sorted_stays_sorted(random::Key key) { // a property test: runs on 100 keys
    arena::Arena a = test::memory(1 << 16); // this test's memory, given back before each key
    usize count = (usize)random::uniform_int64_sample(random::key_derive(key, 0), (random::UniformInt64){.low = 0, .high = 100});
    test::note("count %zu", count); // printed under a failure
    usize items[:] = arena::push_slice(&a, usize, count);
    for (usize i = 0; i < count; i++) {
        items[i] = (usize)random::uniform_int64_sample(random::key_derive(key, i + 1), (random::UniformInt64){.low = 0, .high = 9});
    }
}

export void test_writes_a_file(io::Io *io) { // a real io, in a scratch folder that is removed when the test passes
    test::check(test::scratch(io) != nullptr);
    io::Error wrote = io::files_write(&io->files, "scratch.txt", ((const u8 *)"hi")[0:2], io::Replace);
    test::check_equal(wrote.code, 0);
}
```

- `<name>` is `[a-z0-9_]`, the parameter's name `[a-z_]`. The build finds the property test by the literal text `SmithKey`, `c::SmithKey` or `random::Key` (`<alias>::Key` for `import smith::random as <alias>;`), and the io test by `SmithIo *`, `c::SmithIo *` or `io::Io *` (`<alias>::Io` for `import smith::io as <alias>;`). A test written another way (`void test_x (void) {`, or on two lines) is a compile error: "no previous prototype". A helper is `static`. A test file imports the C headers it calls (`import <string.h>;` for `c::strcmp`): the header of `smith::test` carries none, so a `c::strcmp` that only worked because that header included `<string.h>` fails with `call to undeclared library function 'strcmp'` until the file imports it.
- A property test draws everything from its key (derive one key per use) and is run on `--seeds` keys. The seed in a failure replays it exactly.
- `test::memory(capacity)` is the test's memory (an `arena::Arena`), given back before each test and each key. `test::note(format, ...)` prints under a failure; each note replaces the last. `test::full()` is true when the run was given `--full`.

Also in this family: [smith-test-modules](../smith-test-modules/SKILL.md), [smith-test-checks](../smith-test-checks/SKILL.md), [smith-test-workers](../smith-test-workers/SKILL.md), [smith-test-c](../smith-test-c/SKILL.md), [smith-build-tests](../smith-build-tests/SKILL.md).
