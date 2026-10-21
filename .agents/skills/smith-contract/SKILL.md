---
name: smith-contract
description: "Using <smith/contract.h>, smith's contracts: smith_requires, smith_ensures and smith_invariant, checks written as calls, and smith prove, which proves them. Load when code includes <smith/contract.h>, when a build.c calls smith_library(b, \"contract\"), when running smith prove, or on a question about preconditions, postconditions or loop invariants in C."
---

# Contracts: smith's assertions about a function

This skill ships in the smith plugin, from `.agents/skills/smith-contract/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

A contract is a call that stops the program when its condition is false, as `assert` does. The header is `.smith/include/smith/contract.h`; read it first.

```c
#include <smith/contract.h>

int32_t clamp(int32_t x, int32_t low, int32_t high) {
    smith_requires(low <= high);                     // the caller's job
    int32_t result = x < low ? low : x > high ? high : x;
    smith_ensures(low <= result && result <= high);  // this function's promise
    return result;
}

for (uint8_t square = 0; square < squares; square++) {
    smith_invariant(moves->len <= 2 * square);       // the first statement of a loop body
    // ...
}
```

| Call | Where | Meaning |
|---|---|---|
| `smith_requires(c)` | the start of a function | a caller must make `c` true |
| `smith_ensures(c)` | just before a `return` (one return, after one `smith_ensures` per promise) | the function makes `c` true |
| `smith_invariant(c)` | the first statement of a loop body | `c` is true on every pass |
| `SMITH_CONTRACT_FOR(Type, spec)` | file scope | a macro, since it declares: records that the function pointer type `Type` has the contract `spec`, a function whose only statements are contracts; it only records the pair |

- An old value is a `const` local saved at the top: `const uint8_t old = moves->len;`. The result is the variable about to be returned.
- A spec is ordinary C: a pure helper function, called inside a contract, with loops of constant bounds.
- **A condition must be free of side effects.** The call evaluates it, and with `-DNDEBUG` nothing else happens: the body is empty, so the optimizer removes an argument that has none.

## At run time

- A failed contract prints which contract failed (`smith: smith_requires failed`) and calls `abort()`, at every optimization level. Where and what come from the stack trace and the source line.
- The three are `static inline` functions marked `always_inline`, with an empty body under `NDEBUG`. Otherwise each is one call of an external function of the library, `smith_contract_requires`, `smith_contract_ensures` or `smith_contract_invariant`, which the optimizer cannot see into. That call stays in the IR at `-O0` and optimized, and it is how `smith prove` finds the contracts without a flag.

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

## Prove them: `smith prove <files> [-- <compile flags>]`

Only a smith linked against the prover has it (`smith build llvm-prove`, `smith build prove-libs`, then `smith build -DLLVM=~/.cache/smith/llvm/23.1.3-prove/install`); any other says so and exits 2. It is recognised only as the exact first argument.

```sh
smith prove main.c util.c -- -I .smith/include -DDEBUG
smith prove --function=clamp --budget=5 -v clamp.c -- -I .smith/include
```

- Each file is compiled to LLVM IR in smith's own clang: C23, `-O0`, debug info, UBSan's checks for undefined behaviour in trap mode (signed overflow, shifts, division by zero, array bounds with trailing arrays counted, `float` casts, `__builtin_unreachable`), `-UNDEBUG`. The files are linked into one program. The flags after `--` are the project's include paths and defines.
- A function is proved when it carries a contract, or when no function of the program calls it and it calls a function with a `smith_ensures`. Each is proved in a process of its own, in at most 10 s (`--budget=`); one that runs out fails like a test that times out. `-v` lists the proved ones with their time.
- What is proved: every `smith_ensures` and `smith_invariant`, every `assert`, every UBSan check, and that nothing else is undefined (an invalid access, a poison value used). `smith_requires` of the function is assumed.
- **A call of a function with a `smith_ensures`** proves its `smith_requires` at the call and then assumes its `smith_ensures`; what the call may write comes from LLVM's inferred memory attributes, and a note says where that is not enough. **A call of any other function is inlined**, so its contracts and checks are proved where it is called. A pointer parameter is a valid object of its type (not null, all there, aligned for what the body does, writable if the body stores through it), and parameters of different struct types are different objects.
- **Loops are unrolled**, so a bound must be constant. `smith_invariant` in an unrolled loop is proved on each pass. Many loads in many passes are slow: a loop of 64 passes over a board takes seconds.
- A failure prints like a test failure: the contract and its line, the call it breaks, and a counterexample's values by their names in the source.
- **Errors, never assumptions**, each naming the place and what would make it provable: recursion, a call through a function pointer, inline assembly, a function with neither a body nor a contract, an Objective-C message send, a loop with no constant bound, and a function with several returns whose `smith_ensures` is not before one.
