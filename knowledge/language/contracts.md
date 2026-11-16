---
type: specification
title: Contracts and proofs
description: requires, ensures, and check, what the arith procedure can prove, termination, and hole.
---

# Contracts and proofs

A signature can state what must hold. `requires` is owed by the caller and `ensures` by the function, about a result the signature names. Both are checked at compile time.

```
fn average[n: Nat](x: [n]f32) -> f32
  requires n > 0
{
  sum(x) / float(n)
}

fn clamp(x: f32, lo: f32, hi: f32) -> r: f32
  requires lo <= hi
  ensures lo <= r and r <= hi
{
  max(lo, min(x, hi))
}
```

A statement about values, such as `n > 0`, is a type, and a value of that type is a proof of the statement. A contract is a proof that travels with a call. `requires` is a proof the caller passes, and `ensures` is a proof the function gives back with its result. Neither is written out: `arith`, a small deterministic procedure for arithmetic, builds them. The compiler only checks. It never searches for a proof.

| Keyword | Who owes the proof | About |
| --- | --- | --- |
| `requires` | The caller | The arguments |
| `ensures` | The function | The result, by the name the signature gives it |
| `check` | Nobody: it tests at run time | Outside data. Yields a proof if the test passes |

`arith` decides linear arithmetic over integers and nothing more. That is what lets it always answer yes or no, in a time that can be predicted.

- It decides comparisons between sums and differences of variables and constants, where any multiplication is by a constant, such as `i + k < n + w - 1` and `2 * n + 1 <= m`. Comparisons may be joined with `and`, `or`, and `not`.
- It does not reason about a product of two variables, or about dividing by a variable. It treats such a term as one unknown quantity, and knows only the facts stated about that same term.
- When it cannot prove a goal, the compiler reports the goal. The author supplies the missing fact with a `requires`, with a `check`, or with a type that carries the proof.
- A subtraction `a - b` on `Nat` compiles only when `arith` can show `b <= a`.

So `rows * cols <= limit` is never proven from facts about `rows` and `cols` alone. A function that needs it states it in a `requires`, and a caller with sizes known only at run time gets it from a `check`.

A struct can hold a proof as a field, whose type is the statement it proves. `Fin[n]` from [Arrays and sizes](arrays.md) is defined that way in the standard library, as a number paired with a proof that it is below `n`. `check` is how outside data becomes one.

```
struct Fin[n: Nat] {
  value: Nat
  below: value < n
}

fn token[v: Nat](raw: Nat) -> Option[Fin[v]] {
  match check(raw < v) {
    .some(p) { .some(Fin(value: raw, below: p)) }
    .none { .none }
  }
}
```

There is no `panic`. A `check` that fails returns `.none`, and a failure that can really happen is returned as a `Result`. A function with none of the `alloc`, `console`, and `diverge` effects therefore returns a value for every input its `requires` lines allow. The first two can [end the task](tasks.md) when the system runs out of what they stand for.

- A proof comes from `arith` or from a `check` that passed, and it is erased before code generation. `check` and `hole` are built in, because nothing written in Smith could make a proof from nothing.
- Predicates used in contracts are ordinary functions returning `bool`, so a contract can be run on real data.
- Every function terminates unless it carries `diverge`, which means it may not finish. Structural recursion is accepted automatically, and anything else states a decreasing measure. Callers inherit `diverge`, and it is not allowed in a contract or a GPU kernel.
- `hole(name)` stands in for code or a proof not yet written. The compiler reports the type and the goal it needs.

Facts that recur belong in a type, so they are proved once and travel with the value. `Pos` is a `Nat` proven above zero and can be used wherever a `Nat` is expected, so a size declared `[n: Pos]` needs no `requires n > 0`. It is a struct with a proof as a field, as `Fin` is. Tooling proposes the remaining `requires` lines, as it proposes effects.

```
fn average[n: Pos](x: [n]f32) -> f32 { sum(x) / float(n) }
```

Index arithmetic is allowed wherever the sizes make it provable, so no index is ever checked at run time. A convolution states its input's size in terms of its output's, and `arith` proves that `i + k` is in range.

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

Everyday code uses helpers that carry these proofs. `pad(x, 1, 1, 0.0)` adds a border, and `windows(x, 3)` gives every window of length 3, with an optional `stride`. Both are views that fuse into the loop that reads them. `for` is the primitive, and most users work with the layer above it: element-wise operators, `@`, reductions, and helpers such as these.

Next: [Storage and arenas](storage.md)
