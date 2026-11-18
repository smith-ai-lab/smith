---
type: specification
title: Termination
description: Every function ends unless it carries diverge, which loops are known to end, and decreases, which proves that a function that calls itself ends.
status: partial
built: "`loop decreases e` on an open-ended loop, where `e` is an expression of an unsigned type that `arith` proves smaller at every place the loop goes round again, and which is never computed; `diverge` on an open-ended `loop`, which its callers inherit, and a function whose every `loop` is over a range; not `decreases` on a function, or a function that calls itself"
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

Anything that is not a loop over a range, a loop over an array or an `Iterate` type, or structural recursion ends because of a `decreases`, which names one expression of an unsigned integer type. For an open-ended loop it is written `loop decreases e`, as [the open-ended `loop`](open-loop.md#a-loop-that-is-proven-to-end) shows. For a function that calls itself on something other than a part of its argument, it is written after the signature.

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

Wherever the function calls itself, `arith` must prove that the expression of the call's arguments is smaller than that of its own. The call is made where `from < x.size`, so `x.size - (from + 1)` is below `x.size - from`. `first_zero([5, 0, 7], 0)` is `.some(1)`. An unsigned number cannot go below zero, so the calls cannot go on forever, and the expression is never computed when the program runs.

A call whose expression is not smaller is refused where it is made. In `first_zero`, the call `first_zero(x, from)` leaves `x.size - from` as it was, so it is refused.

## The forms

| Written | Means |
| --- | --- |
| `loop i in a..b`, a recursion into a part of the argument | Ends, and carries nothing |
| `loop { ... }` | May not end, and its function carries `diverge` |
| `loop decreases e { ... }` | Ends, because `e` is smaller each time round again |
| `fn f(...) -> T decreases e` | Ends, because `e` is smaller at each call of `f` from itself |
| `! diverge` | Callers inherit it, and a contract or a GPU kernel may not carry it |

Next: [A struct that holds a proof](proof-fields.md)
