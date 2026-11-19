---
type: specification
title: return
description: return leaves a function early with a value, from the nearest function or lambda around it, and nothing that was running around it is cut short.
status: partial
built:
  - "`return e` and a bare `return` in a function and a lambda, from an `if`, a `match`, a `loop` over a range or open-ended, and the rest of a block after `try` or `with`; it gives the nearest function or lambda's value, costs a jump, and cuts nothing short"
  - "a value of another type than the result is refused as the last expression is, a bare `return` where the result is not nothing and a value where it is nothing are refused, and the first line after a `return` in its block is refused with \"this line never runs, as the `return` before it leaves the function or lambda\""
  - "`arith` knows after `if c { return e }` that `c` is false, as in an `else`, and a value without `Copy` is given away by a `return` and a lent value is not returned"
  - "a `return` in a `loop` over a type that implements `Iterate`, which keeps its value and answers `.stop`, and gives the function the value once `each` is over"
  - "not the formatter taking away a `return` on the last line, or the check that a value that must be used is used on the way a `return` leaves by"
---

# `return`

`return e` makes the function give `e` at once, and the lines after it do not run. It is for leaving early, so that the lines that follow a check stay at the left.

```
fn classify(b: u8) -> str {
  if b >= '0' and b <= '9' { return "digit" }
  if b == ' ' { return "space" }
  "other"
}
```

`classify('7')` is `"digit"`, `classify(' ')` is `"space"`, and `classify('x')` is `"other"`. The function means what this one means, with each check one step further in:

```
fn classify(b: u8) -> str {
  if b >= '0' and b <= '9' { "digit" } else if b == ' ' { "space" } else { "other" }
}
```

## The last line needs none

The last expression of a function is still what it gives, as [Functions](functions.md) says, and `return` is not written there. The formatter rewrites `fn double(x: i64) -> i64 { return x * 2 }` as `fn double(x: i64) -> i64 { x * 2 }`.

## With nothing to give

In a function that gives nothing, `return` is written alone.

```
fn log_if(verbose: bool, text: str) ! console {
  if not verbose { return }
  print(text)
}
```

`log_if(false, "x")` prints nothing.

## What it gives must fit

`return e` gives the function's type. A value of another type is refused, as the last expression is, and so is a `return` with no value in a function that gives one.

```
fn classify(b: u8) -> str {
  if b == ' ' { return 1 }
  "other"
}
```

This is refused, since `1` is not text.

## In a loop

A `return` inside a `loop` ends the loop and the function.

```
fn index_of(xs: List[u8], target: u8) -> Option[u64] {
  loop i in 0..xs.size {
    if xs[i] == target { return .some(i) }
  }
  .none
}
```

`index_of([5, 6, 7], 6)` is `.some(1)`, and with `9` it is `.none`. It is so for every `loop`: over a range, open-ended, and over a type that implements [`Iterate`](iterate.md).

## The nearest function or lambda

`return` leaves the nearest function or [lambda](lambdas.md) around it. Inside a lambda that is passed to a function, it gives the lambda's value and goes no further.

```
fn clamp_all(xs: List[i64]) -> List[i64] ! alloc {
  map(xs) fn(x) {
    if x < 0 { return 0 }
    x
  }
}
```

`return 0` gives `0` for that element. It does not leave `clamp_all`, which goes on to the next element.

## Nothing is cut short

Nothing unwinds. A `return` gives a value to whatever called the function or lambda it is in, and that caller goes on with its next line, as after any call. So after a [`with`](with.md), where the rest of the block is a lambda, the function that was given the lambda still finishes its work.

```
fn header(path: str) -> Result[List[u8], FsError] ! fs, alloc {
  with f = open(path)
  n = try size(f)
  if n < 64 { return .err(.failed("too short")) }
  read(f, 0..64)
}
```

There are three ways out of `header`: the `try`, the `return`, and the last line. On each, `open` gets the value, closes the file, and gives the value on, as [Files](../library/fs.md) describes.

## `return` and `try`

[`try`](try.md) leaves with a failure it was handed. `return` leaves with a value the function chooses.

```
fn port(text: str) -> Result[u16, PortError] {
  n = try parse_int(text)
  if n < 0 or n > 65535 { return .err(.too_big) }
  .ok(n)
}
```

## A value that must be used

A value that must be used exactly once, as [Values used exactly once](resources.md) describes, must be used on the way a `return` leaves by, as on every other way. Here the file is not closed when the `try` leaves.

```
fn peek(path: str) -> Result[u64, FsError] ! fs {
  f = try open_file(path)
  n = try size(f)
  close_file(f)
  .ok(n)
}
```

This is refused. `with f = open(path)` is how it is written, since `open` closes the file on every way out.

## The forms

| Written | Is |
| --- | --- |
| `return e` | The nearest function or lambda gives `e`, and its later lines do not run |
| `return` | The same, in a function or lambda that gives nothing |
| `return e` on the last line | Written as `e`: the formatter takes the `return` away |
| `return` in a `loop` | Ends the loop and the function |
| `return` in a lambda | Gives the lambda's value, and does not leave the function around it |

Next: [Effects](effects.md)
