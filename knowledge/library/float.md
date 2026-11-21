---
type: specification
title: Float functions
description: The functions of std.float: sqrt, exp, ln, sin, and cos, each rounded once, and floor, ceil, trunc, and round, which round a float to a whole number that is still a float.
status: partial
built:
  - "`sqrt` of an `f32` and an `f64`, on every target, as one instruction of the form that each chip has"
  - "not `exp`, `ln`, `sin`, `cos`, `floor`, `ceil`, `trunc`, or `round`"
---

# Float functions

The module `std.float` holds `sqrt`, `exp`, `ln`, `sin`, and `cos`, and the four functions that round a float.

## Roots, powers of e, logarithms, and angles

`sqrt`, `exp`, `ln`, `sin`, and `cos` each take a float of any float type and give a float of the same type. Each gives the float nearest the exact result, and the even one of the two where the result is half way between them, as `+` and `/` of floats do, so every target and the evaluator give the same bits. None asks a library of the host for it.

```
use std.float (sqrt)

fn hypotenuse(a: f64, b: f64) -> f64 { sqrt(a * a + b * b) }
```

`hypotenuse(3.0, 4.0)` is `5.0`.

| Name | Gives | Where it has no number |
| --- | --- | --- |
| `sqrt(x)` | The square root of `x` | `nan` for a number below `0.0` and for `nan`; `-0.0` for `-0.0` |
| `exp(x)` | `e` to the power `x` | `inf` for a number past the greatest float it gives, `0.0` for one below the least, `nan` for `nan` |
| `ln(x)` | The logarithm of `x` to the base `e` | `-inf` for `0.0` and `-0.0`, `nan` for a number below `0.0` and for `nan` |
| `sin(x)` | The sine of `x`, which is in radians | `nan` for `inf`, `-inf`, and `nan`; `x` itself for a zero |
| `cos(x)` | The cosine of `x`, which is in radians | `nan` for `inf`, `-inf`, and `nan` |

## Rounding to a whole number

The four functions that round a float Each takes a float of any float type and gives a float of the same type that holds a whole number. A float becomes a value of a range through `narrow` or `clamp`, as [A float entering a range](../language/arithmetic.md#a-float-entering-a-range) says, and these say which way it is rounded first.

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
