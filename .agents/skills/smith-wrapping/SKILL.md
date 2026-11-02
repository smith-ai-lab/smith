---
name: smith-wrapping
description: "Using std::wrapping (import std::wrapping; wrapping::add, sub, mul, neg, shl, to): signed arithmetic that wraps in two's complement, by name. Load when code imports std::wrapping, or on a hash, a counter or a generator that must wrap on a signed type."
---

# Wrapping arithmetic: smith's wrapping

This skill ships in the smith plugin, from `.agents/skills/smith-wrapping/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there.

`std::wrapping` (`lib/std/wrapping.smith`) holds `add`, `sub`, `mul`, `neg`, `shl` and `to` over any integer type. Each returns the type of its first argument with the result cut to that width in two's complement, so `wrapping::add((I8)127, (I8)1)` is `-128`. In C a signed overflow is undefined; unsigned arithmetic already wraps and needs no call.

```smith
module counter;

import std::wrapping;

// The next tick, wrapping from the largest back to the smallest.
export I32 next(I32 tick) {
    return wrapping::add(tick, 1);
}
```

- Both arguments have the first's type: write the cast, `wrapping::mul((I8)100, (I8)2)` is `-56`.
- `wrapping::shl(a, n)` keeps the low bits of `a << n` (`n` is a `U32`; at or past the width it gives 0). `wrapping::to(T, x)` keeps the low bits of `x` as a `T`: `wrapping::to(U8, 300)` is `44`, `wrapping::to(I8, 200)` is `-56`.
- Not built yet: `std::checked` (an `option` of the sum, `.none` on overflow).

Also in this family: [smith-math](../smith-math/SKILL.md).
