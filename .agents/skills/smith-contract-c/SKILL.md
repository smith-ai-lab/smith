---
name: smith-contract-c
description: "Contracts from C (#include <smith/contract.h>: smith_requires, smith_ensures, smith_invariant, smith_assert, smith_trusted, smith_modifies) and compiling the library in build.c. Load when C code includes <smith/contract.h> or a build.c calls smith_library(b, "contract")."
---

# Contracts from C

```c
#include <smith/contract.h>

int32_t clamp(int32_t x, int32_t low, int32_t high) {
    smith_requires(low <= high);
    int32_t result = x < low ? low : x > high ? high : x;
    smith_ensures(low <= result && result <= high);
    return result;
}

for (uint8_t square = 0; square < squares; square++) {
    smith_invariant(moves->len <= 2 * square);
    // ...
}
```

`smith_modifies(p, size)` says what a function writes (C has no slices and no generics): `smith_modifies(&moves->jumps, sizeof moves->jumps);` at the start, after the `smith_requires`, one call for each range. `smith_modifies(nullptr, 0)` alone means the function writes nothing outside its locals. A pointer and a count is `smith_modifies(xs, n * sizeof *xs)`; the extent of `xs` must be known to the prover (`int32_t xs[static 4]`, a field). `smith prove` refutes a write outside the ranges; callers keep the rest (smith-contract, "What a function writes").

## Get the library in build.c

```c
SmithObject contract = {
    .compiler = "smith-cc",
    .source = smith_library(b, "contract"),
    .output = "build/smith/contract.o",
    .flags = smith_cat(b, flags, smith_list(b, "-I", ".smith/include", nullptr), nullptr),
};
smith_object(b, &contract);
```

Add `-I .smith/include` to the project's own compiles and put `contract` in the program's `.objects`. With `-DNDEBUG` no object references the library, so linking it is harmless. The tests need none of this: `smith_tests` compiles and links the contract library into the runner itself (unless `.objects` already has yours), so a test of code that makes contracts links with nothing added. Only the program's own link needs it.

Also in this family: [smith-contract](../smith-contract/SKILL.md), [smith-prove](../smith-prove/SKILL.md), [smith-prove-trusted](../smith-prove-trusted/SKILL.md), [smith-prove-cost](../smith-prove-cost/SKILL.md).
