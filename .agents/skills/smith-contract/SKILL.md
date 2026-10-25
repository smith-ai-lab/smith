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
| `smith_invariant(c)` | the first statement of a loop body | `c` is true on every pass, and when the loop exits (smith-contract-proof) |
| `smith_assert(c)` | anywhere in a body | `c` is true here: the statement `assert` was, as a function. `smith prove` proves it, and a function with one is in the default set (`main` included); a program stops on it in the debug and release profiles; the fast profile compiles it away |
| `smith_trusted()` | the first statement of a function | `smith prove` does not prove this function: callers prove its `smith_requires` and assume its `smith_ensures`, and its body is not read (see "Trusted boundaries"); it checks nothing at run time |
| `smith_modifies(p, size)` (`contract::modifies(&x, xs, p[a:b])`) | the start of a function, after its `smith_requires` | the function writes nothing but these objects, ranges and its own locals; a caller keeps all it knew of the rest (smith-contract-proof) |

- An old value is a `const` local saved at the top: `const u8 old = moves->len;` (for what a function leaves alone, `modifies`, smith-contract-proof). The result is the variable about to be returned.
- A spec is ordinary C: a pure helper function, called inside a contract, with loops of constant bounds.
- **A condition must be free of side effects.** The call evaluates it, in every build: debug, release and fast check alike, `-DNDEBUG` or not. Only `smith prove` removes a check (a proved one costs nothing); to pay less at run time, prove more.

## At run time

- A failed contract is `smith_failure(kind)` (`<smith/trace.h>`): it writes `smith-trace begin requires failed` and the stack to standard error and calls `abort()`, at every optimization level; link the trace library (`smith_library(b, "trace")`; `smith_tests` does it itself). In a test it prints the report of a failed check (the failing line and the call chain), and `smith symbolize` turns a saved log into it. Where and what come from the stack and the source line.

Also in this family: [smith-contract-proof](../smith-contract-proof/SKILL.md) (`modifies`, a loop's invariant, compile time), [smith-contract-c](../smith-contract-c/SKILL.md), [smith-prove](../smith-prove/SKILL.md), [smith-prove-trusted](../smith-prove-trusted/SKILL.md), [smith-prove-cost](../smith-prove-cost/SKILL.md). `smith prove` and `smith check` say how many functions have a contract (`4 of 23 functions`): write the next one from `smith prove -v`.

Where an assert, a `requires`, an `ensures` and a trusted check each go: `smith-contract-roles`.
