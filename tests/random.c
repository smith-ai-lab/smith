// The white-box checks of <smith/random.h>, run by tests/random.sh; the rest of random's checks are in
// tests/random_test.smith, on smith's test runner, through the exported API. It includes lib/random.c to reach the
// static generator and the normal's inverse CDF. With no argument it runs the checks. With an argument it runs one
// mode:
//   digest   print a digest of many draws from every distribution (every build configuration must print the same)
//   pins     print the pinned values as C source, for updating them in tests/random_test.smith on purpose
//   assert   a normal with a deviation of 0, which must abort
//   stats    print the measured worst errors
#include "random_lowered.c" // `smith cc --smith-lower lib/smith/random.smith`, written by tests/random.sh

#include <assert.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// The constants of one run of tests: keys are fixed, so every check is deterministic.
constexpr uint64_t test_seed = 20260101;

typedef struct {
    alignas(smith_arena_align_max) unsigned char bytes[4096];
} TestMemory;

// ---------------------------------------------------------------------------------------------------------------
// 1. Philox's known answers: Random123's tests/kat_vectors, https://github.com/DEShawResearch/random123, file
//    tests/kat_vectors (fetched 2026-10-20). Each line is: name, rounds, counter (4 words), key (2 words), output. The
//    three "philox4x32 10" lines:
//    philox4x32 10 00000000 00000000 00000000 00000000 00000000 00000000   6627e8d5 e169c58d bc57ac4c 9b00dbd8
//    philox4x32 10 ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff   408f276d 41c83b0e a20bc7c6 6d5451fd
//    philox4x32 10 243f6a88 85a308d3 13198a2e 03707344 a4093822 299f31d0   d16cfe09 94fdcceb 5001e420 24126ea1
typedef struct {
    uint32_t counter[4];
    uint32_t key[2];
    uint32_t expected[4];
} TestKnownAnswer;

static constexpr TestKnownAnswer test_known_answers[] = {
    {{0, 0, 0, 0}, {0, 0}, {0x6627e8d5, 0xe169c58d, 0xbc57ac4c, 0x9b00dbd8}},
    {{0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff},
     {0xffffffff, 0xffffffff},
     {0x408f276d, 0x41c83b0e, 0xa20bc7c6, 0x6d5451fd}},
    {{0x243f6a88, 0x85a308d3, 0x13198a2e, 0x03707344},
     {0xa4093822, 0x299f31d0},
     {0xd16cfe09, 0x94fdcceb, 0x5001e420, 0x24126ea1}},
};

static void test_philox(void) {
    for (size_t i = 0; i < sizeof test_known_answers / sizeof test_known_answers[0]; i++) {
        const TestKnownAnswer *vector = &test_known_answers[i];
        smith_random_Block counter = {{vector->counter[0], vector->counter[1], vector->counter[2], vector->counter[3]}};
        smith_random_Block out = smith_random_philox(counter, vector->key[0], vector->key[1]);
        for (size_t w = 0; w < 4; w++)
            assert(out.word[w] == vector->expected[w]);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 2. Pinned outputs. The numbers are part of the API: a change to any of them is a breaking change.

// ---------------------------------------------------------------------------------------------------------------
// Helpers.

// ---------------------------------------------------------------------------------------------------------------
// 3. Distributions.

// The full range is the word, zigzag decoded (the rest of the uniform integer is in tests/random_test.smith).
static void test_uniform_int64_full_range_is_zigzag(void) {
    SmithKey key = smith_key_make(test_seed);
    for (uint64_t i = 0; i < 10000; i++) {
        int64_t value = smith_uniform_int64_sample(smith_key_derive(key, i), (SmithUniformInt64){INT64_MIN, INT64_MAX});
        assert(value == smith_random_zigzag(smith_key_bits(smith_key_derive(key, i), 0)));
    }
}

// The largest value below a bound, which the uniform draws return when rounding reaches the bound.
static void test_below(void) {
    assert(smith_random_below_float(1.0f) == 0x1.fffffep-1f);
    assert(smith_random_below_float(0.0f) == -0x1p-149f);
    assert(smith_random_below_float(-1.0f) == -0x1.000002p+0f);
    assert(smith_random_below_double(1.0) == 0x1.fffffffffffffp-1);
    assert(smith_random_below_double(0.0) == -0x1p-1074);
    assert(smith_random_below_double(-1.0) == -0x1.0000000000001p+0);
}

// Normal: the moments, the inverse CDF against libm's erfc (only the reference here), symmetry, and the reach.
static double test_cdf(double z) {
    return 0.5 * erfc(-z / sqrt(2.0));
}

static void test_normal_error_one_double(double p, double *worst, double *allowed) {
    double z = smith_random_ppnd16(p);
    double error = fabs(test_cdf(z) - p) / p;
    if (error > *worst) *worst = error;
    double fraction = error / (1e-14 + 4e-16 * z * z);
    if (fraction > *allowed) *allowed = fraction;
}

static void test_normal_error_one_float(float p, double *worst, double *allowed) {
    double z = (double)smith_random_ppnd7(p);
    double error = fabs(test_cdf(z) - (double)p) / (double)p;
    if (error > *worst) *worst = error;
    double fraction = error / (1e-6 + 4e-7 * z * z);
    if (fraction > *allowed) *allowed = fraction;
}

// The relative error of the CDF at z against the probability that produced z, over a grid, a sweep of every scale,
// and the extremes. Returns the worst.
// The CDF is ill-conditioned in the tail: a relative error e in z is z * z * e in the probability, so the allowed error
// is a base plus a term in z^2. *allowed is the worst error as a fraction of what is allowed (it must stay below 1).
static double test_normal_error_double(double *allowed) {
    double worst = 0;
    *allowed = 0;
    const double scales[] = {1.0, 1.37, 1.9};
    for (int e = 2; e <= 53; e++) { // p from 2^-53 up to just under 1/2
        for (size_t s = 0; s < 3; s++) {
            double p = ldexp(scales[s], -e);
            if (p < 0x1p-53 || p >= 0.5) continue;
            test_normal_error_one_double(p, &worst, allowed);
        }
    }
    SmithKey key = smith_key_make(test_seed + 5);
    for (uint64_t i = 0; i < 200000; i++) {
        double p = smith_random_normal_u_double(smith_key_derive(key, i));
        if (p > 0.5) p = 1.0 - p;
        test_normal_error_one_double(p, &worst, allowed);
    }
    return worst;
}

static double test_normal_error_float(double *allowed) {
    double worst = 0;
    *allowed = 0;
    SmithKey key = smith_key_make(test_seed + 6);
    for (uint64_t i = 0; i < 100000; i++) {
        float p = smith_random_normal_u_float(smith_key_derive(key, i));
        if (p > 0.5f) p = 1.0f - p;
        test_normal_error_one_float(p, &worst, allowed);
    }
    for (int e = 2; e <= 24; e++) {
        for (int step = 0; step < 16; step++) {
            float scale = 1.0f + (float)step * 0.0625f;
            float p = ldexpf(scale, -e);
            if (p < 0x1p-24f || p >= 0.5f) continue;
            test_normal_error_one_float(p, &worst, allowed);
        }
    }
    return worst;
}

static void test_normal_cdf_symmetry_reach(void) {
    double allowed = 0;
    assert(test_normal_error_double(&allowed) < 1e-13 && allowed < 1.0);
    assert(test_normal_error_float(&allowed) < 1e-5 && allowed < 1.0);
    SmithKey key = smith_key_make(test_seed + 7);
    for (uint64_t i = 0; i < 100000; i++) { // sample(u) and sample(1 - u) are exact negatives
        double u = smith_random_normal_u_double(smith_key_derive(key, i));
        assert(smith_random_ppnd16(u) == -smith_random_ppnd16(1.0 - u));
        float f = smith_random_normal_u_float(smith_key_derive(key, i));
        assert(smith_random_ppnd7(f) == -smith_random_ppnd7(1.0f - f));
    }
    // The extremes of the grid: the documented reach, and every value finite.
    double reach_double = smith_random_ppnd16(0x1p-53);
    assert(reach_double < -8.2 && reach_double > -8.3);
    assert(smith_random_ppnd16(1.0 - 0x1p-53) == -reach_double);
    float reach_float = smith_random_ppnd7(0x1p-24f);
    assert(reach_float < -5.2f && reach_float > -5.4f);
    assert(smith_random_ppnd7(1.0f - 0x1p-24f) == -reach_float);
    for (uint64_t i = 0; i < 100000; i++) {
        assert(fabs(smith_normal_double_sample(smith_key_derive(key, i), (SmithNormalDouble){0.0, 1.0})) <=
               -reach_double);
        assert(fabsf(smith_normal_float_sample(smith_key_derive(key, i), (SmithNormalFloat){0.0f, 1.0f})) <=
               -reach_float);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The pinned values: bits of smith_key_bits for seeds 0, 1 and UINT64_MAX, smith_key_derive, and every distribution
// for one key (floats as their bits). Run `pins` to print them.

constexpr size_t test_pin_capacity = 128;

typedef struct {
    const char *label[test_pin_capacity];
    uint64_t value[test_pin_capacity];
    size_t count;
} TestPins;

static void test_pin(TestPins *pins, const char *label, uint64_t value) {
    assert(pins->count < test_pin_capacity);
    pins->label[pins->count] = label;
    pins->value[pins->count++] = value;
}

static void test_pin_keys(TestPins *pins) {
    const uint64_t seeds[] = {0, 1, UINT64_MAX};
    for (size_t s = 0; s < 3; s++) {
        for (uint64_t n = 0; n < 4; n++)
            test_pin(pins, "bits", smith_key_bits(smith_key_make(seeds[s]), n));
        for (uint64_t i = 0; i < 3; i++)
            test_pin(pins, "derive", smith_key_derive(smith_key_make(seeds[s]), i).bits);
    }
}

static void test_pin_distributions(TestPins *pins) {
    static TestMemory memory;
    SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
    SmithKey key = smith_key_make(42);
    const double weights[] = {1.0, 2.0, 3.0, 0.0, 4.0};
    SmithCategorical categorical = smith_categorical_make(&arena, weights, 5);
    for (size_t i = 0; i < 5; i++) {
        test_pin(pins, "categorical probability", smith_random_double_bits(categorical.probability[i]));
        test_pin(pins, "categorical alias", categorical.alias[i]);
    }
    for (uint64_t i = 0; i < 4; i++) {
        SmithKey k = smith_key_derive(key, i);
        test_pin(pins, "unit_float", smith_random_float_bits(smith_key_unit_float(k, 0)));
        test_pin(pins, "unit_double", smith_random_double_bits(smith_key_unit_double(k, 0)));
        test_pin(pins, "uniform_float",
                 smith_random_float_bits(smith_uniform_float_sample(k, (SmithUniformFloat){-2.0f, 3.0f})));
        test_pin(pins, "uniform_double",
                 smith_random_double_bits(smith_uniform_double_sample(k, (SmithUniformDouble){-2.0, 3.0})));
        test_pin(pins, "uniform_int64", (uint64_t)smith_uniform_int64_sample(k, (SmithUniformInt64){-3, 6}));
        test_pin(pins, "normal_float",
                 smith_random_float_bits(smith_normal_float_sample(k, (SmithNormalFloat){1.0f, 2.0f})));
        test_pin(pins, "normal_double",
                 smith_random_double_bits(smith_normal_double_sample(k, (SmithNormalDouble){1.0, 2.0})));
        test_pin(pins, "bernoulli", smith_bernoulli_sample(k, (SmithBernoulli){0.3}));
        test_pin(pins, "categorical", smith_categorical_sample(k, categorical));
        test_pin(pins, "log_float", smith_random_float_bits(smith_log_float(smith_key_unit_float(k, 1) + 0.25f)));
        test_pin(pins, "log_double", smith_random_double_bits(smith_log_double(smith_key_unit_double(k, 1) + 0.25)));
    }
    size_t order[8];
    smith_permutation_sample(key, (SmithPermutation){8}, order);
    for (size_t i = 0; i < 8; i++)
        test_pin(pins, "permutation", order[i]);
}

// ---------------------------------------------------------------------------------------------------------------
// The digest: FNV-1a over the bits of many draws from every distribution. Any two builds must print the same one.

static uint64_t test_fold(uint64_t digest, uint64_t value) {
    for (uint64_t byte = 0; byte < 8; byte++) {
        digest ^= (value >> (8 * byte)) & 0xff;
        digest *= 0x100000001b3ULL;
    }
    return digest;
}

static uint64_t test_digest_draws(const SmithRandomTap *tap) {
    static TestMemory memory;
    SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
    const double weights[] = {1.0, 2.0, 3.0, 0.0, 4.0, 0.5, 9.5};
    SmithCategorical categorical = smith_categorical_make(&arena, weights, 7);
    uint64_t digest = 0xcbf29ce484222325ULL;
    SmithKey key = smith_key_make(test_seed);
    key.tap = tap;
    for (uint64_t i = 0; i < 100000; i++) {
        SmithKey k = smith_key_derive(key, i);
        digest = test_fold(digest, k.bits);
        digest = test_fold(digest, smith_key_bits(k, i % 7));
        digest = test_fold(digest, smith_random_float_bits(smith_key_unit_float(k, 1)));
        digest = test_fold(digest, smith_random_double_bits(smith_key_unit_double(k, 2)));
        digest =
            test_fold(digest, smith_random_float_bits(smith_uniform_float_sample(k, (SmithUniformFloat){-7.5f, 1e6f})));
        digest = test_fold(digest,
                           smith_random_double_bits(smith_uniform_double_sample(k, (SmithUniformDouble){-0.3, 1e-9})));
        digest =
            test_fold(digest, (uint64_t)smith_uniform_int64_sample(k, (SmithUniformInt64){-1000, 12345678901234LL}));
        digest = test_fold(digest, (uint64_t)smith_uniform_int64_sample(k, (SmithUniformInt64){INT64_MIN, 1}));
        digest =
            test_fold(digest, smith_random_float_bits(smith_normal_float_sample(k, (SmithNormalFloat){3.0f, 0.1f})));
        digest = test_fold(digest,
                           smith_random_double_bits(smith_normal_double_sample(k, (SmithNormalDouble){-3.0, 100.0})));
        digest = test_fold(digest, smith_bernoulli_sample(k, (SmithBernoulli){0.37}));
        digest = test_fold(digest, smith_categorical_sample(k, categorical));
        digest = test_fold(digest, smith_random_float_bits(smith_log_float(
                                       smith_random_bits_float((uint32_t)smith_key_bits(k, 9) & 0x7fffffffU))));
        digest = test_fold(digest, smith_random_double_bits(smith_log_double(
                                       smith_random_bits_double(smith_key_bits(k, 8) & 0x7fffffffffffffffULL))));
    }
    size_t order[37];
    for (uint64_t i = 0; i < 2000; i++) {
        smith_permutation_sample(smith_key_derive(key, i), (SmithPermutation){37}, order);
        for (size_t j = 0; j < 37; j++)
            digest = test_fold(digest, order[j]);
    }
    return digest;
}

// ---------------------------------------------------------------------------------------------------------------
// 4. Shrinking: the zero word gives the simplest value, and the hook.

static uint64_t test_hook_word;

static uint64_t test_hook_constant(const SmithRandomTap *tap, uint64_t key, uint64_t n, uint64_t bits) {
    (void)tap;
    (void)key;
    (void)n;
    (void)bits;
    return test_hook_word;
}

static uint64_t test_hook_identity(const SmithRandomTap *tap, uint64_t key, uint64_t n, uint64_t bits) {
    (void)tap;
    (void)key;
    (void)n;
    return bits;
}

static const SmithRandomTap test_tap_constant = {test_hook_constant};
static const SmithRandomTap test_tap_identity = {test_hook_identity};

// A bijection: over each (count, origin), indices 0 to count - 1 give every position exactly once, and index 0 gives
// the origin.
static void test_around_bijection(void) {
    for (uint64_t count = 1; count <= 40; count++) {
        for (uint64_t origin = 0; origin < count; origin++) {
            bool seen[40] = {false};
            assert(smith_random_around(0, origin, count) == origin);
            for (uint64_t index = 0; index < count; index++) {
                uint64_t position = smith_random_around(index, origin, count);
                assert(position < count && !seen[position]);
                seen[position] = true;
            }
        }
    }
    const int64_t bounds[][2] = {{-3, 6}, {5, 9}, {-9, -5}, {0, 4}, {-4, 0}, {-1, 1}, {7, 7}, {-2, 40}};
    for (size_t b = 0; b < sizeof bounds / sizeof bounds[0]; b++) {
        uint64_t span = (uint64_t)(bounds[b][1] - bounds[b][0] + 1);
        bool seen[64] = {false};
        for (uint64_t index = 0; index < span; index++) {
            int64_t value = smith_random_int_value(bounds[b][0], bounds[b][1], index);
            assert(value >= bounds[b][0] && value <= bounds[b][1] && !seen[value - bounds[b][0]]);
            seen[value - bounds[b][0]] = true;
        }
    }
    // The order, with room on both sides, then with one side short, then with no room below or above.
    const int64_t order[] = {0, -1, 1, -2, 2, -3, 3, 4, 5, 6};
    for (uint64_t index = 0; index < 10; index++)
        assert(smith_random_int_value(-3, 6, index) == order[index]);
    const int64_t order_low[] = {0, -1, 1, 2, 3, 4, 5, 6};
    for (uint64_t index = 0; index < 8; index++)
        assert(smith_random_int_value(-1, 6, index) == order_low[index]);
    const int64_t order_high[] = {-1, -2, -3, -4};
    for (uint64_t index = 0; index < 4; index++)
        assert(smith_random_int_value(-4, -1, index) == order_high[index]);
    for (uint64_t index = 0; index < 5; index++)
        assert(smith_random_int_value(5, 9, index) == 5 + (int64_t)index);
    assert(smith_random_zigzag(0) == 0 && smith_random_zigzag(1) == -1 && smith_random_zigzag(2) == 1);
    assert(smith_random_zigzag(3) == -2 && smith_random_zigzag(UINT64_MAX) == INT64_MIN);
    assert(smith_random_zigzag(UINT64_MAX - 1) == INT64_MAX);
}

// A word at 0 is accepted by Lemire's rejection (it would not be if the smallest low half were rejected).
static void test_index_zero_word(void) {
    test_hook_word = 0;
    SmithKey key = smith_key_make(1);
    key.tap = &test_tap_constant;
    const uint64_t spans[] = {1, 2, 3, 5, 6, 7, 10, 1000, ((uint64_t)1 << 63) + 1, UINT64_MAX};
    for (size_t i = 0; i < sizeof spans / sizeof spans[0]; i++)
        assert(smith_random_draw_index(key, spans[i]) == 0);
}

// Words 1, 2 and 3 move the values away from the simplest one in the zigzag order (the integers and the unit are in
// tests/random_test.smith).
static void test_small_words_zigzag(void) {
    SmithKey key = smith_key_make(test_seed + 14);
    key.tap = &test_tap_constant;
    for (uint64_t w = 0; w < 5; w++) {
        test_hook_word = w << 11;
        double d = smith_uniform_double_sample(key, (SmithUniformDouble){-1.0, 1.0}); // origin at the 2^52 grid point
        assert(d == (double)smith_random_zigzag(w) * 0x1p-52 + 0.0);
    }
    for (uint64_t w = 0; w < 5; w++) { // the normal: u just above 0.5, just below, the next above...
        test_hook_word = w << 12;
        double u = smith_random_normal_u_double(key);
        assert(u == 0.5 + (double)(2 * smith_random_zigzag(w) + 1) * 0x1p-53);
        assert(smith_normal_double_sample(key, (SmithNormalDouble){0.0, 1.0}) ==
               ((w & 1) == 0 ? 1.0 : -1.0) * fabs(smith_random_ppnd16(u)));
        test_hook_word = w << 41;
        float uf = smith_random_normal_u_float(key);
        assert(uf == 0.5f + (float)(2 * smith_random_zigzag(w) + 1) * 0x1p-24f);
    }
}

static void test_hook_identity_matches(void) {
    uint64_t without = test_digest_draws(nullptr);
    uint64_t with = test_digest_draws(&test_tap_identity);
    assert(with == without);
}

static void test_print_pins(void) {
    static TestPins pins;
    test_pin_keys(&pins);
    test_pin_distributions(&pins);
    printf("// Generated by `random pins`, for tests/random_test.smith: the numbers are part of the API.\nstatic const "
           "uint64_t "
           "pinned_values[] = {\n");
    for (size_t i = 0; i < pins.count; i++)
        printf("    0x%016" PRIx64 "ULL, // %s\n", pins.value[i], pins.label[i]);
    printf("};\n");
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "digest") == 0) {
        printf("%016" PRIx64 "\n", test_digest_draws(nullptr));
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "pins") == 0) {
        test_print_pins();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "assert") == 0) {
        SmithKey key = smith_key_make(1);
        double value = smith_normal_double_sample(key, (SmithNormalDouble){0.0, 0.0}); // deviation <= 0 aborts
        printf("not reached %g\n", value);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "stats") == 0) {
        double allowed = 0;
        printf("normal double worst relative CDF error: %.3e (%.2f of the allowed)\n",
               test_normal_error_double(&allowed), allowed);
        printf("normal float worst relative CDF error: %.3e (%.2f of the allowed)\n", test_normal_error_float(&allowed),
               allowed);
        return 0;
    }
    test_philox();
    test_uniform_int64_full_range_is_zigzag();
    test_below();
    test_normal_cdf_symmetry_reach();
    test_around_bijection();
    test_index_zero_word();
    test_small_words_zigzag();
    test_hook_identity_matches();
    printf("random: all checks passed\n");
    return 0;
}
