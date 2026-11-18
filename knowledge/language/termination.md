---
type: specification
title: Termination
description: Every function ends unless it carries diverge, which loops are known to end, and decreases, which proves that a function that calls itself ends.
status: partial
built:
  - "`loop decreases e` on an open-ended loop, where `e` is an expression of an unsigned type that `arith` proves smaller at every place the loop goes round again, and which is never computed"
  - "`diverge` on an open-ended `loop`, which its callers inherit, and a function whose every `loop` is over a range"
  - "not `decreases` on a function, or a function that calls itself"
---

# Termination

Every function ends unless it carries `diverge`, which means it may not finish. A function that ends is one a caller can rely on to give a value.

```
fn sum_below(n: u64) -> u64 {
  var total: u64 = 0
  loop i in 0..n { total = wrapping_add(total, i) }
  total
}
```

`sum_below(10)` is `45`. It carries no effect, and the compiler knows it ends because a `loop` over a range ends: the range is worked out once, as [`loop`](loop.md) says. So does a `loop` over an array or over an `Iterate` type, and so does a recursion that goes only into a part of its argument.

```
fn total(xs: List[u64]) -> u64 {
  if xs.size == 0 { 0 } else { wrapping_add(xs[0], total(xs[1..])) }
}
```

`total` calls itself only on `xs[1..]`, which is a part of `xs`, as [Ranges and slices](ranges.md) says, so it ends and carries no effect. `total([4, 9, 6])` is `19`.

## What may not end

A loop that can go on is an [open-ended `loop`](open-loop.md), and its function carries `diverge`. So does every function that calls it: callers inherit `diverge`, and a caller that does not list it is refused, as that page shows.

`diverge` is not allowed in a contract or in a [GPU kernel](devices.md).

```
fn settled(n: u64) -> bool ! diverge { n == 0 }

fn first(n: u64) -> u64
  requires settled(n)
{
  n
}
```

This is refused: "`settled` needs the `diverge` effect, which `first` does not declare".

## Proving that a function ends

Anything that is not a loop over a range, a loop over an array or an `Iterate` type, or structural recursion ends because of a `decreases`, which names one expression of whole numbers, the measure. For an open-ended loop it is written `loop decreases e`, as [the open-ended `loop`](open-loop.md#a-loop-that-is-proven-to-end) shows. For a function that calls itself on something other than a part of its argument, it is written after the signature.

```
fn first_zero(x: List[u8], from: u64) -> Option[u64]
  decreases x.size - from
{
  if from >= x.size {
    .none
  } else if x[from] == 0 {
    .some(from)
  } else {
    first_zero(x, from + 1)
  }
}
```

Wherever the function calls itself, `arith` must prove that the expression of the call's arguments is not below zero and is smaller than that of its own. The call is made where `from < x.size`, so `x.size - (from + 1)` is below `x.size - from`. `first_zero([5, 0, 7], 0)` is `.some(1)`. A number that gets smaller at each call and never goes below zero cannot do so forever, so the calls end. The expression is never computed when the program runs, so it is reasoned about as an exact number.

A call whose expression is not smaller is refused where it is made. In `first_zero`, the call `first_zero(x, from)` leaves `x.size - from` as it was, so it is refused.

## Functions that call each other

Functions that call each other in a circle each state a `decreases`, and a call from one to another is held to the rule above: the measure of the call's arguments, as the function that is called states it, is smaller than the caller's own.

One number is often not enough. A parser's `sum` calls `product` before it has read anything, so the tokens that are left are as many as they were. So a measure may have several parts, separated by commas, and it is compared as words are in a dictionary: a call must make the first part smaller, or leave the first as it was and make the second smaller, and so on.

```
fn sum(toks: List[Token], at: u64) -> Parsed
  decreases toks.size - at, 2
{
  left = product(toks, at)
  ...
}

fn product(toks: List[Token], at: u64) -> Parsed
  decreases toks.size - at, 1
{
  left = atom(toks, at)
  ...
}

fn atom(toks: List[Token], at: u64) -> Parsed
  decreases toks.size - at, 0
{
  if at < toks.size and toks[at] == .open {
    sum(toks, at + 1)
  } else {
    ...
  }
}
```

`sum` calls `product` at the same place, so the first part stays and the second goes from `2` to `1`. `product` calls `atom` the same way, from `1` to `0`. `atom` calls `sum` one token on, so the first part gets smaller, and the second may be anything. The first part says that input was read, and the second which of the functions is the lower. Every part is proven not below zero, as a measure of one part is.

The measures of functions that call each other have the same number of parts. A function in the circle with no `decreases` is refused, and so is a call that makes the measure no smaller.

A loop's measure may have several parts too, written `loop decreases a, b { ... }`, and each time round is held to the same rule.

## The forms

| Written | Means |
| --- | --- |
| `loop i in a..b`, a recursion into a part of the argument | Ends, and carries nothing |
| `decreases a, b` | A measure of two parts: `a` gets smaller, or `a` stays and `b` gets smaller |
| `loop { ... }` | May not end, and its function carries `diverge` |
| `loop decreases e { ... }` | Ends, because `e` is smaller each time round again |
| `fn f(...) -> T decreases e` | Ends, because `e` is smaller at each call of `f` from itself |
| `! diverge` | Callers inherit it, and a contract or a GPU kernel may not carry it |

Next: [A struct that holds a proof](proof-fields.md)
