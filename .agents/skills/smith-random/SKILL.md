---
name: smith-random
description: "Using <smith/random.h>, smith's random numbers from keys. Load when code includes <smith/random.h>, when a build.c calls smith_library(b, \"random\"), or on a question about random numbers, seeds, reproducible randomness or property tests."
---

# Random numbers from keys: smith's random

This skill ships in the smith plugin, from `.agents/skills/smith-random/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

A key is an immutable value, and every draw is a pure function of a key, as in JAX. The same key gives the same numbers on every platform, in any thread and in any order. There is no global state and no generator that advances, so there is no `next()`: a test that fails once fails every time. The header is `.smith/include/smith/random.h`; read it first.

## Keys

```c
#include <smith/random.h>

SmithKey key = smith_key_make(seed);              // a seed is any uint64_t
SmithKey world = smith_key_derive(key, 0);        // JAX's fold_in: the key for part or item 0
SmithKey noise = smith_key_derive(key, 1);
uint64_t bits = smith_key_bits(key, n);           // the key's n-th 64 random bits
float u = smith_key_unit_float(key, n);           // [0, 1), a grid of 2^-24
double v = smith_key_unit_double(key, n);         // [0, 1), a grid of 2^-53
```

**Never reuse a key.** Drawing twice from one key gives the same numbers twice, so the second use is not independent of the first. Derive one key per use, and one per item in a loop. Draws `n = 0, 1, 2...` of one key are a stream that one distribution may spend; a program gives each use its own derived key.

`1 - unit` is exact and lies in (0, 1], which is the safe way to take a log: `smith_log_double(1.0 - smith_key_unit_double(key, 0))`.

## Distributions

Typed names, as `sqrtf` and `sqrt`: real distributions come in `float` and `double`, integers use `int64_t`. A distribution is a struct of parameters and `smith_<name>_sample(key, d)`:

```c
float a = smith_uniform_float_sample(key, (SmithUniformFloat){.low = 0, .high = 1});       // [low, high)
double b = smith_uniform_double_sample(key, (SmithUniformDouble){.low = -1, .high = 1});   // [low, high)
int64_t c = smith_uniform_int64_sample(key, (SmithUniformInt64){.low = 1, .high = 6});     // [low, high], unbiased
float d = smith_normal_float_sample(key, (SmithNormalFloat){.mean = 0, .deviation = 1});
double e = smith_normal_double_sample(key, (SmithNormalDouble){.mean = 0, .deviation = 1});
bool f = smith_bernoulli_sample(key, (SmithBernoulli){.probability = 0.3});

SmithCategorical weights = smith_categorical_make(&arena, (const double[]){1, 2, 3}, 3);    // built once, in the arena
size_t g = smith_categorical_sample(key, weights);                                          // an index below count

size_t order[52];
smith_permutation_sample(key, (SmithPermutation){.count = 52}, order);                      // 0..51, uniformly ordered
```

- Parameters are `assert`ed: `low < high` and finite, `deviation > 0`, a probability in [0, 1], categorical weights finite, not negative, with a positive sum. A real uniform never returns `high`.
- The integer uniform is Lemire's multiply-and-reject: every value exactly as likely, up to 64 attempts. Needing all 64 has a probability below 2^-64, and then the last candidate is returned.
- The normal is the inverse CDF (Wichura's AS241), computed in `float` for `float`. Its reach is about +-8.2 deviations for `double` and about +-5.3 for `float`: the grid never reaches 0 or 1, so a draw is finite, and the distribution is exactly symmetric. A `float` normal is a poor fit when the tails past 5.3 sigma matter; use `double`.
- A zero weight is never drawn. The categorical table lives in the arena, which must outlive it; a sample is an index from `derive(key, 0)` and a coin from `derive(key, 1)`.

## Fill an array: a loop

There is no `fill`. Filling is one loop that works for any distribution and parallelizes, since no draw depends on another:

```c
for (size_t i = 0; i < count; i++)
    out[i] = smith_normal_float_sample(smith_key_derive(key, i), (SmithNormalFloat){.mean = 0, .deviation = 1});
```

Run the iterations in any order, on any number of threads: the array is the same.

## Write your own distribution

A distribution is a function of a key built only from the public primitives, written exactly like smith's. An exponential distribution:

```c
double exponential_sample(SmithKey key, double rate) {
    return -smith_log_double(1.0 - smith_key_unit_double(key, 0)) / rate;
}
```

A mixture derives a key for each part, so the parts are independent:

```c
double mixture_sample(SmithKey key, double rate, double mean, double deviation, double weight) {
    if (smith_bernoulli_sample(smith_key_derive(key, 0), (SmithBernoulli){.probability = weight}))
        return exponential_sample(smith_key_derive(key, 1), rate);
    return smith_normal_double_sample(smith_key_derive(key, 2), (SmithNormalDouble){.mean = mean, .deviation = deviation});
}
```

## The guarantees, and their limits

- **The numbers never change.** They are part of the API: the Philox4x32-10 generator, the key and counter mapping and every distribution's algorithm are pinned by tests, so a change to any of them is a breaking change.
- **The same bits on every platform.** The library has its own logarithm (`smith_log_float`, `smith_log_double`; fdlibm's algorithm, within 1 ulp), and uses libm only for `sqrt` and `sqrtf`, which IEEE requires to be exactly rounded. `lib/random.c` sets `#pragma STDC FP_CONTRACT OFF` and passes every product that feeds a sum through an empty `asm` barrier, because `-ffp-contract=fast` ignores pragmas and would fuse a multiply-add and change a bit; it also refuses `-ffast-math` and `-Ofast` with an `#error`. Tests give the same digest at -O0 with sanitizers, -O2, `-DNDEBUG`, `-std=gnu23` and `gnu2y`, `-ffp-contract=fast`, and on x86_64 under Rosetta where Rosetta is installed (it is skipped otherwise). GPUs, other compilers and other CPUs are not tested.
- `float` and `double` are asserted to be IEEE binary32 and binary64 in the header.

## Get the library in build.c

random uses the arena (`smith_categorical_make`), so a project compiles both. smith writes the headers and the sources into `.smith/` on every `smith build`:

```c
SmithObject random = {.compiler = "smith", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
smith_object(b, &random);
SmithObject arena = {.compiler = "smith", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
smith_object(b, &arena);
```

`flags` includes `-I .smith/include`, and the program's `.objects` list both. Add `-lm` to the link on platforms where `sqrt` is not inlined (macOS does not need it). See the smith-build and smith-arena skills.
