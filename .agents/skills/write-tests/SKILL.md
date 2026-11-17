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

## How long a test may take

- **One second of its own computing.** A run fails, and names the test, when a test spends more. The harness measures the processor time of the thread that ran the test, so a busy machine does not make a quick test slow, and the verdict is the same alone and beside every other test.
- **A test that starts a program or a thread has not spent that time.** Call a function where you can, and start a program only to check what starting it does, and then through `output_within` in `tests/clock.rs`, which kills it after ten seconds on the wall and fails the test saying that the program never ends.
- **A program that never ends fails the test that runs it.** The evaluator panics after three million blocks, and a run ends a test that has not ended after twelve seconds on the wall, naming it. A new way to run a program goes through one of these, so that no test waits for ever.
- **Aim for under half the limit,** so that a slower machine still passes. Work that every test of a file needs is done once, in a place that `OnceLock` holds, and costs the first test that asks for it.
- **A property runs as few cases as make it fail when what it checks is broken.** Break the code once and watch it fail with the cases you chose.
- **The limit applies to every test.** A test starts no thread, as the time of a thread is not the time of its own.
- **A run fails on the kind of machine the `linux` job of `ci` uses,** and elsewhere prints a slow test and passes.

## Where each kind goes

- **Properties** run in-process over generated input, from the seed of the run, which a commit fixes and which a failure prints as `SMITH_SEED`, and shrink a failing input, for four seconds at most, before reporting it.
- **Specific cases** are `.smith` files with their expected output beside them, updated by one command and reviewed as a diff. Add one when a bug is fixed, so it stays fixed.
- **Targets** share one set of tests that runs over every target. A test written for one target belongs in that set instead.

## Before you finish

- The sweep, `sh sweep/sweep.sh`, in `bootstrap/`, shows what the tests catch. Run it again when you add a test that should catch a break the results say nothing caught.
- Every line of the issue's "Must be true" has a test.
- No test you added names a place or a message by hand.
- The run ends with its slowest tests, by the time they computed. If one of yours is on the list, make its input smaller.
