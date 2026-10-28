---
name: smith-random-distributions
description: "The distributions of std::random (uniform_float_sample, uniform_int64_sample, normal_*_sample, bernoulli_sample, permutation_sample, and std::categorical) and filling an array with a loop over keys. Load when drawing numbers of a given distribution."
---

# Distributions

Typed names, as `sqrtf` and `sqrt`: real distributions come in `F32` and `F64`, integers use `I64`. A distribution is a struct of parameters and `random::<name>_sample(key, d)`:

```smith
import std::arena;
import std::random;

void distributions(random::Key key, arena::Arena *a) {
    random::Key k[8];
    random::split(key, k);                                  // each draw below has its own key
    F32 x = random::uniform_float_sample(k[0], (random::UniformFloat){.low = 0, .high = 1});        // [low, high)
    F64 y = random::uniform_double_sample(k[1], (random::UniformDouble){.low = -1, .high = 1});     // [low, high)
    I64 die = random::uniform_int64_sample(k[2], (random::UniformInt64){.low = 1, .high = 6});      // [low, high], unbiased
    F32 gauss = random::normal_float_sample(k[3], (random::NormalFloat){.mean = 0, .deviation = 1});
    F64 gauss64 = random::normal_double_sample(k[4], (random::NormalDouble){.mean = 0, .deviation = 1});
    Bool coin = random::bernoulli_sample(k[5], (random::Bernoulli){.probability = 0.3});

    Usize pick = 0;                                         // a weighted index: std::categorical (smith-categorical)

    Usize *order = arena::push_slice(a, Usize, 52).data;
    random::permutation_sample(k[7], (random::Permutation){.count = 52}, order);         // 0..51, uniformly ordered
    (void)x, (void)y, (void)die, (void)gauss, (void)gauss64, (void)coin, (void)pick;
}
```

- Parameters are `assert`ed: `low < high` and finite, `deviation > 0`, a probability in [0, 1]. A real uniform never returns `high`.
- The integer uniform is Lemire's multiply-and-reject: every value exactly as likely, up to 64 attempts. Needing all 64 has a probability below 2^-64, and then the last candidate is returned.
- The normal is the inverse CDF (Wichura's AS241), computed in `float` for `float`. Its reach is about +-8.2 deviations for `double` and about +-5.3 for `float`: the grid never reaches 0 or 1, so a draw is finite, and the distribution is exactly symmetric. A `float` normal is a poor fit when the tails past 5.3 sigma matter; use `double`.
- The zero word gives the simplest value, and values move away from it as the word grows (this is for the test runner, which shrinks a failing test by making its words smaller; it changes no distribution, only which word gives which value). The integer uniform gives 0 clamped into the range, then 0 - 1, 0 + 1, 0 - 2... (zigzag), and the real uniforms do the same on their grid; the normals give the mean to within one grid step; the Bernoulli gives `false`; `categorical::sample` gives column 0; the permutation gives the identity. A draw is a pure function of its key, so shrinking one word never shifts another: part i of a `split` is the same however many parts came with it.
- `random::Tap` is for smith's test runner and nothing else: a key the runner made carries a `tap` (a field of `random::Key`, `nullptr` for every key a program makes), and `random::key_bits` passes each word through the tap, so the runner can record a failing test's words and replace them with smaller ones; `split` copies the tap to the keys it makes. There is no global to set, and a program's own keys stay pure functions of their bits.
- A weighted index (a categorical distribution) is the module `std::categorical`, because building its table allocates: [smith-categorical](../smith-categorical/SKILL.md).

## Fill an array: a loop

There is no `fill`. Filling is one loop that works for any distribution: each turn takes a key from the reassignment pattern, or you split into an array and use the plain loop (`smith-lang-affine`):

```smith
import std::random;

void fill(random::Key key, F32 out[:]) {
    for (Usize i = 0; i < countof(out); i++) {
        random::Key next[2];
        random::split(key, next);
        key = next[0];                      // the key moves on; next[1] is for this item
        out[i] = random::normal_float_sample(next[1], (random::NormalFloat){.mean = 0, .deviation = 1});
    }
}
```

With the split into an array the iterations run in any order, on any number of threads: the array is the same.

Also in this family: [smith-random](../smith-random/SKILL.md), [smith-random-own](../smith-random-own/SKILL.md), [smith-random-c](../smith-random-c/SKILL.md).
