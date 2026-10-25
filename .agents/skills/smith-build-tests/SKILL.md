---
name: smith-build-tests
description: "smith_tests in build.c: declaring a project's tests, the runner it builds, its options and which libraries it links. Load when a build.c calls smith_tests or smith test says it defines no tests."
---

# Set it up

In build.c (smith-build-c has the rest):

```c
SmithTests tests = {
    .sources = smith_glob(b, smith_list(b, "**/*_test.smith", "**/*_test.c", nullptr)), // .smith or C files
    .objects = (const SmithObject *[]){&code_under_test, nullptr}, // the code under test; nullptr for none. smith_tests links the arena, random, trace, contract and io libraries itself
    .flags = flags,       // the test files and smith's test library, -I .smith/include included
    .link_flags = link_flags,
    .inputs = smith_list(b, "build/shaders.metallib", nullptr), // files the tests read when they run: built first, a change reruns the tests
};
smith_tests(b, &tests);
```

smith::test is a `.smith` module, so `smith_tests` needs a smith with clang inside (a stage0 cannot compile it).

Also in this family: [smith-test](../smith-test/SKILL.md), [smith-test-modules](../smith-test-modules/SKILL.md), [smith-test-checks](../smith-test-checks/SKILL.md), [smith-test-workers](../smith-test-workers/SKILL.md), [smith-test-c](../smith-test-c/SKILL.md).
