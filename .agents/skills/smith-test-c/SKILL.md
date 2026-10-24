---
name: smith-test-c
description: "Writing tests from C with <smith/test.h>: the same checks and signatures under C names. Load when a C file includes <smith/test.h>."
---

# smith::test from C

The same tests in C include the header and use the C names, which are the generics' named instances: `smith_check(c)`, `smith_check_equal_<type>(a, b)` with `<type>` one of `bool`, `char`, `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64` and `ptr` (as `<math.h>` names `sinf` and `sin`), `smith_check_near_f32` and `_f64`, `smith_check_same_bits_f32` and `_f64`, `smith_test_expect_abort()`, `smith_test_note`, `smith_test_arena`, `smith_test_full` and `smith_test_scratch`. There are no ordering checks in C until a C caller needs one, and no macros: the old `SMITH_CHECK`, `SMITH_CHECK_NEAR` and `SMITH_CHECK_SAME_BITS` are gone (`SMITH_CHECK(a, ==, b)` is `test::check_equal(a, b)`, `<` is `check_less`, a lone condition is `test::check(c)`).

```c
#include <smith/test.h>

void test_addition(void) {
    smith_check_equal_i32(2 + 2, 4);
}

void test_sorted_stays_sorted(SmithKey key) {      // a property test: runs on 100 keys
    SmithArena arena = smith_test_arena(1 << 16);  // this test's memory, given back before each key
    SmithKey parts[2];
    smith_key_split_n(key, parts, 2);              // a key is used once: split it for each use
    size_t count = (size_t)smith_uniform_int64_sample(parts[0], (SmithUniformInt64){.low = 0, .high = 100});
    smith_test_note("count %zu", count);           // printed under a failure
}

void test_reads_a_fixture(SmithIo *io) {           // a real io, in a scratch folder
    smith_check(smith_test_scratch(io) != nullptr);
}
```

Code under test that is still C is reached from a `.smith` test with `import <smith/json.h>;` and `c::` names, or `import "v2.h";` and `c::v2_normalize(...)`, and becomes `v2::normalize` when that file is a module.

Also in this family: [smith-test](../smith-test/SKILL.md), [smith-test-modules](../smith-test-modules/SKILL.md), [smith-test-checks](../smith-test-checks/SKILL.md), [smith-test-workers](../smith-test-workers/SKILL.md), [smith-build-tests](../smith-build-tests/SKILL.md).
