---
name: smith-prove
description: "smith prove: proving the contracts of a program with Alive2, Bitwuzla and Z3, what is proved and assumed, the default set, and the verdict lines. Load when running smith prove, reading a proof result, or when a proof fails; the pieces are in smith-prove-project, -slices, -calls, -loops, -budget, -ir, -cost and -trusted."
---

# Prove them

```smith
module primes;

import smith::contract;

export i32 sum(usize count) {
    contract::requires(count <= 5);              // assumed here, proved at every call
    constexpr i32 first[_] = {2, 3, 5, 7, 11};
    i32 total = 0;
    for (usize i = 0; i < count; i++) {
        total += first[i];
    }
    contract::ensures(total <= 28);              // proved for every input that meets the requires
    return total;
}
```

```text
$ smith prove
smith prove: ok (0.3 s)
             3 of 5 functions have contracts; smith prove -v lists the 2 without
$ smith prove -v
ok    primes::sum  (primes.smith:9)  0.02 s wall, 0.01 s CPU, charged 0.01 s
  primes.smith:13: a loop unrolled into 5 passes
2 proved, 0 failed in 3.46 s
```

Every full smith has it (`smith build llvm` builds the prover with LLVM); only the stage0, built by `cc` alone, says it has none and exits 2. It is recognised only as the exact first argument.

- **The default set is every function with a contract** (`smith_requires`, `smith_ensures`, `smith_invariant` or `smith_assert`, C's `assert` too, of its own), **minus the trusted ones**. A function with only asserts (`main` too) is proved, its asserts from what is known at that point (a call of a function with a `smith_ensures` gives that ensures); callers still inline it, since it promises nothing. A function with none of these is not proved, and nothing says anything of them; a proved function that calls one reads its body, and one with a `smith_ensures` is used through its contract. To have a function proved, give it a contract (`smith_ensures(true)` is enough to ask for its undefined behaviour and asserts).
- What is proved: every `smith_ensures` and `smith_invariant`, every `smith_assert` and `assert`, every UBSan check, and that nothing else is undefined (an invalid access, a poison value used). `smith_requires` of the function is assumed.
- **`smith new`'s program is the example** (both languages): `primes::sum` (`primes_sum`) has `contract::requires(count <= 5)` (`smith_requires`), a loop, and `contract::ensures(total <= 28)` (`smith_ensures`), so `smith prove` there says `1 proved, 0 failed` in about 0.03 s. A precondition is `requires`, a promise is `ensures`, and neither is `assert`, which is a fact in the middle: `main`'s `contract::assert(sum <= 28)` (`assert` in C) is proved from `primes::sum`'s `ensures`, so `smith prove` there says `2 proved, 0 failed`.
- A failure prints like a test failure: the contract and its line, the call it breaks, and a counterexample's values by their names in the source.
- **Errors, never assumptions**, each naming the place and what would make it provable: recursion, a call through a function pointer, inline assembly, a function with neither a body nor a contract, an Objective-C message send, a loop with no constant bound, and a function with several returns whose `smith_ensures` is not before one. For the first four (and a libc call) the way out is a trusted boundary, below.

- **Proofs remove checks (issue #221).** A run writes, for each `.smith` source, the lines whose slice index and cut checks, and `contract::assert`, `ensures` and `invariant` calls (never `requires`), a proved function owns to `.smith/proved/<source>.pts` (first line `smith-proved <key>`, the key a SHA-256 of the source's text and the prover's version, then one line number each). The lowering of an optimized build (`smith build release`, `fast`; never the -O0 debug one) leaves those checks out when the key matches (a proved contract call is written as `contract::proved`, which does nothing; a debug build writes the check under a name that is no failure point, so a function whose only failure point it was has no `fails` in any build; `SMITH_PROVED_REPORT=1 smith cc -O2 -c x.smith` counts removed and kept); the file is an input of that compile (and `-DSMITH_PROVED=<hash>` is in its command), so a new proof rebuilds only the sources whose set changed. `smith build` never runs the prover. A check in a helper inlined into the proved function, in a generic's instance, in a function that failed or was not proved stays; `--function` writes nothing. The key also covers the bytes of every other file the proof read a function or global from (`dep <path>` lines: a callee whose `requires` and `ensures` it assumed, an inlined helper, a generic), and each is an input of the compile, so changing a callee's contract puts the caller's checks back at the next release build (the check is only gone while the proof still holds; re-run `smith prove` to remove it again). Only optimized builds remove: debug (-O0) keeps every check as a safety net against a prover bug, but reads the file too (it is an input of the debug compile), so the `fails` verdict is the same in every build. `smith prove -v` says how many sources it wrote.

Where to go: files and the project form `smith-prove-project`; slices and arrays `smith-prove-slices`; calls, `restrict`, `modifies` `smith-prove-calls`; loops `smith-prove-loops`; budget, memory, the cache `smith-prove-budget`; the IR and soundness tests `smith-prove-ir`; the cost report `smith-prove-cost`; the trusted boundary `smith-prove-trusted`; writing contracts `smith-contract`.
