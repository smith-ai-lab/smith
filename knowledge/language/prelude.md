---
type: specification
title: The prelude
description: The names that are in scope in every file without a use, and what a file imports for everything else.
status: planned
---

# The prelude

A small set of names is in scope in every file with no `use`. It is called the prelude.

```
fn first_or_zero(xs: [3]i32) -> Option[i32] {
  .some(sum(xs))
}

fn main() ! console {
  print("hello")
}
```

`Option`, `sum`, `i32`, `print`, and the effect `console` are all in the prelude, so this file has no `use` line.

## What is in it

- the types `Option`, `Result`, and `Order`;
- the traits `Try`, `Fmt`, `Repr`, and `Random`, and the operator traits with `Eq` and `Ord`;
- the numeric types and `bool`;
- the effects `console`, `fs`, `net`, `time`, and `ui`;
- `print`;
- the array functions, such as `sum` and `map`.

## Everything else is imported

A name that is not in the prelude is imported with a `use`, as [Importing with use](use.md) describes. `Failed` and `expect` are not in the prelude, so a test file imports them.

```
use std.test (Failed, expect)

fn test_sum() -> Result[(), Failed] {
  expect { 1 + 1 == 2 }
}
```

`Result` is in the prelude and `Failed` is not.

## The forms

| A name | Needs a `use`? |
| --- | --- |
| `Option`, `Result`, `Order` | No |
| `Try`, `Fmt`, `Repr`, `Random`, `Eq`, `Ord`, the other operator traits | No |
| `i32`, `f32`, `bool`, and the other numeric types | No |
| `console`, `fs`, `net`, `time`, `ui` | No |
| `print`, `sum`, `map`, and the other array functions | No |
| Any other name, `std` or not | Yes |

Next: [Importing a module](qualified-names.md)
