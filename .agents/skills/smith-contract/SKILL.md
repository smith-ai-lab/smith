---
name: smith-contract
description: "Writing contracts with smith::contract (import smith::contract; contract::requires, ensures, invariant, assert, trusted): checks written as calls that run at run time and that smith prove proves. Load when code imports smith::contract, on a question about preconditions, postconditions or loop invariants."
---

# Contracts: smith's assertions about a function

A contract is a call that stops the program when its condition is false, as `assert` does. The header is `.smith/include/smith/contract.h`; read it first.

**contract is the module `smith::contract`** (`lib/smith/contract.smith`): a `.smith` file writes `import smith::contract;` and `contract::requires(c)`, `ensures`, `invariant`, `assert` and `trusted`. C includes `<smith/contract.h>` and calls `smith_requires`, `smith_ensures`, `smith_invariant`, `smith_assert` and `smith_trusted`; the prose below uses the C names. The assert is `contract::assert`, not `assert` (a macro; `smith lint` refuses `c::assert`). A contract is a plain function call, so a failure's report is read from the call stack (`smith-trace`).

```smith
module geometry::box;

import smith::contract;

export i32 clamp(i32 x, i32 low, i32 high) {
    contract::requires(low <= high);                     // the caller's job
    i32 result = x < low ? low : x > high ? high : x;
    contract::ensures(low <= result && result <= high);  // this function's promise
    return result;
}

export u32 count_pairs(u32 squares, u32 *moves) {
    u32 len = 0;
    for (u32 square = 0; square < squares; square++) {
        contract::invariant(len <= 2 * square);          // the first statement of a loop body
        moves[len] = square;
        len += square % 2;
    }
    return len;
}
```

| Call | Where | Meaning |
|---|---|---|
| `smith_requires(c)` | the start of a function | a caller must make `c` true |
| `smith_ensures(c)` | just before a `return` (one return, after one `smith_ensures` per promise) | the function makes `c` true |
| `smith_invariant(c)` | the first statement of a loop body | `c` is true on every pass, and when the loop exits (see "A loop's invariant") |
| `smith_assert(c)` | anywhere in a body | `c` is true here: the statement `assert` was, as a function. `smith prove` proves it, and a function with one is in the default set (`main` included); a program stops on it in the debug and release profiles; the fast profile compiles it away |
| `smith_trusted()` | the first statement of a function | `smith prove` does not prove this function: callers prove its `smith_requires` and assume its `smith_ensures`, and its body is not read (see "Trusted boundaries"); it checks nothing at run time |
| `smith_modifies(&x, size)` (`contract::modifies(&x, ...)`) | the start of a function, after its `smith_requires` | the function writes nothing but these objects and its own locals; a caller keeps all it knew of the rest (see "What a function writes") |

- An old value is a `const` local saved at the top: `const u8 old = moves->len;` (for what a function leaves alone, `modifies` below). The result is the variable about to be returned.
- A spec is ordinary C: a pure helper function, called inside a contract, with loops of constant bounds.
- **A condition must be free of side effects.** The call evaluates it, and when assertions are off (`target::assertions`, which `-DNDEBUG` turns off: the fast profile) nothing else happens: the body is empty.

## What a function writes: `modifies`
- `contract::modifies(&moves->jumps);` (C: `smith_modifies(&moves->jumps, sizeof moves->jumps)`, one call for each range; `contract::modifies(&a, &b)` names several): the function writes only these and its locals, so a caller keeps every other byte it passed, with no `old_*` saves. It does nothing at run time. With no argument (C: `smith_modifies(nullptr, 0)`) the function writes nothing outside its locals. A function with none may write what its pointer parameters reach.
- **Only `&x` (a pointer to one object) is taken today**; anything else is `static assertion failed: contract::modifies takes &x`. A slice range (`xs`, `p[a:b]`) waits for #119, and an array-typed `&s->items` fails too: name the struct that holds it.
- `smith prove` refutes a write of the body outside the ranges at its line, and at a call makes only the ranges arbitrary before `ensures`. Put it before the first write.

## A loop's invariant: unrolled, or proved by induction
- **Unrolled** (up to 16 passes): its passes are copied and `c` is proved on each, so `c` need not be inductive.
- **By induction** (more passes, or no constant bound): `smith prove` proves `c` on entry; then, with what a pass changes made arbitrary (loop variables, every object the loop writes), assumes `c` and the test, runs one pass and proves `c` again. A loop costs one pass whatever its bound: `n <= 4096` proves in 0.01 s where unrolling runs out of its budget. `c` is read where the test is, so it holds when the loop exits too (`i <= n && total == 3 * i`) and must say all the proof needs of what a pass writes. A weak one fails `smith_invariant(c) may be false after a pass of the loop` with the pass's values and the names the body writes that `c` does not mention; one false on entry fails `when the loop is entered`. If induction fails and every loop has at most 128 passes, `smith prove` then tries unrolling, which needs no inductive invariant.
- `smith prove --induction`, `--unroll` and `--induction-from=N` (the 16) choose; `-v` names each loop's mode, and why a loop was not cut and is unrolled (a write the prover cannot trace to an object of known size, a call writing memory it cannot name, a pointer loop variable, an inner loop before the invariant).

## At run time

- A failed contract is `smith_failure(kind)` (`<smith/trace.h>`): it writes `smith-trace begin requires failed` and the stack to standard error and calls `abort()`, at every optimization level; link the trace library (`smith_library(b, "trace")`; `smith_tests` does it itself). In a test it prints the report of a failed check (the failing line and the call chain), and `smith symbolize` turns a saved log into it. Where and what come from the stack and the source line.
- **The static analyzer takes a contract as an assumption.** clang-tidy's `clang-analyzer-*` (`smith lint`) defines `__clang_analyzer__` (`target::static_analyzer`), and there each of the four is `__builtin_assume(condition)`, in every build, `NDEBUG` or not; so `smith_requires(index < N)` stops `clang-analyzer-security.ArrayBound` from reporting that index, as `assert` does, and an index nothing bounds is still reported. A compile, `smith prove` and the checks never see this branch.
- The four are `always_inline` functions the header carries (the module's `export inline`; its `if constexpr` on `target::` is written as the `#if` a C includer's compiler answers), empty when assertions are off. Otherwise each calls an external function of the library (`smith_contract_requires`, `_ensures`, `_invariant`, `_assert`) that the optimizer cannot see into; the call stays in the IR at `-O0` and optimized, and is how `smith prove` finds the contracts without a flag.
- **The library is compiled from source with the project's flags**, so `target::assertions` there is the project's: in the fast profile the library's functions are empty, as the header's inline ones are.

Also in this family: [smith-contract-c](../smith-contract-c/SKILL.md), [smith-prove](../smith-prove/SKILL.md), [smith-prove-trusted](../smith-prove-trusted/SKILL.md), [smith-prove-cost](../smith-prove-cost/SKILL.md).

## Where an assert goes (the roles stay distinct)

- **A precondition at entry is `requires`**, never an `assert`: the caller proves it, the body assumes it. A pointer parameter is already assumed valid and not null, so `assert(p != nullptr)` is deleted, not converted.
- **A promise about the result is `ensures`**, written before the return (it may name locals).
- **A fact in the middle that follows from what the function knows stays an `assert`**; a function with only asserts is proved, so it must be provable cheaply.
- **A check that cannot be proved cheaply stays an assert, in a trusted function**: a fact about data, about a libc result or deep in a loop over input goes in a small function that starts with `contract::trusted();` and holds `contract::assert(holds); if (!holds) __builtin_trap();` (the library's `check`). The prover never reads its body, so the check is a run-time one that writes the failure report (the call's line and the stack); the trap stops a build without assertions. A bare `__builtin_trap()` writes no report.
- **A function with a precondition that cannot be proved is `requires` plus `smith_trusted()`** (first statement), with the reason in a comment (smith-prove-trusted).

## At compile time
A failed contract (or `test::check`) in a function clang evaluates where C requires a constant (`constexpr Table t = make_table(0);`, a `static_assert`) is a **compile error** at the outermost call: the first note is `requires failed in call to 'contract::requires(false)'` at the contract's line, then the compile-time calls, innermost first. A contract that holds is a no-op. The contract functions are annotated `[[clang::annotate("smith_compile_check")]]`; see `smith-lang-constexpr`.
