---
name: smith-random-c
description: "smith::random from C (#include <smith/random.h>) and compiling it in build.c with smith_library(b, "random"). Load when C code includes <smith/random.h> or a build.c compiles random."
---

# random from C

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

## Get the library in build.c

random uses the arena (`smith_categorical_make`), so a project compiles both. smith writes the headers and the sources into `.smith/` on every `smith build`. random is written in `.smith` (the module `smith::random`, source `.smith/lib/smith/random.smith`), so compiling it needs a smith with clang inside; C code that only includes `<smith/random.h>` works anywhere, and the C API (names, types, symbols) is the one it always had:

```c
SmithObject random = {.compiler = "smith-cc", .source = smith_library(b, "random"), .output = "build/smith/random.o", .flags = flags};
smith_object(b, &random);
SmithObject arena = {.compiler = "smith-cc", .source = smith_library(b, "arena"), .output = "build/smith/arena.o", .flags = flags};
smith_object(b, &arena);
```

`flags` includes `-I .smith/include`, and the program's `.objects` list both. Add `-lm` to the link on platforms where `sqrt` is not inlined (macOS does not need it). See the smith-build and smith-arena skills.

Also in this family: [smith-random](../smith-random/SKILL.md), [smith-random-distributions](../smith-random-distributions/SKILL.md), [smith-random-own](../smith-random-own/SKILL.md).
