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

**The asm lives in C** (#2224, decided on #1406). The new front end rejects extended asm, so the four barriers are `smith_opaque_f64`, `_f32`, `_u32` and `_ptr` (and `_u64`, for the generic `opaque` on integers), declared in `include/smith/optimize_asm.h` and defined once, out of line, in `lib/optimize_asm.c`: the C library `optimize_asm` that smith embeds, and that `smith.c`, the build program and the units runner link. `lib/std/optimize.smith` calls them through `import <smith/optimize_asm.h>` and keeps its exports and `opaque`'s signature; both front ends read the header's declarations and share the one definition. Out of line, the barrier is a call unless LTO inlines it. The library lands first and `optimize.smith` switches to it after a release: `random`, `math` and `math_trig` are compiled into smith by the previous release (the lag rule), which does not know the new library.
