---
type: specification
title: Tests
description: Tests found by convention, examples and properties driven by a Key, and how they run.
status: planned
---

# Tests

Tests are found by convention. A file named `x_test.smith` holds tests for `x.smith`, and every function in it named `test_...` is a test. A test returns a `Result`, and several checks chain with `try`. `expect` is the one check. It receives its lambda as [code](code.md), so a failed comparison reports both of its sides. A test file is an ordinary module: it imports with `use` and sees only public names, so tests check behaviour through the interface, while contracts and proofs check the internals. Test files are left out of normal builds.

```
use model.tokenizer (decode, encode)
use std.test (Failed, expect)

fn test_empty() -> Result[(), Failed] {
  expect { len(encode("")) == 0 }
}

fn test_round_trip(key: take Key) -> Result[(), Failed] {
  text = random_text(key, max: 200)
  expect { decode(encode(text)) == text }
}

fn test_tokens(text: str) -> Result[(), Failed] {
  tokens = encode(text)
  try expect { len(tokens) <= len(text) }
  expect { decode(tokens) == text }
}
```

- A test that takes nothing is a single example and runs once.
- A test that takes a `Key` is a property. It runs many times, building its inputs with ordinary random functions. Under `smith test` the key is backed by a recorded sequence of choices, so a failure is shrunk by replaying the test with smaller choices, and every shrunk input is one the test's own code built.
- Typed parameters are shorthand: each is drawn from a split key with its type's default random function.
- The checker tries to prove a property first and runs it only when it cannot.

```
smith test                           everything
smith test model/gpt_test.smith      one file
smith test --name round_trip         tests whose name matches
smith test --seed 4127               replay a failing run
```

- All tests run in one process, each as a task with its own arena, in parallel.
- Running one test compiles only what that test reaches, so it stays fast in a large project.
- A failure prints the smallest failing input and the seed.
- A run ends with its slowest tests and how long each took.
- A test declares its effects like any function. One that touches files gets an in-memory file system unless it says otherwise.

Next: [A transformer](transformer.md)
