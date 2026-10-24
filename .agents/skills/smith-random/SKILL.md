---
name: smith-random
description: "Random numbers from keys with smith::random (random::Key, split): the same key gives the same numbers on every build and thread. Load when code includes <smith/random.h> or imports smith::random, or on seeds, reproducible randomness or property tests."
---

# Random numbers from keys: smith's random

```smith
import smith::random;

f64 demo(u64 seed, f64 out[:]) {
    random::Key key = random::key_make(seed);           // a seed is any u64
    random::Key parts[2];
    random::split(key, parts);                          // key is used here; parts are new, independent keys
    f64 total = random::uniform_double_sample(parts[0], (random::UniformDouble){.low = 0, .high = 1});
    random::Key per_item[4];
    random::split(parts[1], per_item);
    for (usize i = 0; i < 4; i++) total += random::key_unit_double(per_item[i]);   // the plain loop: one key per turn
    return total + (f64)countof(out);
}
```

A key is immutable and every draw is a pure function of a key, as in JAX: the same key gives the same numbers on every platform, in any thread. There is no generator that advances. **A key is used once**: `random::Key` is `[[smith::affine]]` (`smith-lang-affine`), so drawing twice from one is a compile error at the second use, naming the first:

```smith fragment
f64 a = random::normal_double_sample(parts[0], n);
f64 b = random::normal_double_sample(parts[0], n);   // error: smith-affine: 'parts[0]' is used again; it was already used at line N
```

`random::split(key, keys)` uses `key` and fills an array or a slice with `countof(keys)` new keys (C: `smith_key_split_n(key, keys, count)`, also `random::split_n(key, keys, count)` for a pointer and a count); a count's keys are the first of a larger count's. The header is `.smith/include/smith/random.h`; read it first.

A loop that needs a key per turn without an array reassigns it:

```smith fragment
for (usize i = 0; i < count; i++) {
    random::Key next[2];
    random::split(key, next);
    key = next[0];
    out[i] = random::uniform_double_sample(next[1], u);
}
```

`random::key_bits(key)` is 64 random bits, `random::key_unit_float(key)` a real in [0, 1) on a grid of 2^-24, `random::key_unit_double(key)` on 2^-53. A key is one draw. `1 - unit` is exact and in (0, 1], the safe way to take a log: `random::log_double(1.0 - random::key_unit_double(key))`.  The same numbers twice: make the key twice from the seed.

## The guarantees, and their limits

- **The numbers never change.** (`split` gives the keys `key_derive` gave and a draw is the old draw 0, so every distribution still gives what it gave for a key.) They are part of the API: the Philox4x32-10 generator, the key and counter mapping and every distribution's algorithm are pinned by tests, so a change to any of them is a breaking change.
- **The same bits on every platform.** The library has its own logarithm (`random::log_float`, `random::log_double`; fdlibm's algorithm, within 1 ulp), uses only `math::sqrt` (exactly rounded by IEEE), builds with `-ffp-contract=off` (which `smith build` adds for the library; `.smith/lib/smith/random.flags`) and refuses `-ffast-math`. Tests give the same digest across optimization levels, sanitizers and `-ffp-contract=fast`; GPUs and other compilers are not tested.
- `float` and `double` are asserted to be IEEE binary32 and binary64 when the library is compiled.

Also in this family: [smith-random-distributions](../smith-random-distributions/SKILL.md), [smith-random-own](../smith-random-own/SKILL.md), [smith-random-c](../smith-random-c/SKILL.md).
