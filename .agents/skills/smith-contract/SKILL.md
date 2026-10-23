---
name: smith-contract
description: "Writing contracts with smith::contract (import smith::contract; contract::requires, ensures, invariant, assert, trusted): checks written as calls that run at run time and that smith prove proves. Load when code imports smith::contract, on a question about preconditions, postconditions or loop invariants."
---

# Contracts: smith's assertions about a function

A contract is a call that stops the program when its condition is false, as `assert` does. The header is `.smith/include/smith/contract.h`; read it first.

**contract is the module `smith::contract`** (`lib/smith/contract.smith`), and a `.smith` file writes `import smith::contract;` and `contract::requires(c)`, `contract::ensures(c)`, `contract::invariant(c)`, `contract::assert(c)` and `contract::trusted()`. C includes `<smith/contract.h>` and calls `smith_requires`, `smith_ensures`, `smith_invariant`, `smith_assert` and `smith_trusted`; the prose below names the calls by their C names. The assert is `contract::assert`, not `assert`: C's `assert` is a macro, and a `.smith` file has no macro (`smith lint` refuses `c::assert` and tells you to write this). A contract is a plain function call, never a macro, so a failure's report is read from the call stack (`smith-trace`).

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
| `smith_invariant(c)` | the first statement of a loop body | `c` is true on every pass |
| `smith_assert(c)` | anywhere in a body | `c` is true here: the statement `assert` was, as a function. `smith prove` proves it where the function is part of a proof (in a function with a contract, or inlined into one), and a program stops on it in the debug and release profiles; the fast profile compiles it away |
| `smith_trusted()` | the first statement of a function | `smith prove` does not prove this function: callers prove its `smith_requires` and assume its `smith_ensures`, and its body is not read (see "Trusted boundaries"); it checks nothing at run time |

- An old value is a `const` local saved at the top: `const u8 old = moves->len;`. The result is the variable about to be returned.
- A spec is ordinary C: a pure helper function, called inside a contract, with loops of constant bounds.
- **A condition must be free of side effects.** The call evaluates it, and when assertions are off (`target::assertions`, which `-DNDEBUG` turns off: the fast profile) nothing else happens: the body is empty, so the optimizer removes an argument that has none.

## At run time

- A failed contract is `smith_failure(kind)` (`<smith/trace.h>`): it writes `smith-trace begin requires failed` and the stack to standard error and calls `abort()`, at every optimization level; link the trace library (`smith_library(b, "trace")`; `smith_tests` does it itself). In a test it prints the report of a failed check (the failing line and the call chain), and `smith symbolize` turns a saved log into it. Where and what come from the stack and the source line.
- **The static analyzer takes a contract as an assumption.** clang-tidy's `clang-analyzer-*` (`smith lint`) defines `__clang_analyzer__` (`target::static_analyzer`), and there each of the four is `__builtin_assume(condition)`, in every build, `NDEBUG` or not; so `smith_requires(index < N)` stops `clang-analyzer-security.ArrayBound` from reporting that index, as `assert` does, and an index nothing bounds is still reported. A compile, `smith prove` and the checks never see this branch.
- The four are `static inline` functions marked `always_inline` that the header carries (the module's `export inline`, whose `if constexpr` on `target::` is written to the header as the `#if` a C includer's compiler answers), with an empty body when assertions are off. Otherwise each is one call of an external function of the library, `smith_contract_requires`, `smith_contract_ensures`, `smith_contract_invariant` or `smith_contract_assert`, which the optimizer cannot see into. That call stays in the IR at `-O0` and optimized, and it is how `smith prove` finds the contracts without a flag.
- **The library is compiled from source with the project's flags**, so `target::assertions` there is the project's: in the fast profile the library's functions are empty, as the header's inline ones are.

Also in this family: [smith-contract-c](../smith-contract-c/SKILL.md), [smith-prove](../smith-prove/SKILL.md), [smith-prove-trusted](../smith-prove-trusted/SKILL.md), [smith-prove-cost](../smith-prove-cost/SKILL.md).
