---
type: specification
title: A struct that holds a proof
description: A statement about values is a type, a struct holds a proof as a field where a fact joins two values, a fact about one number is its range, an index that travels is a range, and a fact that must hold in every round of a loop is kept in a type.
status: planned
---

# A struct that holds a proof

A statement about values, such as `at <= size`, is a type, and a value of that type is a proof of the statement. A struct can hold a proof as a field, whose type is the statement it proves.

```
struct Cursor[size: Nat] {
  at: Nat
  inside: at <= size
}
```

A `Cursor` is a place in a text of `size` bytes together with the proof that it is not past the end. The proof is erased before code generation, as every proof is, so a `Cursor` is one `Nat` when the program runs.

A fact about one number alone needs no struct, as its range says it: a number above zero is a [`Pos`](number-types.md), and a number below `n` is a `0..n`. A struct holds a proof where the fact joins two values, as `at` and `size` here, or is more than a range can say.

## Making one

The field for the proof is left out of a construction. `arith` builds it from what is known, or the construction is refused.

```
fn first() -> Cursor[10] { Cursor(at: 4) }
```

`first()` is a `Cursor[10]`, since `4 <= 10`. `Cursor(at: 11)` is refused there, since `11 <= 10` cannot be proven. A value that is known only when the program runs needs a [`check`](check.md) to give the proof.

## A fact that must hold in every round

A `var`'s type never changes, so a fact that must hold each time round a `loop` is stated in a type. A `Cursor` is such a type: a `var` of it is never past the end, whatever the loop does to it.

```
fn step[size: Nat](c: Cursor[size]) -> Cursor[size] {
  if c.at < size { Cursor(at: c.at + 1) } else { c }
}
```

Inside the `if`, `c.at < size` gives `c.at + 1 <= size`, which is the proof the new `Cursor` needs, and it shows that `c.at + 1` fits. Outside the `if`, `Cursor(at: c.at + 1)` is refused.

## A size that is not zero

A size declared `[n: Pos]` is above zero wherever it is used, as `Pos` is the range from 1, so a function over it needs no `requires n > 0`.

```
fn average[n: Pos](x: [n]f32) -> f32 { sum(x) / float(n) }
```

Where a size is only a `Nat`, the same function states the fact with `requires`.

```
fn average[n: Nat](x: [n]f32) -> f32
  requires n > 0
{
  sum(x) / float(n)
}
```

Tooling proposes the remaining `requires` lines, as it proposes effects.

## An index that travels

A number that is below a size, kept in a field or passed on, is a range `0..n`. It indexes an axis of that size wherever it goes, with nothing more to prove, and it needs no struct, as the proof is its type.

```
struct Cell[n: Nat] {
  at: 0..n
}

fn read[n: Nat](xs: [n]f32, c: Cell[n]) -> f32 { xs[c.at] }
```

A `0..n` is used as a `Nat` wherever one is expected, as a range goes where a wider one is, and `arith` has `value < n` of it. [`check`](check.md) shows how outside data becomes one.

## Sizes that are proven

Index arithmetic is allowed wherever the sizes make it provable, so no index is ever checked when the program runs. A convolution states its input's size in terms of its output's, and `arith` proves that `i + k` is in range.

```
fn conv[n: Nat, w: Pos](x: [n + w - 1]f32, kernel: [w]f32) -> [n]f32 {
  for i { sum(for k { x[i + k] * kernel[k] }) }
}

fn conv2d[h: Nat, w: Nat, kh: Pos, kw: Pos](
  img: [h + kh - 1, w + kw - 1]f32,
  kernel: [kh, kw]f32,
) -> [h, w]f32 {
  for y, x { sum(for i, j { img[y + i, x + j] * kernel[i, j] }) }
}
```

Everyday code uses helpers that carry these proofs. `pad(x, 1, 1, 0.0)` adds a border, and `windows(x, 3)` gives every window of length 3, with an optional `stride`. Both are views that fuse into the loop that reads them. [`for`](for.md) is the primitive, and most users work with the layer above it: element-wise operators, `@`, reductions, and helpers such as these.

## The forms

| Written | Is |
| --- | --- |
| `name: statement` in a struct | A field that is a proof of the statement |
| `Cursor(at: e)` | A `Cursor`, with its proof built by `arith` |
| `[n: Pos]` | A size that is not zero, as `Pos` is the range from 1 |
| `0..n` | A number proven below `n` |

Next: [check](check.md)
