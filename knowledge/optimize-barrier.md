---
type: architecture
title: std::optimize::opaque is the optimization barrier
description: A generic that returns its argument as a value the optimizer cannot see through; the one place the standard library writes extended asm (issue #1419, decision on #1375).
tags: [libraries, optimizer]
---
# std::optimize

Owner (2026-11-01, #1375): the barrier is `optimize::opaque(x)`. It returns `x` unchanged; the optimizer treats the value as unknown and as used, so it can neither fold it into a constant nor delete the computation behind it.

**Decisions.**
- *Generic over the type:* `auto x`, returning `typeof(x)`; integers and pointers use `"+r"`, floats `"+w"` on aarch64 and `"+X"` elsewhere, as `std::random` did.
- *Typed forms too:* a module also lowered to plain C (`std::random`, the C rule) cannot call a function with an `auto` parameter or answer a type comparison, so `opaque_f64`, `opaque_f32`, `opaque_u32` and `opaque_pointer` name their type and `std::random` calls them.
- *The one allowed asm:* the front end rejects extended asm in a `.smith` file (its error names `optimize::opaque(x)`); the body of `opaque` is the single exception, marked `contract::trusted` because a proof cannot read an empty asm.
- *Callers:* `std::random` (products kept from fusing, Philox constants and coefficients kept in registers) and the `if-constexpr/kind` corpus case.
