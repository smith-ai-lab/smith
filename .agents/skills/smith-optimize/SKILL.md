---
name: smith-optimize
description: "Using std::optimize (import std::optimize; optimize::opaque): the optimization barrier, a value the optimizer cannot see through. Load when code imports std::optimize, when a benchmark's work is optimized away, or when constant-time code must not be folded; it replaces extended asm in a .smith file."
---

# The optimization barrier: smith's optimize

This skill ships in the smith plugin, from `.agents/skills/smith-optimize/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there.

`optimize::opaque(x)` returns `x` unchanged. The optimizer must treat the result as an unknown value and the argument as used: it cannot fold the value into a constant, and it cannot delete the computation behind it. It works on integers, floats and pointers, and it costs no instruction beyond keeping the value in a register. A `.smith` file may not write extended `__asm__` (the error names `optimize::opaque(x)`), so this is the barrier.

A benchmark that keeps the measured work:

```smith
module bench;

import std::optimize;

export F64 measure(F64 a, F64 b) {
    F64 product = optimize::opaque(a * b); // the product is computed, and not fused into the next add
    return product + 1.0;
}
```

Constant-time code, where the compiler must not turn a branchless select back into a branch on a value it can see through:

```smith
module ct;

import std::optimize;

// a when pick is 1, b when pick is 0, with no branch on pick.
export U32 select(U32 pick, U32 a, U32 b) {
    U32 mask = optimize::opaque((U32)0 - pick);
    return (a & mask) | (b & ~mask);
}
```

- `optimize::opaque_f64`, `opaque_f32`, `opaque_u32` and `opaque_pointer` are the typed forms. Use them in a module that is also lowered to plain C (`std::random` is): that lowering has no compiler to type an `auto` argument or answer `if constexpr (T == F64)`.
- It does not stop the optimizer from reordering other code around it, only from knowing the value.
- It is not a memory fence and says nothing about threads.
