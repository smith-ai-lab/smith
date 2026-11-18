---
type: specification
title: Running tests
description: smith test, which runs every test or the ones chosen, in parallel, and what a failure and a run print.
status: planned
---

# Running tests

`smith test` runs every test of the project.

```
smith test
```

All tests run in one process, each as a task with its own arena, in parallel. A run ends with its slowest tests and how long each took.

## Choosing tests

`smith test` takes the tests to run.

```
smith test                           everything
smith test model/gpt_test.smith      one file
smith test --name round_trip         tests whose name matches
smith test --seed 4127               replay a failing run
```

Running one test compiles only what that test reaches, so it stays fast in a large project.

## When a test fails

A failure prints the smallest failing input and the seed. `smith test --seed` with that seed replays the run.

```
smith test --seed 4127
```

## What a test may do

A test declares its effects like any function.

```
fn test_config() -> Result[(), Failed] ! fs, alloc
```

A test that touches files gets an in-memory file system unless it says otherwise, so `test_config` reads no file of the machine, as [handlers](handlers.md) describe for `memory_fs`.

## The forms

| Written | Means |
| --- | --- |
| `smith test` | Runs every test |
| `smith test <file>` | Runs the tests of that file |
| `smith test --name <text>` | Runs the tests whose name matches |
| `smith test --seed <n>` | Replays a run with that seed |
| `fn test_x() ! e` | A test with the effects `e` |

Next: [A transformer](transformer.md)
