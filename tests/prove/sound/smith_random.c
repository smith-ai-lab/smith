// proves: smith_uniform_int64_sample smith_random_around smith_bernoulli_sample
// link: lib/smith/arena.smith
// smith's random draws: the proved functions, the ensures of the trusted draw_index (callers assume it), and what each
// draw promises in its comment (a value inside its range).
#include "random_lowered.c" // `smith cc --smith-lower lib/smith/random.smith`, written by tests/prove_sound_test.smith
#include "sound.h"

SOUND_MAIN({
    SOUND_RUN("random_around", sound_count, {
        const uint64_t count = sound_u64() | (sound_next() & 1 ? 0 : 1), origin = sound_below(count ? count : 1),
                       index = sound_next() & 1 ? sound_below(count ? count : 1) : sound_u64();
        SOUND_NOTE("random_around(index %llu, origin %llu, count %llu)", (unsigned long long)index,
                   (unsigned long long)origin, (unsigned long long)count);
        const uint64_t result = smith_random_around(index, origin, count);
        if (result >= count) {
            fprintf(stderr, "random_around left [0, count): %llu\n", (unsigned long long)result);
            exit(1);
        }
    });
    SOUND_RUN("uniform_int64_sample", sound_count, {
        const int64_t a = (int64_t)sound_u64(), b = (int64_t)sound_u64();
        const int64_t low = sound_next() & 1 && a > b ? b : a, high = sound_next() & 1 && a > b ? a : b;
        const uint64_t seed = sound_u64(); // a key is made from it where it is used: a key is used once
        SOUND_NOTE("uniform_int64_sample(key %llu, low %lld, high %lld)", (unsigned long long)seed, (long long)low,
                   (long long)high);
        const int64_t value = smith_uniform_int64_sample(smith_key_make(seed), (SmithUniformInt64){.low = low, .high = high});
        if (value < low || value > high) {
            fprintf(stderr, "uniform_int64_sample left [low, high]: %lld\n", (long long)value);
            exit(1);
        }
    });
    SOUND_RUN("uniform_int64_sample (the whole range, and one value)", sound_count, {
        const uint64_t seed = sound_u64(); // a key is made from it where it is used: a key is used once
        const int64_t low = sound_next() & 1 ? INT64_MIN : (int64_t)sound_u64(), high = sound_next() & 1 ? INT64_MAX : low;
        SOUND_NOTE("uniform_int64_sample(key %llu, low %lld, high %lld)", (unsigned long long)seed, (long long)low,
                   (long long)high);
        const int64_t value = smith_uniform_int64_sample(smith_key_make(seed), (SmithUniformInt64){.low = low, .high = high});
        if (value < low || value > high) exit(1);
    });
    SOUND_RUN("draw_index (trusted: its ensures)", sound_count, {
        const uint64_t seed = sound_u64(); // a key is made from it where it is used: a key is used once
        const uint64_t n = sound_u64();
        SOUND_NOTE("draw_index(key %llu, n %llu)", (unsigned long long)seed, (unsigned long long)n);
        const SmithKey key = smith_key_make(seed);
        (void)smith_random_draw_index(&key, n);
    });
    SOUND_RUN("bernoulli_sample", sound_count, {
        const uint64_t seed = sound_u64(); // a key is made from it where it is used: a key is used once
        static const double edges[] = {0.0, 1.0, 0.5, 0x1p-53, 1.0 - 0x1p-53, -0.0, 1.5, -0.5};
        const double p = sound_next() & 1 ? edges[sound_next() % (sizeof edges / sizeof edges[0])]
                                          : (double)(sound_next() >> 11) * 0x1p-53;
        SOUND_NOTE("bernoulli_sample(key %llu, p %a)", (unsigned long long)seed, p);
        const bool value = smith_bernoulli_sample(smith_key_make(seed), (SmithBernoulli){.probability = p});
        if ((p == 0.0 && value) || (p == 1.0 && !value)) {
            fprintf(stderr, "bernoulli_sample ignored its probability\n");
            exit(1);
        }
    });
})
