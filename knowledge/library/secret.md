---
type: specification
title: Secret
description: Secret[T], a value such as a key that the time a program takes must not depend on, which cannot steer a branch or be an index because it lacks the traits those need.
status: planned
---

# Secret

A `Secret[T]` holds a value that must not be learned from how long the program takes: a key, a password, a part of a signature. Code that stops at the first byte that differs tells whoever times it how many bytes were right. A `Secret` cannot be used that way, and the compiler refuses the code, as it refuses an index that may be out of range.

```
use std.secret (Secret, secret, reveal, is_zero)
use std.bits (bit_or, bit_xor)

fn same(a: Secret[[32]u8], b: Secret[[32]u8]) -> bool {
  var diff: Secret[u8] = secret(0)
  loop i in 0..32 { diff = bit_or(diff, bit_xor(a[i], b[i])) }
  reveal(is_zero(diff))
}
```

`same` looks at all 32 bytes whatever they hold, so it takes the same time for every pair.

## What a secret can do

A `Secret` of a whole number has the operations whose time does not depend on the numbers: the `wrapping_` functions for adding, subtracting, and multiplying, and the functions of [`Bits`](bits.md). Each gives a `Secret`. An element of a `Secret` of an array is a `Secret`, and the index is an ordinary number.

`is_zero(s)` gives a `Secret[bool]`, and `select(which, a, b)` gives `a` where a `Secret[bool]` is true and `b` where it is false, and runs the same instructions for both.

## What a secret cannot do

A `Secret` has no [`Eq`](../language/comparisons.md) and no `Ord`, so comparing one gives no `bool`, and nothing made of it can be the condition of an `if`, the value of a `match`, or the end of a `loop`.

```
fn leaks(key: Secret[u8], guess: u8) -> bool { key == secret(guess) }
```

This is refused: a `Secret[u8]` has no `Eq`. A `Secret` is not an index, so `table[key]` is refused, as which place of memory is read shows in the time. It has no division and no remainder, which take a time that depends on the numbers on real processors. It has no [`Fmt`](../language/interpolation.md) and no `Repr`, so it is not printed by mistake.

None of this is a rule of its own. `Secret` is a struct of the library with a private field, and what it cannot do is the traits it does not have.

## Making and revealing

`secret(x)` makes a `Secret` of `x`. `reveal(s)` gives the value back as an ordinary one, and is the only way out, so every place where a secret becomes known is a call of `reveal` that can be searched for. What is revealed is the answer, as `same` reveals whether the two were equal, and never the key.

## What the compiler promises

An operation of a `Secret` is built as instructions whose time does not depend on what they are given, on every target, and no pass turns one into a branch or into a read at an address made from it. A test builds each operation for each target and looks at the instructions.

## The forms

| Written | Is |
| --- | --- |
| `secret(x)` | `x` as a `Secret` |
| `reveal(s)` | The value of `s`, as an ordinary one |
| `bit_xor(a, b)`, `wrapping_add(a, b)` | A `Secret`, in a time that does not depend on `a` and `b` |
| `is_zero(s)` | A `Secret[bool]` |
| `select(which, a, b)` | `a` or `b`, with the same instructions run for either |
| `a == b`, `if`, `match`, `table[s]`, `a / b`, `"{s}"` | Refused |
