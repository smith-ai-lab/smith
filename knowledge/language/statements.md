---
type: specification
title: Statements
description: A statement is a type built from what data is built from, a proof is a value of it, and a boolean expression is the statement that it is true, so there is no second language for proofs.
status: planned
---

# Statements

A statement is a type, and a proof of it is a value of that type. A boolean expression is the statement that it is true, which is the kind the pages before this one use.

```
fn first(xs: List[u8], p: xs.size > 0) -> u8 { xs[0] }
```

`xs.size > 0` is a statement, and `p` is a proof of it. Most statements a program needs are of this kind, and [`arith`](arith.md) finds their proofs, so they are written in a `requires` and no proof is named.

## Why a statement is a type

A statement about one value is a boolean: `i < xs.size` is true or it is not. A statement about every value of some kind is not one. "Every element of `xs` is below `n`" cannot be worked out once and held as `true`, and "for every two lists, `append(a, b)` has the size of both" cannot be worked out at all.

So a statement is a type, and what is built from statements is built as data is. There is no second language for proofs: the constructs are the ones a program already has.

| In words | Is the type | A proof of it is |
| --- | --- | --- |
| `e` is true | The boolean expression `e` | Found by `arith`, or given by a [`check`](check.md) |
| P and Q | A tuple or a struct of the two | A proof of each, as `(p, q)` |
| P or Q | An enum with a case for each | One case, holding its proof |
| P gives Q | A function type from P to Q | A function that makes a proof of Q from one of P |
| For every `x`, P | A function type whose result names its parameter | A function that takes `x` and gives the proof for it |
| There is an `x` with P | A struct that holds `x` and the proof | The value and its proof, as [`Fin`](proof-fields.md) is |
| Never | An enum with no case | Nothing: there is no value of it |

## A statement about every one

A function type may name its parameters, so that the type of what it gives can speak of them. That is how "for every" is written.

```
struct Sorted {
  xs: List[u8]
  ordered: fn(i: Nat, j: Nat, p: i < j and j < xs.size) -> xs[i] <= xs[j]
}
```

A `Sorted` is a list together with a proof that it is in order. The proof is a function: given any two places `i` and `j`, and a proof that `i` comes before `j` inside the list, it gives a proof that the element at `i` is not above the one at `j`. A function that takes a `Sorted` knows its list is in order, as a function that takes a [`Fin`](proof-fields.md) knows its number is below the size.

## A proof by induction

A proof about every number, or every list, is a function that calls itself, and it is held to the rule every function is held to: it [ends](termination.md). The call on the smaller value is the step of the induction, and the check that the calls end is what makes it one.

```
fn size_of_append(a: List[u8], b: List[u8]) -> append(a, b).size == a.size + b.size
  decreases a.size
```

`size_of_append` is declared here without its body. Its result is a statement that names its parameters, and a call of it gives the caller that statement for the two lists it passes.

## The kinds stay apart in use, not in kind

- A statement that is a boolean expression can be worked out, so `arith` may decide it and a `check` may test it when the program runs. This is the everyday kind, and nothing about it changes.
- A statement that is a function type cannot be worked out. It is proven once, by a function someone writes, and the kernel checks that function, as [What is trusted](trust.md) says. The compiler does not look for it.

A proof is written once. The compiler checks proofs and never searches for one, so a program takes no longer to build because it is proven.

## What is not settled

When two values are the same, as against `==` being true of them, is not settled. A program's own `Eq` may call two values equal that differ, and a proof must not take one for the other.

## The forms

| Written | Is |
| --- | --- |
| `p: a < 10` | A proof that the boolean expression is true |
| `(p, q)` | A proof of both |
| `fn(p: P) -> Q` | A proof that P gives Q |
| `fn(i: Nat, p: i < n) -> xs[i] < m` | A proof for every `i` below `n` |
| A struct with a value and a proof | A proof that there is such a value |

Next: [hole](hole.md)
