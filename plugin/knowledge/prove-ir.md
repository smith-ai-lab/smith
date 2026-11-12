---
type: concept
title: The IR smith prove checks
description: smith prove compiles each file to LLVM IR with undefined-behaviour checks; --emit-ir writes the IR it proves, with each contract and check as a numbered point, to explain a message that points at a line you cannot explain.
tags: [prove, ir]
---
# The IR smith prove checks

Each file is compiled to LLVM IR in smith's own clang: C23, `-O0`, debug info, UBSan's checks for undefined behaviour in trap mode (signed overflow, shifts, division by zero, array bounds with trailing arrays counted, float casts, `__builtin_unreachable`) and `-UNDEBUG`. The files are linked into one program. The flags after `--` are the project's include paths and defines.

## Example

```text
$ smith prove --emit-ir=out.ll
$ head out.ll
; point 0: smith_requires of a callee, proved at the call: count <= 5  (primes.smith:10)
; point 1: smith_requires, assumed: total <= 28  (primes.smith:16)
; point 2: assert, proved: sum <= 28  (main.smith:12)
...
  call void @smith_point(i32 0, i1 true), !dbg !460
```

## Rules

- **`--emit-ir=FILE`** writes, for each function it proves, the IR the prover checks: callees replaced by their contracts, helpers inlined, loops unrolled, each contract and each undefined behaviour a call `smith_point(id, condition)`, the points listed in a comment above the function. It is written for a function that cannot be proved too, as far as the prover got.
- **`cannot use the contract of X: its smith_requires reads memory ...`** names the read and the write with their lines and instructions, as the prover sees them. A store into a local struct or a `memset` of a local is not a write of the caller's memory. Put every `requires` before the first write and every `ensures` after the last.
- **A local nothing has written holds one value, any one**, never an `undef` the proof may choose: `if c: x = 7` then `ensures(x == 7)` fails with `x = 0` (for a local of up to 256 scalars; a bigger local is not covered).

## Interactions

- Contracts are the points ([contracts](contracts.md)); a message that names a callee's memory is explained in [prove-calls](prove-calls.md).

## Replaces from C

Reading the assembly to see what a check was.
