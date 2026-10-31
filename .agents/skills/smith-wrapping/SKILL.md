---
name: smith-wrapping
description: "Using smith::wrapping (import smith::wrapping; wrapping::add, sub, mul, neg): signed arithmetic that wraps in two's complement, by name. Load when code imports smith::wrapping, or on a hash, a counter or a generator that must wrap on a signed type."
---

# Wrapping arithmetic: smith's wrapping

This skill ships in the smith plugin, from `.agents/skills/smith-wrapping/SKILL.md` in https://github.com/smith-ai-lab/smith. Change it there.

`smith::wrapping` (`lib/smith/wrapping.smith`) holds `add`, `sub`, `mul` and `neg` over any integer type. Each returns the type of its first argument with the result cut to that width in two's complement, so `wrapping::add((i8)127, (i8)1)` is `-128`. In C a signed overflow is undefined; unsigned arithmetic already wraps and needs no call.

```smith
module counter;

import smith::wrapping;

// The next tick, wrapping from the largest back to the smallest.
export i32 next(i32 tick) {
    return wrapping::add(tick, 1);
}
```

- Both arguments have the first's type: write the cast, `wrapping::mul((i8)100, (i8)2)` is `-56`.
- Not built yet: `shl` and `to`, and `smith::checked` (an `option` of the sum, `.none` on overflow).

Also in this family: [smith-math](../smith-math/SKILL.md).
