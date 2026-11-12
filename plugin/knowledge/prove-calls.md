---
type: concept
title: Proving across calls
description: A callee with an ensures is used through its contract and any other is inlined; pointer parameters, restrict checked at each call, what a caller may assume after a call, returned pointers and lemmas.
tags: [prove, calls]
---
# Proving across calls

A call of a function with an `ensures` proves its `requires` at the call and then assumes its `ensures`. A call of any other function is inlined, so its contracts and checks are proved where it is called.

## Example

```smith
import std.contract

pub fn copy(dst: []mut u8, src: []u8):
    contract.requires(countof(dst) >= countof(src))
    mut i: usize = 0
    while i < countof(src):
        contract.invariant(i <= countof(src))
        dst[i] = src[i]
        i += 1
```

`copy(b[0:4], b[4:8])` proves with no contract. `copy(b[1:5], b[0:4])` is refused at the call: `the call of bytes::copy passes dst (restrict) and src slices or ranges that may overlap`.

## Rules

- **A pointer parameter** is a valid object of its type (not null, aligned for what the body does, writable if the body stores through it); a struct passed by value is a value, never poison. **Two pointer parameters may be the same object**, whatever their types, so a claim that holds only when they are apart fails.
- **`restrict`** makes a pointer a different object from every other pointer parameter unless both are only read. It is checked at each call from a proved function and fails at the call naming both parameters. A callee that uses a `restrict` pointer as an array has no range the prover knows: that is an error at its declaration. Take a slice instead; it carries its count and the call proves the two ranges apart.
- **After a call a caller may assume the callee's `ensures` and nothing else the body does.** The memory the call may write is what the callee's pointer parameters point to, plus what LLVM infers; `smith prove -v` says what it found in the body. That memory holds arbitrary values, then the `ensures` is assumed. A caller that relies on something the callee does not promise is refuted with a counterexample.
- **Say what stays the same** with `contract.modifies` ([contract-modifies](contract-modifies.md)), or without it with `moves.diagonals.len == old_diagonals`.
- **A pointer a function returns under an `ensures`** is a whole object of its type, aligned, or null. It is proved at each `return` (one past the end, or a pointer to a local, is refused) and assumed by callers, which still need `ensures(result != nullptr)` to load through it. A trusted function's result is its promise only: nothing is assumed of the pointer.
- **A lemma** is a function with a `requires` and an `ensures` and no result: calling it gives its `ensures` at the arguments once its `requires` is proved there. A function called inside a contract is part of the claim; its body or its contract says what its value is. A recursive function is neither proved nor usable through its contract.
- A function with a variable argument list is an error where a proved function calls it.

## Interactions

- A contract is paid for at every call, since its `ensures` is assumed in each caller's queries: give the callee the `ensures` its callers need and no exact specification ([prove-cost](prove-cost.md)).
- A trusted callee's calls are not checked ([prove-trusted](prove-trusted.md)).

## Replaces from C

`restrict` as a promise nobody checks, and comments on what a callee leaves alone.
