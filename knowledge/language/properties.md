---
type: specification
title: Properties
description: A test that takes a Key, or typed parameters, runs many times on random inputs, is shrunk to the smallest failure, and is proved first when the checker can.
status: planned
---

# Properties

A test that takes a `Key` is a property. It runs many times, building its inputs with ordinary random functions.

```
use model.tokenizer (decode, encode)
use std.test (Failed, expect)

fn test_round_trip(key: take Key) -> Result[(), Failed] {
  text = random_text(key, max: 200)
  expect { decode(encode(text)) == text }
}
```

`test_round_trip` runs with a new key each time, so `text` is a different text each time.

## When it fails

Under `smith test` the key is backed by a recorded sequence of choices. A failure is shrunk by replaying the test with smaller choices, and every shrunk input is one the test's own code built.

If `decode(encode(text)) == text` fails for a text of 140 characters, the test is replayed with smaller choices until the text is as small as it can be while it still fails. Each text it tries was built by `random_text`, so each is a text the test could have been given.

## Typed parameters

Typed parameters are shorthand. Each is drawn from a split key with `random`, the one function of the [`Random`](random.md) trait.

```
fn test_order(a: i32, b: i32) -> Result[(), Failed] {
  expect { (a < b) == (b > a) }
}
```

This is the same as writing the keys out.

```
fn test_order(key: take Key) -> Result[(), Failed] {
  (ka, kb) = split(key)
  a: i32 = random(ka)
  b: i32 = random(kb)
  expect { (a < b) == (b > a) }
}
```

A type that a program defines can be a parameter with nothing more written, since it has `Random`. A parameter whose type has no `Random` is refused.

```
fn test_point(p: Point) -> Result[(), Failed] {
  expect { p == p }
}
```

This is a test when `Point` has `Random`, and is refused after `without Random for Point`.

Typed parameters are written when each input is a random value of its type. A `Key` is written when an input must be built by a function that takes more than the key, as `random_text(key, max: 200)` does.

## Proved before it is run

The checker tries to prove a property first and runs it only when it cannot.

```
fn test_not_not(b: bool) -> Result[(), Failed] {
  expect { not not b == b }
}
```

The checker proves this for every `b`, so the property is not run.

## The forms

| Written | Means |
| --- | --- |
| `fn test_x(key: take Key)` | A property: runs many times, with inputs built from `key` |
| `fn test_x(a: A, b: B)` | The same, with each parameter drawn from a split key with `random` |
| A failure | Shrunk by replaying with smaller choices |
| A property the checker proves | Not run |

Next: [Running tests](running-tests.md)
