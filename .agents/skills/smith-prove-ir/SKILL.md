---
name: smith-prove-ir
description: "The IR smith prove checks and why a message points where it does: --emit-ir=FILE (smith_point lines), cannot use the contract of X (smith_requires reads memory), locals nothing has written, and the soundness tests (tests/prove_sound.sh). Load when a proof message points at a line you cannot explain."
---

# The IR the prover checks

```text
$ smith prove --emit-ir=out.ll
$ head out.ll
; point 0: smith_requires of a callee, proved at the call: count <= 5  (primes.smith:10)
; point 1: smith_requires, assumed: total <= 28  (primes.smith:16)
; point 2: assert, proved: sum <= 28  (main.smith:12)
...
  call void @smith_point(I32 0, i1 true), !dbg !460
```

- Each file is compiled to LLVM IR in smith's own clang: C23, `-O0`, debug info, UBSan's checks for undefined behaviour in trap mode (signed overflow, shifts, division by zero, array bounds with trailing arrays counted, `float` casts, `__builtin_unreachable`), `-UNDEBUG`. The files are linked into one program. The flags after `--` are the project's include paths and defines.
- `cannot use the contract of X: its smith_requires reads memory ...` names the read and the write with their lines and instructions: `its smith_requires reads memory at f.c:15: \`%2 = load I32, ptr %b2\` after the function has written, at f.c:14: \`store I32 %1, ptr %a1\``. Read the instruction as the prover sees it; a store into a local struct or a `memset` of a local (a zero-initialization) is not a write of the caller's memory and does not count. Put every `smith_requires` before the first write, and every `smith_ensures` after the last.
- **`smith prove --emit-ir=FILE`** writes, for each function it proves, the IR the prover checks: callees replaced by their contracts, helpers inlined, loops unrolled, each contract and each undefined behaviour a call `smith_point(id, condition)`, with the points listed in a comment above the function (`; point 3: smith_ensures, proved: total <= 28  (primes.smith:21)`). It is written for a function that cannot be proved too, as far as the prover got. Read it when a message points at a line you cannot explain.
- **A local nothing has written holds one value, any one**, never `undef` that the proof may choose: `int x; if (c) x = 7; ensures(x == 7)` fails with `x = 0` (a scalar of a local of up to 256 scalars; a bigger local is not covered).
- **Soundness is tested by running** (`tests/prove_sound_test.smith`, in `smith check`; `smith build prove-sound-full` runs 8 seeds of 50000 inputs): `tests/prove/sound/<case>.c` calls each proved function on random inputs that satisfy its `requires`, aliased pointers and boundary values included, under ASan and UBSan, so a failed `ensures` or a sanitizer report is a wrong "proved"; `tests/prove/unsound/*.c` are false claims that must fail, each with the run that refutes it (`// unsound: <function> <text>`). A bug found adds a case there.

See `smith-prove`.
