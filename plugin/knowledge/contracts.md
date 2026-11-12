---
type: concept
title: Contracts
description: Calls of std.contract that stop the program when a condition is false and that smith prove proves; what requires, ensures, invariant, assert and trusted each mean and where each goes.
tags: [contracts, prove]
---
# Contracts

A contract is a call that stops the program when its condition is false, as `assert` does, and that `smith prove` proves. `import std.contract` gives `contract.requires`, `ensures`, `invariant`, `assert`, `trusted` and `modifies`. C includes `<smith/contract.h>` and calls `smith_requires`, `smith_ensures`, `smith_invariant`, `smith_assert`, `smith_trusted` and `smith_modifies`; `assert` itself is a macro and is not a contract.

## Example

```smith
import std.contract

pub fn clamp(x: i32, low: i32, high: i32) -> i32:
    contract.requires(low <= high)
    result := if x < low: low else if x > high: high else: x
    contract.ensures(low <= result and result <= high)
    result
```

## Rules

| Call | Where | Meaning |
|---|---|---|
| `requires(c)` | the start of a function | a caller must make `c` true; the body assumes it |
| `ensures(c)` | before the returned value, one call per promise | the function makes `c` true |
| `invariant(c)` | the first statement of a loop body | `c` holds on every pass and when the loop exits ([prove-loops](prove-loops.md)) |
| `assert(c)` | anywhere in a body | `c` holds here, from what is known at that point |
| `trusted()` | the first statement of a function | the prover does not read the body ([prove-trusted](prove-trusted.md)) |
| `modifies(...)` | after the `requires` | what the function writes ([contract-modifies](contract-modifies.md)) |

- **A precondition is `requires`, never `assert`**: a pointer parameter is already assumed valid and not null, so `assert(p != nullptr)` is deleted, not converted. A promise about the result is `ensures`; it may name locals. A fact in the middle that follows from what is known stays an `assert`.
- **A check that cannot be proved cheaply** (a fact about data, a libc result) goes in a small trusted function that holds `contract.assert(holds)` and a trap; the report then names the failing line and the call chain, which a bare trap does not.
- **An old value is a `const` local saved at the top**; the result is the variable about to be returned.
- **A condition has no side effects**: every build evaluates it, `NDEBUG` or not.
- **A lemma is a function** whose `requires` is the hypothesis and `ensures` the claim; calling it proves the `requires` at the call and then gives the `ensures` for those arguments ([prove-calls](prove-calls.md)).

## Interactions

- A failed contract writes `smith-trace begin requires failed` and the stack to standard error and aborts, at every optimization level; link the trace library (`smith_tests` does it itself). In a test it fails the test with the failing line and the call chain.
- Only `smith prove` removes a check: a proved `ensures`, `invariant` or `assert` is left out of an optimized build, a `requires` never ([proofs-remove-checks](proofs-remove-checks.md)).
- clang-tidy's analyzer takes a contract as an assumption (`__builtin_assume`), so `requires(index < 4)` stops an array-bound report for that index.
- A contract that fails where C needs a constant (`static_assert`, a `constexpr` initializer) is a compile error at the outermost call, with the compile-time calls as notes.
- C: build.c links the library with `smith_library(b, "contract")` and `-I .smith/include`; `smith_tests` links it itself.

## Replaces from C

`assert(c)` and comments that say what a function expects and promises.
