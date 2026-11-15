---
name: write-tests
description: Write tests for the Smith compiler that check what must be true and do not break when the code is restructured or a feature is added. Use whenever you add or change a test, and when turning an issue's "Must be true" list into tests.
---

# Write tests

A test exists to fail when something that must be true stops being true, and for no other reason. A test that fails when the code is rearranged, or when an unrelated feature is added, costs more than it protects. What the suite must cover is in the Testing section of `knowledge/compiler.md`. Read it first.

## What a good test is

- **It states a property.** "For every program the generator builds, the executable prints what the evaluator says" covers more than any list of examples, and it keeps covering new features as the generator grows.
- **It goes through the compiler's commands or its one entry point,** not through the functions inside a stage. Then a stage can be rewritten without touching the test.
- **Its expectation comes from the generator that built the input.** The generator knows what it made: the text a literal stands for, the token it broke and where. The test asks the compiler and compares.
- **It would fail if the line of "Must be true" it stands for were false.** Check this by breaking the code once and watching it fail.

## What to avoid

- **A line, a column, or a whole message written by hand in a test.** Take the place from the generator. Match a message by the one name it must mention, such as the missing effect.
- **A table of cases where a property would do.** Keep one or two examples to pin down the meaning, as an issue does.
- **A test of a private function, a data structure, or the order of fields.** These change when the code improves.
- **A test that needs a kind of machine to run and fails elsewhere.** Run what the machine can run and say what was skipped.
- **A sleep, a retry, or anything that depends on timing.**

## Where each kind goes

- **Properties** run in-process over generated input, with a fixed seed so a failure repeats, and shrink a failing input before reporting it.
- **Specific cases** are `.smith` files with their expected output beside them, updated by one command and reviewed as a diff. Add one when a bug is fixed, so it stays fixed.
- **Targets** share one set of tests that runs over every target. A test written for one target belongs in that set instead.

## Before you finish

- Every line of the issue's "Must be true" has a test.
- No test you added names a place or a message by hand.
- The run ends with its slowest tests. If one of yours is on the list, make its input smaller.
