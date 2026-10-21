---
name: smith-contract
description: "Using <smith/contract.h>, smith's contracts: SMITH_REQUIRES, SMITH_ENSURES and SMITH_INVARIANT, checks written like assert. Load when code includes <smith/contract.h>, when a build.c calls smith_library(b, \"contract\"), or on a question about preconditions, postconditions or loop invariants in C."
---

# Contracts: smith's assertions about a function

This skill ships in the smith plugin, from `.agents/skills/smith-contract/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

A contract is written like an `assert`, and each one is a check at run time. The header is `.smith/include/smith/contract.h`; read it first.

```c
#include <smith/contract.h>

int32_t clamp(int32_t x, int32_t low, int32_t high) {
    SMITH_REQUIRES(low <= high);                     // the caller's job
    int32_t result = x < low ? low : x > high ? high : x;
    SMITH_ENSURES(low <= result && result <= high);  // this function's promise
    return result;
}

for (uint8_t square = 0; square < squares; square++) {
    SMITH_INVARIANT(moves->len <= 2 * square);       // the first statement of a loop body
    // ...
}
```

| Macro | Where | Meaning |
|---|---|---|
| `SMITH_REQUIRES(c)` | the start of a function | a caller must make `c` true |
| `SMITH_ENSURES(c)` | just before a `return` (each return, or one exit) | the function makes `c` true |
| `SMITH_INVARIANT(c)` | the first statement of a loop body | `c` is true on every pass |
| `SMITH_CONTRACT_FOR(Type, spec)` | file scope | records that the function pointer type `Type` has the contract `spec`, a function whose only statements are contracts; it only records the pair |

- An old value is a `const` local saved at the top: `const uint8_t old = moves->len;`. The result is the variable about to be returned.
- A spec is ordinary C: a pure helper function, called inside a contract, that `SMITH_ENSURES` can use.

## At run time

- A failed contract prints `<file>:<line>: SMITH_REQUIRES(low <= high) failed` to stderr and calls `abort()`, as `assert` does, at every optimization level.
- With `-DNDEBUG` the macros compile away, as `assert` does: the expression is not evaluated, but it is still type-checked and its variables are still used, so an old value kept only for a contract draws no unused-variable warning.
- Each macro calls a function of the library, `smith_contract_requires`, `smith_contract_ensures` or `smith_contract_invariant`, with the condition, its text, the file and the line. The call stays in the IR at `-O0` and optimized, which is how a checker finds the contracts without a flag.

## Get the library in build.c

```c
SmithObject contract = {
    .compiler = "smith",
    .source = smith_library(b, "contract"),
    .output = "build/smith/contract.o",
    .flags = smith_cat(b, flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr),
};
smith_object(b, &contract);
```

Add `-I .smith/include` to the project's own compiles and put `contract` in the program's `.objects`. With `-DNDEBUG` no object references the library, so linking it is harmless.
