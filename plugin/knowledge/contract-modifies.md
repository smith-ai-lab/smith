---
type: concept
title: What a function writes
description: contract.modifies names the only objects and ranges a function writes, so smith prove refutes a write outside them and a caller keeps everything else it knew.
tags: [contracts, prove]
---
# What a function writes

`contract.modifies(...)` is a call after the `requires` that names what the function writes; it does nothing at run time. `smith prove` refutes a write of the body outside the named ranges and its own locals, and at a call only those ranges become arbitrary, so a caller keeps every other byte without saving `old_*` copies. A function without `modifies` may write whatever its pointer parameters reach.

## Example

```smith
import std.contract

pub struct Moves:
    pub len: u32
    pub jumps: u32

pub fn add_jump(moves: *mut Moves):
    contract.requires(moves.jumps < 100)
    contract.modifies(&moves.jumps)
    moves.jumps += 1
    contract.ensures(moves.jumps <= 100)

pub fn use_it(moves: *mut Moves) -> u32:
    contract.requires(moves.jumps < 100 and moves.len == 7)
    add_jump(moves)
    contract.assert(moves.len == 7)
    moves.len
```

## Rules

- Each argument is `&x` (one object, its size from the type) or a slice (a range: `xs`, or `p[a:b]` of a pointer or an array, its count times the element size, none when empty). Anything else is `static assertion failed: contract.modifies takes &x ... or a slice`. `contract.modifies(xs[0:2], &count)` names two.
- With no argument the function writes nothing outside its locals.
- Put it before the first write. A function with `modifies` is used through its contract even with no `ensures`.
- `p[a:b]` of a `*T` (one object) needs its extent to be known, as a read does ([prove-slices](prove-slices.md)).
- A failed body shows `FAIL f` and `file:12: the function writes here, outside the ranges its smith_modifies names`.

- **Decided (#3190, item 18):** the language will have no `modifies`: what a function writes will be inferred from its body and the fixer already deletes the call. Until `smith prove` infers it, this call is how a function states its write set.

## Interactions

- Calls: the callee's pointer arguments go to a scratch object and only its ranges become arbitrary before its `ensures`, so the cost follows the ranges, not the object ([prove-calls](prove-calls.md)).
- A `const` pointer parameter is not written by the call, and callers rely on that.
- C has no slices: `smith_modifies(p, size)`, one call for each range, `smith_modifies(&moves->jumps, sizeof moves->jumps)`; `smith_modifies(nullptr, 0)` writes nothing; a pointer and a count is `smith_modifies(xs, n * sizeof *xs)`, and the extent of `xs` must be known to the prover (`int32_t xs[static 4]`, a field).

## Replaces from C

Comments such as "only touches `jumps`" and `old_len == len` checks after a call.
