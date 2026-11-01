---
name: smith-optimize
description: "Using std::optimize (import std::optimize; optimize::opaque): the optimization barrier, a value the optimizer cannot see through. Load when code imports std::optimize, on a benchmark whose work the optimizer deletes, constant-time code, or on 'extended asm is not part of Smith'."
---

# The optimization barrier: smith's optimize

This skill ships in the smith plugin, from `.agents/skills/smith-optimize/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there.

`std::optimize` (`lib/std/optimize.smith`) holds `opaque(x)`. It returns `x` unchanged over an integer, a float or a pointer, but the optimizer must treat the result as an unknown value and the computation behind it as used. It cannot fold the value into a constant and cannot delete the work that made it. It costs no instruction. Extended asm (`__asm__("" : "+r"(x))`) is not part of Smith; this is its spelling.

A benchmark that keeps the work it measures:

```smith
module bench;

import std::optimize;

export F64 measure(Usize n) {
    F64 total = 0;
    for (Usize i = 0; i < n; i++) {
        total += optimize::opaque((F64)i) * 0.5; // the input is unknown, so the loop is not folded away
    }
    return optimize::opaque(total); // the result is used, so the loop is not deleted
}
```

Constant-time code: the compiler may turn a mask into a branch once it sees the mask is 0 or all ones; hiding it keeps the arithmetic.

```smith
module choose;

import std::optimize;

export U32 pick(Bool take, U32 a, U32 b) {
    U32 mask = optimize::opaque((U32)0 - (U32)take); // not known to be 0 or ~0, so no branch
    return (a & mask) | (b & ~mask);
}
```

- It hides one value, not a side effect: a call whose result is unused is still removed unless its result goes through `opaque`.
- A hidden value cannot be proved about: `smith prove` takes `opaque(x)` as any value.
