---
type: concept
title: What smith prove proves
description: smith prove proves every function with a contract of its own, minus the trusted ones, for every input that meets the requires; how to run it on files or a project and read its verdict lines.
tags: [prove, contracts]
---
# What smith prove proves

`smith prove` checks the contracts of a program with Alive2, Bitwuzla and Z3. Every full smith has it; the stage0, built by `cc` alone, says it has none and exits 2.

## Example

```smith
import std.contract

pub fn half(n: u32) -> u32:
    contract.requires(n % 2 == 0)
    result := n / 2
    contract.ensures(result * 2 == n)
    result
```

```text
$ smith prove
smith prove: ok (3.9 s)
             2 of 10 functions have contracts; smith prove -v lists the 8 without
```

## Rules

- **The default set is every function with a contract of its own** (`requires`, `ensures`, `invariant` or `assert`, C's `assert` too), minus the trusted ones. A function with only asserts (`main` too) is proved. A function with none is not proved and nothing says anything of it; a proved function that calls one reads its body.
- **Proved**: every `ensures`, `invariant` and `assert`, every undefined-behaviour check (signed overflow, a bad shift, division by zero, a float cast out of range, an index out of range, an invalid access, a poison value used), for every input that meets the `requires`. The `requires` is assumed.
- **Errors, never assumptions**, each naming the place and what would make it provable: recursion, a call through a function pointer, inline assembly, a function with neither body nor contract, an Objective-C message send, a loop with no constant bound, and a function with several returns whose `ensures` is not before one. For the first four and a libc call the way out is [prove-trusted](prove-trusted.md).
- **A failure** prints the contract and its line, the call it breaks and a counterexample's values by their names in the source.
- **Project form**: `smith prove` with no arguments proves the C and `.smith` sources of each `smith_program` together as they link, with the build's flags; `smith check` runs it. It prints one line and writes the rest, trusted lines included, to `.smith/log/prove.log`; the second line is the coverage and nothing fails on low coverage. `-v` lists each function without a contract as `without  name  (file:line)`.
- **File form**: `smith prove main.c util.c -- -I .smith/include -DDEBUG`; the flags after `--` are for the compile. A `.smith` file keeps its names (`geometry::box::clamp`), and its imports must be named in the same command or be headers on the include path. The file form cannot name what `smith_failure` writes, so a loop with an invariant says it has no bound: prove the project.
- Options: `-v`, `--budget=SECONDS`, `--emit-ir=FILE`, `--no-cache`, `--function=NAME` (one function; an unknown name or a function with no contract is an error), `--induction`, `--unroll`.
- With no contract anywhere it prints `smith: prove: nothing to prove: ...`, `0 of N functions have contracts`, and succeeds. A `static` function nothing calls is not emitted by clang: mark a harness `[[gnu::used]]`.

## Interactions

- Slices, calls, loops, trusted functions and cost each have a page: [prove-slices](prove-slices.md), [prove-calls](prove-calls.md), [prove-loops](prove-loops.md), [prove-trusted](prove-trusted.md), [prove-cost](prove-cost.md).
- A proof removes checks from optimized builds ([proofs-remove-checks](proofs-remove-checks.md)); `smith build` never runs the prover.

## Replaces from C

Tests of the same property on a few inputs.
