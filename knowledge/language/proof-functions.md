---
type: specification
title: Proofs as functions
description: A proof is a value that is passed, returned, and held, a predicate is an ordinary function that returns bool, and arith reads through a call of one.
status: partial
built:
  - "a predicate in a contract: a function that returns `bool`, carries no effect, and is one expression of comparisons of whole numbers joined with `and`, `or`, and `not`, or a call of another such function, which `arith` reads as that expression with the arguments put in"
---

# Proofs as functions

A proof is a value like any other. It is passed to a function, returned from one, and held in a field, as [a struct that holds a proof](proof-fields.md) shows. Every proof starts from `arith` or from a [`check`](check.md) that passed, and it is erased before code generation.

```
fn keep(at: Nat, size: Nat, p: at <= size) -> at <= size { p }

fn cursor[size: Nat](at: Nat) -> Option[Cursor[size]] {
  match check(at <= size) {
    .some(p) { .some(Cursor(at: at, inside: keep(at, size, p))) }
    .none { .none }
  }
}
```

`keep` takes a proof that `at <= size` and gives it back. The parameter `p` is a proof, so the caller owes one, and here it comes from the `check`. `Cursor` is the struct of [A struct that holds a proof](proof-fields.md), and the proof `keep` gives is the one its field `inside` needs.

## What a proof may not do

A proof is erased before the program runs, so three rules hold of it, which [What is trusted](trust.md) gives the reasons for. A function that returns a proof carries no effect, `diverge` among them. The statement it proves names no `var`. And a proof is passed, held, and given where one is owed, and no `match` picks a value by which proof it was given.

## A fact proved once

A function that takes proofs and returns one proves a fact once for every place that needs it. This is how a fact that `arith` does not decide is supplied: the function that supplies it is written once, and each caller passes the proofs it has.

## A predicate is a function

A predicate used in a contract is an ordinary function that returns `bool`, so the contract can be run on real data too.

```
fn inside(i: Byte, n: Byte) -> bool { i < n }

fn at(i: Byte, n: Byte) -> Byte
  requires inside(i, n)
{
  n - i
}
```

`inside(9, 3)` is `false` and `inside(3, 9)` is `true`. A caller of `at` owes `inside(i, n)`, and `arith` reads through that call to the comparison `i < n` that it returns, so inside `at` it knows that `i < n` and that `n - i` fits.

That is how `arith` proves the `valid` of an [index](indexing.md): `valid(c, i)` is a function that returns a `bool`, and `x[i]` compiles when the comparison it returns is proven.

## The forms

| Written | Is |
| --- | --- |
| `p: a > 0` as a parameter | A proof the caller owes |
| `-> a > 0` as a result | A proof the function gives |
| `requires f(x)` | The comparison that `f(x)` returns, owed by the caller |

Next: [Statements](statements.md)
