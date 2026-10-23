---
name: smith-random
description: "Random numbers from keys with smith::random (random::Key, split, fold_in): the same key gives the same numbers on every build and thread. Load when code includes <smith/random.h> or imports smith::random, or on seeds, reproducible randomness or property tests."
---

# Random numbers from keys: smith's random

A key is an immutable value, and every draw is a pure function of a key, as in JAX. The same key gives the same numbers on every platform, in any thread and in any order. There is no global state and no generator that advances, so there is no `next()`: a test that fails once fails every time. The header is `.smith/include/smith/random.h`; read it first.

## Keys

The module is `smith::random` (`lib/smith/random.smith`); a `.smith` file imports it. Names are the module's, short: `random::Key`, `random::key_make`, `random::normal_float_sample`.

```smith
import smith::random;

f64 demo(u64 seed) {
    random::Key key = random::key_make(seed);           // a seed is any u64
    random::Key world = random::key_derive(key, 0);     // JAX's fold_in: the key for part or item 0
    random::Key noise = random::key_derive(key, 1);
    u64 bits = random::key_bits(noise, 0);              // the key's n-th 64 random bits
    f32 u = random::key_unit_float(noise, 1);           // [0, 1), a grid of 2^-24
    f64 v = random::key_unit_double(world, 0);          // [0, 1), a grid of 2^-53
    return v + (f64)u + (f64)(bits & 1);
}
```

**Never reuse a key.** Drawing twice from one key gives the same numbers twice, so the second use is not independent of the first. Derive one key per use, and one per item in a loop. Draws `n = 0, 1, 2...` of one key are a stream that one distribution may spend; a program gives each use its own derived key.

`1 - unit` is exact and lies in (0, 1], which is the safe way to take a log: `random::log_double(1.0 - random::key_unit_double(key, 0))`.

## The guarantees, and their limits

- **The numbers never change.** They are part of the API: the Philox4x32-10 generator, the key and counter mapping and every distribution's algorithm are pinned by tests, so a change to any of them is a breaking change.
- **The same bits on every platform.** The library has its own logarithm (`random::log_float`, `random::log_double` (C: `smith_log_float`, `smith_log_double`); fdlibm's algorithm, within 1 ulp), and uses only `math::sqrt` (`smith-math`), the machine's square root, which IEEE requires to be exactly rounded. `lib/smith/random.smith` (the module `smith::random`) declares the flag `-ffp-contract=off`, which `smith build` adds after the project's own flags whenever it compiles the library (the `smith_libraries` row in smith.c; `.smith/lib/smith/random.flags` holds it), and passes every product that feeds a sum through an empty `asm` barrier, because `-ffp-contract=fast` would fuse a multiply-add and change a bit when the library is compiled by hand; it also refuses `-ffast-math` and `-Ofast` (any flag that sets `__FINITE_MATH_ONLY__`) with a `static_assert`. Tests give the same digest at -O0 with sanitizers, -O2, `-DNDEBUG`, `-std=gnu23` and `gnu2y`, `-ffp-contract=fast`. GPUs, other compilers and other CPUs are not tested.
- `float` and `double` are asserted to be IEEE binary32 and binary64 when the library is compiled.

Also in this family: [smith-random-distributions](../smith-random-distributions/SKILL.md), [smith-random-own](../smith-random-own/SKILL.md), [smith-random-c](../smith-random-c/SKILL.md).
