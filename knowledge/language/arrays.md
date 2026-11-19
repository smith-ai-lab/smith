---
type: specification
title: Arrays and sizes
description: An array is a fixed number of elements of one type, with its size in its type, where a size is a Nat that may be any name that cannot change, and a List is an array kept together with its size.
status: partial
built:
  - "the `size` of a `List` and of a `str`, a `Nat`, which a program writes as a parameter, a result, a field, and inside another type, and an arithmetic on a `Nat` that `arith` proves fits"
  - "not an array type, a size in brackets, or arithmetic on sizes"
---

# Arrays and sizes

An array is a fixed number of elements of one type, and the number is part of its type.

## The type of an array

`[3]f32` is three floats. An array written in brackets, as `[1.0, 2.0, 3.0]`, has that type, as [Literals](literals.md) says.

```
fn pass_on(a: [3]f32) -> [3]f32 { a }
```

`pass_on` takes three floats and gives them back. Two arrays have the same type only when their sizes are the same.

A matrix is an array with two sizes written in one pair of brackets. `[m, k]f32` is `m` rows of `k` floats, an `m` by `k` matrix. [Indexing](indexing.md) says how to read one element of it.

## A size is a `Nat`

A size is a `Nat`, and it may be any name that cannot change. There are four places such a name comes from.

- A name in brackets after the function's name, which the compiler works out from the arguments. Here `n` is the size of whatever array is passed.

  ```
  fn pass_any[n: Nat](a: [n]f32) -> [n]f32 { a }
  ```

  `pass_any` takes an array of any size and gives back one of the same size.
- An ordinary parameter. Here the size is a number the caller gives.

  ```
  fn zeros(n: Nat) -> [n]f32 { for i { 0.0 } }
  ```

  `zeros(4)` is four floats, each `0.0`. [`for`](for.md) builds an array of the size the type names.
- A binding.

  ```
  fn pair(x: f32) -> [2]f32 {
    n = 2
    a: [n]f32 = [x, x]
    a
  }
  ```
- An earlier field of the same struct. A later field's type may mention it.

  ```
  struct Row {
    n: Nat
    cells: [n]f32
  }
  ```

A name that can change cannot be a size. [`var`](var.md) names are the ones that can.

## Arithmetic on sizes

A size inside a type may be worked out from others, as in `[n + w - 1]f32`. The arithmetic is exact and has no limit, because it is reasoned about and never computed.

```
fn widen[n: Nat, w: Nat](a: [n]f32, kernel: [w]f32) -> [n + w - 1]f32
```

`widen` gives an array of `n + w - 1` floats, so a call with arrays of 5 and 3 gives one of 7.

## A size known only when the program runs

A size may be known only when the program runs, as the `n` of `zeros` is. Whatever the size is, the program holds it as a `Nat`, and a `Nat` that the program holds is 64 bits on every target, so the size of an array that exists always fits.

A `Nat` worked out from others, such as `n * 2`, is an ordinary integer once the program holds it, and it must be shown to fit, as any other integer must.

```
fn doubled(n: Nat) -> Nat { n * 2 }
```

This is refused, as `n * 2` needs more than 64 bits and `n` may be too great for it to fit a `Nat`. A count is shown to fit with a [`requires`](contracts.md) or an `if`, as [What arith proves](arith.md) says.

## An array kept with its size

An array that is kept together with its size is a [`List`](../library/list.md). Its size is a field, `size`, which is a `Nat` and so can be read like any other.

```
fn main() ! console, alloc {
  xs: List[u8] = [10, 20, 30]
  print("{xs.size}")
}
```

This prints `3`.

## The forms

| Written | Is |
| --- | --- |
| `[3]f32` | Three floats |
| `[m, k]f32` | An `m` by `k` matrix of floats |
| `[n]f32` with `n: Nat` in brackets | An array of whatever size the argument has |
| `[n + w - 1]f32` | An array whose size is worked out from others, exactly |
| `List[f32]` | Floats kept together with their size |

Next: [Indexing](indexing.md)
