---
type: specification
title: Tests
description: Tests found by convention, a function named test_ that returns a Result, the expect check, and a test file as an ordinary module.
status: planned
---

# Tests

Tests are found by convention. A file named `x_test.smith` holds tests for `x.smith`, and every function in it named `test_...` is a test.

```
use std.test (Failed, expect)

fn test_one_and_one() -> Result[(), Failed] {
  expect { 1 + 1 == 2 }
}
```

Written in `x_test.smith`, `test_one_and_one` is a test of `x.smith`. A test that takes nothing is a single example, and it runs once.

## A test returns a Result

A test returns a `Result`, and `expect` is the one check. `expect { ... }` gives `.ok(())` when its condition holds, and a `Failed` when it does not.

```
use model.tokenizer (encode)
use std.test (Failed, expect)

fn test_empty() -> Result[(), Failed] {
  expect { len(encode("")) == 0 }
}
```

The test file for `model/tokenizer.smith` is `model/tokenizer_test.smith`, and `encode` is a function of that module.

## What `expect` reports

`expect` receives its lambda as [code](code.md), so a failed comparison reports both of its sides. A test that fails on `len(encode("")) == 0` says what `len(encode(""))` was and what `0` was.

## Several checks

Several checks chain with [`try`](try.md). A check that fails ends the test, and the last check is the test's result.

```
use model.tokenizer (decode, encode)
use std.test (Failed, expect)

fn test_tokens() -> Result[(), Failed] {
  tokens = encode("hello")
  try expect { len(tokens) <= len("hello") }
  expect { decode(tokens) == "hello" }
}
```

## A test file is an ordinary module

A test file is an ordinary module: it imports with `use`, as [Importing with use](use.md) describes, and it sees only public names. A test checks behaviour through the interface, while [contracts](contracts.md) and proofs check the internals.

```
use model.tokenizer (encode, split_words)
```

If `split_words` is private to `model.tokenizer`, this `use` is refused, as [Public and private](pub.md) describes.

Test files are left out of normal builds, so `smith build` never compiles `x_test.smith`.

## The forms

| Written | Means |
| --- | --- |
| `x_test.smith` | The tests of `x.smith` |
| `fn test_name() -> Result[(), Failed]` | A test: a single example that runs once |
| `expect { e }` | The check: `.ok(())` when `e` holds, a `Failed` reporting both sides when it does not |
| `try expect { e }` | A check that ends the test when it fails |
| `fn test_name(key: take Key)` or `fn test_name(x: T)` | A property, as [Properties](properties.md) describes |

Next: [Random values for tests](random.md)
