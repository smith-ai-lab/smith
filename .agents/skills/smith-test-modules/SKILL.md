---
name: smith-test-modules
description: "Tests in modules: a test file is `module x_test;` with exported test_ functions, found and linked by smith_tests. Load when naming a test file or on 'no tests found'."
---

# Tests in modules

Name the `.smith` files in `.sources` (`smith_glob(b, smith_list(b, "**/*_test.smith", nullptr))`). The same lines are tests:

```smith
// tests/geometry/box_test.smith
module geometry::box_test;

import smith::random;
import smith::test;

export void test_area() { // `export`: a private function is static, nothing can call it
    test::check_equal(3 * 2, 6);
}

export void test_area_property(random::Key key) {
    test::check(random::key_unit_double(key) < 1.0);
}
```

- Test files are compiled with `-Werror=missing-prototypes`, on purpose: a test the finder does not recognise (a misspelt signature) has no prototype in the generated list and fails the build instead of silently not running. The cost is that a helper function in a test file needs `static` when the file has no `module` line (in a module every non-`export` function is private already): "no previous prototype for function 'helper'" means make it `static`.
- In a file with no `module` line, `void test_x(void) {` is a test with no `export`, named `x`. In a module the test is shown, listed and selected by its name as written, `geometry::box_test::area` (`smith test --test geometry::box_test` runs the module's tests; `--test geometry::box_test::test_area` works too, `test_` as in the source). Two files cannot define the same test (a module's tests are `a::b::test_x`, so only the same module and name collide).
- A failure names the `.smith` line and its source, and the function as written (`tests/geometry/box_test.smith:12  geometry::box_test::test_area_fails`). Frames of the test library (`smith_test_*`, `smith::test::*`) and of check functions are left out of the report.

Also in this family: [smith-test](../smith-test/SKILL.md), [smith-test-checks](../smith-test-checks/SKILL.md), [smith-test-workers](../smith-test-workers/SKILL.md), [smith-test-c](../smith-test-c/SKILL.md), [smith-build-tests](../smith-build-tests/SKILL.md).
