---
name: smith-random
description: "Using <smith/random.h>, smith's random numbers from keys. Load when code includes <smith/random.h>, when a build.c calls smith_library(b, \"random\"), or on a question about random numbers, seeds, reproducible randomness or property tests."
---

# Random numbers from keys: smith's random

This skill ships in the smith plugin, from `.agents/skills/smith-random/SKILL.md` in https://github.com/adam-r-kowalski/smith. Change it there.

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

## Distributions

Typed names, as `sqrtf` and `sqrt`: real distributions come in `f32` and `f64`, integers use `i64`. A distribution is a struct of parameters and `random::<name>_sample(key, d)`:

```smith
import smith::arena;
import smith::random;

void distributions(random::Key key, arena::Arena *a) {
    f32 x = random::uniform_float_sample(key, (random::UniformFloat){.low = 0, .high = 1});        // [low, high)
    f64 y = random::uniform_double_sample(key, (random::UniformDouble){.low = -1, .high = 1});     // [low, high)
    i64 die = random::uniform_int64_sample(key, (random::UniformInt64){.low = 1, .high = 6});      // [low, high], unbiased
    f32 gauss = random::normal_float_sample(key, (random::NormalFloat){.mean = 0, .deviation = 1});
    f64 gauss64 = random::normal_double_sample(key, (random::NormalDouble){.mean = 0, .deviation = 1});
    bool coin = random::bernoulli_sample(key, (random::Bernoulli){.probability = 0.3});

    f64 weights[] = {1, 2, 3};
    random::Categorical table = random::categorical_make(a, weights, _Countof(weights)); // built once, in the arena
    usize pick = random::categorical_sample(key, table);                                  // an index below count

    usize *order = arena::push_array(a, usize, 52);
    random::permutation_sample(key, (random::Permutation){.count = 52}, order);          // 0..51, uniformly ordered
    (void)x, (void)y, (void)die, (void)gauss, (void)gauss64, (void)coin, (void)pick;
}
```

- Parameters are `assert`ed: `low < high` and finite, `deviation > 0`, a probability in [0, 1], categorical weights finite, not negative, with a positive sum. A real uniform never returns `high`.
- The integer uniform is Lemire's multiply-and-reject: every value exactly as likely, up to 64 attempts. Needing all 64 has a probability below 2^-64, and then the last candidate is returned.
- The normal is the inverse CDF (Wichura's AS241), computed in `float` for `float`. Its reach is about +-8.2 deviations for `double` and about +-5.3 for `float`: the grid never reaches 0 or 1, so a draw is finite, and the distribution is exactly symmetric. A `float` normal is a poor fit when the tails past 5.3 sigma matter; use `double`.
- The zero word gives the simplest value, and values move away from it as the word grows (this is for the test runner, which shrinks a failing test by making its words smaller; it changes no distribution, only which word gives which value). The integer uniform gives 0 clamped into the range, then 0 - 1, 0 + 1, 0 - 2... (zigzag), and the real uniforms do the same on their grid; the normals give the mean to within one grid step; the Bernoulli gives `false`; the categorical gives column 0; the permutation gives the identity. Draws are pure functions of (key, n), so shrinking one word never shifts another: item i's key is `random::key_derive(key, i)` however many items came before.
- `random::Tap` is for smith's test runner and nothing else: a key the runner made carries a `tap` (a field of `random::Key`, `nullptr` for every key a program makes), and `random::key_bits` passes each word through the tap, so the runner can record a failing test's words and replace them with smaller ones; `key_derive` copies the tap to the keys it derives. There is no global to set, and a program's own keys stay pure functions of their bits.
- A zero weight is never drawn. The categorical table lives in the arena, which must outlive it; a sample is an index from `derive(key, 0)` and a coin from `derive(key, 1)`.

## Fill an array: a loop

There is no `fill`. Filling is one loop that works for any distribution and parallelizes, since no draw depends on another:

```smith
import smith::random;

void fill(random::Key key, f32 *out, usize count) {
    for (usize i = 0; i < count; i++)
        out[i] = random::normal_float_sample(random::key_derive(key, i), (random::NormalFloat){.mean = 0, .deviation = 1});
}
```

Run the iterations in any order, on any number of threads: the array is the same.

## Write your own distribution

A distribution is a function of a key built only from the public primitives, written exactly like smith's. **Map the zero word to the simplest value, and let values move away from it as the word grows**, so a failing test shrinks to it: smith's test runner records the words a test drew and makes them smaller as unsigned integers. `random::key_unit_double(key, 0)` is 0.0 for the word 0, so an exponential drawn as below gives 0 for it, the simplest value. A mixture derives a key for each part, so the parts are independent:

```smith
import smith::random;

f64 exponential_sample(random::Key key, f64 rate) {
    return -random::log_double(1.0 - random::key_unit_double(key, 0)) / rate;
}

f64 mixture_sample(random::Key key, f64 rate, f64 mean, f64 deviation, f64 weight) {
    if (random::bernoulli_sample(random::key_derive(key, 0), (random::Bernoulli){.probability = weight}))
        return exponential_sample(random::key_derive(key, 1), rate);
    return random::normal_double_sample(random::key_derive(key, 2), (random::NormalDouble){.mean = mean, .deviation = deviation});
}
```

## From C

A C program includes the generated header `.smith/include/smith/random.h`, and the C API (names, types, symbols) is the one it always had: types have `Smith` in front (`SmithKey`, `SmithUniformDouble`), functions are `smith_<name>`, real distributions are `float` and `double`, integers `int64_t`, and the runner's tap is `SmithRandomTap` (a field of `SmithKey`, `nullptr` in a key you make).

```c
#include <smith/random.h>

SmithKey key = smith_key_make(seed);              // a seed is any uint64_t
SmithKey world = smith_key_derive(key, 0);        // JAX's fold_in
uint64_t bits = smith_key_bits(key, n);
float u = smith_key_unit_float(key, n);           // [0, 1), a grid of 2^-24
double v = smith_key_unit_double(key, n);         // [0, 1), a grid of 2^-53

float a = smith_uniform_float_sample(key, (SmithUniformFloat){.low = 0, .high = 1});
int64_t c = smith_uniform_int64_sample(key, (SmithUniformInt64){.low = 1, .high = 6});
float d = smith_normal_float_sample(key, (SmithNormalFloat){.mean = 0, .deviation = 1});
bool f = smith_bernoulli_sample(key, (SmithBernoulli){.probability = 0.3});
SmithCategorical weights = smith_categorical_make(&arena, (const double[]){1, 2, 3}, 3);
size_t g = smith_categorical_sample(key, weights);
size_t order[52];
smith_permutation_sample(key, (SmithPermutation){.count = 52}, order);
```

```c
double exponential_sample(SmithKey key, double rate) {
    return -smith_log_double(1.0 - smith_key_unit_double(key, 0)) / rate;
}
```

## The guarantees, and their limits

- **The numbers never change.** They are part of the API: the Philox4x32-10 generator, the key and counter mapping and every distribution's algorithm are pinned by tests, so a change to any of them is a breaking change.
- **The same bits on every platform.** The library has its own logarithm (`random::log_float`, `random::log_double` (C: `smith_log_float`, `smith_log_double`); fdlibm's algorithm, within 1 ulp), and uses only `math::sqrt` (`smith-math`), the machine's square root, which IEEE requires to be exactly rounded. `lib/smith/random.smith` (the module `smith::random`) declares the flag `-ffp-contract=off`, which `smith build` adds after the project's own flags whenever it compiles the library (the `smith_libraries` row in smith.c; `.smith/lib/smith/random.flags` holds it), and passes every product that feeds a sum through an empty `asm` barrier, because `-ffp-contract=fast` would fuse a multiply-add and change a bit when the library is compiled by hand; it also refuses `-ffast-math` and `-Ofast` (any flag that sets `__FINITE_MATH_ONLY__`) with a `static_assert`. Tests give the same digest at -O0 with sanitizers, -O2, `-DNDEBUG`, `-std=gnu23` and `gnu2y`, `-ffp-contract=fast`. GPUs, other compilers and other CPUs are not tested.
- `float` and `double` are asserted to be IEEE binary32 and binary64 when the library is compiled.

## Get the library in build.c

random uses the arena (`smith_categorical_make`), so a project compiles both. smith writes the headers and the sources into `.smith/` on every `smith build`. random is written in `.smith` (the module `smith::random`, source `.smith/lib/smith/random.smith`), so compiling it needs a smith with clang inside; C code that only includes `<smith/random.h>` works anywhere, and the C API (names, types, symbols) is the one it always had:

```c
SmithObject random = {.compiler = "smith-cc", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
smith_object(b, &random);
SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
smith_object(b, &arena);
```

`flags` includes `-I .smith/include`, and the program's `.objects` list both. Add `-lm` to the link on platforms where `sqrt` is not inlined (macOS does not need it). See the smith-build and smith-arena skills.
