---
type: specification
title: A struct that holds a proof
description: A statement about values is a type, a struct holds a proof as a field, Pos is defined that way, an index that travels is a range, and a fact that must hold in every round of a loop is kept in a type.
status: planned
---

# A struct that holds a proof

A statement about values, such as `n > 0`, is a type, and a value of that type is a proof of the statement. A struct can hold a proof as a field, whose type is the statement it proves.

```
struct Pos {
  value: Nat
  above: value > 0
}
```

A `Pos` is a `Nat` together with the proof that it is above zero. The proof is erased before code generation, as every proof is.

## Making one

The field for the proof is left out of a construction. `arith` builds it from what is known, or the construction is refused.

```
fn four() -> Pos { Pos(value: 4) }
```

`four()` is a `Pos`, since `4 > 0`. `Pos(value: 0)` is refused, since `0 > 0` cannot be proven. A value that is known only when the program runs needs a [`check`](check.md) to give the proof.

## Using it as what it holds

A struct with exactly one field that is not a proof can be used wherever that field's type is expected, since when the program runs it is that value and nothing more. So a `Pos` is used as a `Nat`, and the fact that it is above zero goes with it into `arith`.

```
fn double(p: Pos) -> Nat { p + p }
```

It goes one way only: making a `Pos` from a `Nat` needs the proof, so `Pos(value: n)` with an `n` that may be zero is refused.

A size declared `[n: Pos]` is above zero wherever it is used, so a function over it needs no `requires n > 0`.

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

## A fact that must hold in every round

A `var`'s type never changes, so a fact that must hold each time round a `loop` is stated in a type. A place in a source text that is never past its end is such a type.

```
struct Cursor[size: Nat] {
  at: Nat
  inside: at <= size
}

fn step[size: Nat](c: Cursor[size]) -> Cursor[size] {
  if c.at < size { Cursor(at: c.at + 1) } else { c }
}
```

When the field for the proof is left out of a construction, `arith` builds it, or the construction is refused. Inside the `if`, `c.at < size` gives `c.at + 1 <= size`, which is the proof the new `Cursor` needs, and it shows that `c.at + 1` fits. Outside the `if`, `Cursor(at: c.at + 1)` is refused.

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
| `Pos(value: e)` | A `Pos`, with its proof built by `arith` |
| `[n: Pos]` | A size known to be above zero |
| `0..n` | A number proven below `n` |
| `p` where a `Nat` is expected | The `value` of a `Pos` |

Next: [check](check.md)
