---
name: smith-contract-roles
description: "Where each contract call goes: a precondition at entry is requires (never assert), a promise about the result is ensures, a fact in the middle is assert, a check that cannot be proved cheaply is a trusted function holding assert and a trap. Load when deciding between assert, requires and ensures, or when converting an assert() in ported C."
---

# Where an assert goes (the roles stay distinct)

```smith
module checks;

import smith::contract;

export i32 first(const i32 *xs, usize count) {
    contract::requires(count > 0);                // the precondition: callers prove it, the body assumes it
    i32 head = xs[0];
    contract::assert(head == xs[0]);              // a fact in the middle that follows from what is known
    contract::ensures(head == xs[0]);             // the promise about the result, before the return
    return head;
}

export void check_input(bool holds) {
    contract::trusted();                          // the prover never reads this body: a run-time check with a report
    contract::assert(holds);
    if (!holds) __builtin_trap();                 // a bare trap writes no report
}
```

- **A precondition at entry is `requires`**, never an `assert`: the caller proves it, the body assumes it. A pointer parameter is already assumed valid and not null, so `assert(p != nullptr)` is deleted, not converted.
- **A promise about the result is `ensures`**, written before the return (it may name locals).
- **A fact in the middle that follows from what the function knows stays an `assert`**; a function with only asserts is proved, so it must be provable cheaply.
- **A check that cannot be proved cheaply stays an assert, in a trusted function**: a fact about data, about a libc result or deep in a loop over input goes in a small function that starts with `contract::trusted();` and holds `contract::assert(holds); if (!holds) __builtin_trap();` (the library's `check`). The prover never reads its body, so the check is a run-time one that writes the failure report (the call's line and the stack); the trap after it is a second stop. A bare `__builtin_trap()` writes no report.
- **A function with a precondition that cannot be proved is `requires` plus `smith_trusted()`** (first statement), with the reason in a comment (smith-prove-trusted).

See `smith-contract`.
