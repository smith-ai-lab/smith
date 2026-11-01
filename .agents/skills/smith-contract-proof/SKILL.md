---
name: smith-contract-proof
description: "What std::contract's modifies and invariant mean to smith prove: contract::modifies (what a function writes, what a caller keeps), a loop's invariant unrolled or by induction, and a failed contract at compile time. Load when writing contract::modifies or contract::invariant, or on 'the function writes here, outside the ranges' or 'smith_invariant(c) may be false after a pass'."
---

# Contracts: modifies, invariants and compile time

```smith
module bag;

import std::contract;

export typedef struct {
    u32 len;
    u32 jumps;
} Moves;

export void add_jump(Moves *moves) {
    contract::requires(moves->jumps < 100);
    contract::modifies(&moves->jumps);           // writes only this; a caller keeps `len` with no old_len saved
    moves->jumps += 1;
    contract::ensures(moves->jumps <= 100);
}

export u32 use_it(Moves *moves) {
    contract::requires(moves->jumps < 100 && moves->len == 7);
    add_jump(moves);
    contract::assert(moves->len == 7);           // proved from `modifies`
    return moves->len;
}
```

Part of [smith-contract](../smith-contract/SKILL.md), which has the calls. Both functions prove in 0.1 s of CPU.

## What a function writes: `modifies`
- `contract::modifies(&moves->jumps);` (C: `smith_modifies(&moves->jumps, sizeof moves->jumps)`, one call for each range; `contract::modifies(&a, &b)` names several): the function writes only these and its locals, so a caller keeps every other byte it passed, with no `old_*` saves. It does nothing at run time. With no argument (C: `smith_modifies(nullptr, 0)`) the function writes nothing outside its locals. A function with none may write what its pointer parameters reach.
- **Each argument is `&x` (a pointer: one object, its size from the type) or a slice (a range: `xs`, or `p[a:b]` of a pointer or an array; its count times the element's size, none when empty)**; anything else is `static assertion failed: contract::modifies takes &x (a pointer to one object) or a slice xs, p[a:b] (a range)`. `contract::modifies(xs[0:2], &count)` names two. `p[a:b]` of a `T *p` (one object) needs the extent to be known, as a read does (smith-prove).
- `smith prove` refutes a write of the body outside the ranges at its line, and at a call makes only the ranges arbitrary before `ensures`. Put it before the first write.

## A loop's invariant: unrolled, or proved by induction
- **Unrolled** (up to 16 passes): its passes are copied and `c` is proved on each, so `c` need not be inductive.
- **By induction** (more passes, or no constant bound): `smith prove` proves `c` on entry; then, with what a pass changes made arbitrary (loop variables, every object the loop writes), assumes `c` and the test, runs one pass and proves `c` again. A loop costs one pass whatever its bound: `n <= 4096` proves in 0.01 s where unrolling runs out of its budget. `c` is read where the test is, so it holds when the loop exits too (`i <= n && total == 3 * i`) and must say all the proof needs of what a pass writes. A weak one fails `smith_invariant(c) may be false after a pass of the loop` with the pass's values and the names the body writes that `c` does not mention; one false on entry fails `when the loop is entered`. If induction fails and every loop has at most 128 passes, `smith prove` then tries unrolling, which needs no inductive invariant.
- `smith prove --induction`, `--unroll` and `--induction-from=N` (the 16) choose; `-v` names each loop's mode, and why a loop was not cut and is unrolled (a write the prover cannot trace to an object of known size, a call writing memory it cannot name, a pointer loop variable, an inner loop before the invariant).

## At compile time
A failed contract (or `test::check`) in a function clang evaluates where C requires a constant (`constexpr Table t = make_table(0);`, a `static_assert`) is a **compile error** at the outermost call: the first note is `requires failed in call to 'contract::requires(false)'` at the contract's line, then the compile-time calls, innermost first. A contract that holds is a no-op. The contract functions are annotated `[[clang::annotate("smith_compile_check")]]`; see `smith-lang-constexpr`.
