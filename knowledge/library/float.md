---
type: specification
title: Float functions
description: The functions of std.float that round a float to a whole number that is still a float: floor, ceil, trunc, and round.
status: planned
---

# Float functions

The module `std.float` holds the four functions that round a float. Each takes a float of any float type and gives a float of the same type that holds a whole number. A float becomes a value of a range through `narrow` or `clamp`, as [A float entering a range](../language/arithmetic.md#a-float-entering-a-range) says, and these say which way it is rounded first.

```
use std.float (floor)

fn whole_below(x: f64) -> f64 { floor(x) }
```

`whole_below(-2.5)` is `-3.0`. Of `-2.5`, `ceil` gives `-2.0`, `trunc` gives `-2.0`, and `round` gives `-2.0`, the even one of `-3.0` and `-2.0`.

## What it holds

| Name | Gives |
| --- | --- |
| `floor` | The greatest whole number that is not above the float |
| `ceil` | The least whole number that is not below the float |
| `trunc` | The float with its fraction left off, which is the whole number next to it towards zero |
| `round` | The nearest whole number, and the even one of the two when the float is exactly half way between them |

An infinity and a float that is not a number are given back as they are. `round` goes to the even number at a half, as `clamp` of a float does, so `clamp(round(x))` is `clamp(x)`.
