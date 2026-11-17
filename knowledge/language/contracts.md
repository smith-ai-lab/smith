---
type: specification
title: Contracts and proofs
description: requires, ensures, and check, what the arith procedure can prove, termination, and hole.
status: partial
built: "`arith`, which decides linear arithmetic over the integers and proves that a plain `+`, `-`, or `*` fits, from the range of each value's type, the value of a literal, what a binding that is not a `var` was given, and the condition of each `if` the operation is inside, joined with `and`, `or`, and `not`; and `requires` and `ensures` on a function, each a comparison of whole numbers joined the same way over its parameters and the result's name, which `arith` proves of the arguments at every call and of what the function returns at each place it returns, and takes as facts in the other place"
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

A statement about values, such as `n > 0`, is a type, and a value of that type is a proof of the statement. A contract is a proof that travels with a call. `requires` is a proof the caller passes, and `ensures` is a proof the function gives back with its result. Most are never written out: `arith`, a small deterministic procedure for arithmetic, builds them. Where `arith` cannot, the author supplies the proof. The compiler only checks. It never searches for a proof, and nothing is ever tested at run time in a proof's place.

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
- It uses every fact in scope: the range of each value's type, the value of a literal, the condition of an `if` or a `match` block it is inside, the range of a `loop i in a..b`, the function's `requires`, the `ensures` of a function that was called, and every proof it can see, as a value or as a field of a value.
- A `var` has the facts its type states, and, where no loop assigns it, those of the value it was last given. Inside one round of a loop that assigns it, it also has those of the value that round last gave it, stated against what it held when the round began.

A function type states no contract, so a function with a `requires` is only called by its name, and using its name as a value is an error. A function with only an `ensures` can be a value, and what it gives is then not known to the caller.

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

A fact that must hold each time round a `loop` is stated in a type, since a `var`'s type never changes. A place in a source text that is never past its end is such a type, and when the field for the proof is left out of a construction, `arith` builds it, or the construction is an error.

```
struct Cursor[size: Nat] {
  at: Nat
  inside: at <= size
}

fn step[size: Nat](c: Cursor[size]) -> Cursor[size] {
  if c.at < size { Cursor(at: c.at + 1) } else { c }
}
```

Inside the `if`, `c.at < size` gives `c.at + 1 <= size`, which is the proof the new `Cursor` needs, and it shows that `c.at + 1` fits.

An open-ended loop, and a function that calls itself on something other than a part of its argument, is proven to end by `decreases`, which names one expression of an unsigned integer type. Wherever the loop goes round again, `arith` must prove that the expression is smaller than it was when that round began, and wherever the function calls itself, that the expression of the call's arguments is smaller than that of its own. An unsigned number cannot go below zero, so neither can go on forever. The expression is never computed when the program runs.

```
fn skip_spaces[size: Nat](text: [size]u8, start: Cursor[size]) -> Cursor[size] {
  var c = start
  loop decreases size - c.at {
    if c.at == size { break }
    if text[c.at] != 32 { break }
    c = Cursor(at: c.at + 1)
  }
  c
}

fn first_zero[n: Nat](x: [n]u8, from: Nat) -> Option[Nat]
  decreases n - from
{
  if from >= n { .none } else if x[from] == 0 { .some(from) } else { first_zero(x, from + 1) }
}
```

In `skip_spaces`, a round that goes on has passed `c.at == size`, so `c.at < size`, and it ends with `c.at` one greater, so `size - c.at` is one less. In `first_zero`, the call is made where `from < n`, so `n - (from + 1)` is below `n - from`.

There is no `panic`. A `check` that fails returns `.none`, and a failure that can really happen is returned as a `Result`. A function with none of the `alloc`, `console`, and `diverge` effects therefore returns a value for every input its `requires` lines allow. The first two can [end the task](tasks.md) when the system runs out of what they stand for.

- A proof is a value. It is passed, returned, and held in a field like any other, and a function that takes proofs and returns one proves a fact once for every place that needs it, which is how a fact `arith` does not decide is supplied. Every proof starts from `arith` or from a `check` that passed, and it is erased before code generation. `check` and `hole` are built in, because nothing written in Smith could make a proof from nothing.
- Predicates used in contracts are ordinary functions returning `bool`, so a contract can be run on real data.
- Every function terminates unless it carries `diverge`, which means it may not finish. Callers inherit `diverge`, and it is not allowed in a contract or a GPU kernel. A `loop` over a range, an array, or an `Iterate` type ends, and so does structural recursion. Anything else ends because of a `decreases`, described below.
- `hole(name)` stands in for code or a proof not yet written. The compiler reports the type and the goal it needs.

Facts that recur belong in a type, so they are proved once and travel with the value. A struct with exactly one field that is not a proof can be used wherever that field's type is expected, because when the program runs it is that value and nothing more. So a `Fin[n]` is used as a `Nat`, and the fact that it is below `n` goes with it into `arith`. It goes one way only: making a `Fin` from a `Nat` needs the proof. `Pos` is defined the same way, as a `Nat` proven above zero, so a size declared `[n: Pos]` needs no `requires n > 0`. Tooling proposes the remaining `requires` lines, as it proposes effects.

```
struct Pos {
  value: Nat
  above: value > 0
}
```

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
