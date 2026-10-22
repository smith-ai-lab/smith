// Tests of <smith/random.h>, run by tests/random.sh. It includes lib/random.c to reach the static generator and the
// normal's inverse CDF. With no argument it runs the checks. With an argument it runs one mode:
//   digest   print a digest of many draws from every distribution (every build configuration must print the same)
//   pins     print the pinned values below as C source, for updating them on purpose
//   assert   a normal with a deviation of 0, which must abort
//   stats    print the measured worst errors
#include "../bootstrap/lib/smith/random.c"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// The constants of one run of tests: keys are fixed, so every check is deterministic.
constexpr uint64_t test_seed = 20260101;
constexpr size_t test_draws = 1000000;

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

static const TestKnownAnswer test_known_answers[] = {
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

static double
test_chi_limit(size_t degrees) { // Wilson-Hilferty at z = 5, about 3e-7 in the tail: a correct draw passes
    double d = (double)degrees;
    double t = 1.0 - 2.0 / (9.0 * d) + 5.0 * sqrt(2.0 / (9.0 * d));
    return d * t * t * t;
}

static double test_chi_square(const size_t *observed, const double *expected, size_t cells) {
    double sum = 0;
    for (size_t i = 0; i < cells; i++) {
        double difference = (double)observed[i] - expected[i];
        sum += difference * difference / expected[i];
    }
    return sum;
}

// Distance in representable values between two finite doubles / floats.
static int64_t test_ulps_double(double a, double b) {
    int64_t x = (int64_t)smith_random_double_bits(a);
    int64_t y = (int64_t)smith_random_double_bits(b);
    if (x < 0) x = INT64_MIN - x;
    if (y < 0) y = INT64_MIN - y;
    return x > y ? x - y : y - x;
}

static int64_t test_ulps_float(float a, float b) {
    int64_t x = (int32_t)smith_random_float_bits(a);
    int64_t y = (int32_t)smith_random_float_bits(b);
    if (x < 0) x = INT32_MIN - x;
    if (y < 0) y = INT32_MIN - y;
    return x > y ? x - y : y - x;
}

// ---------------------------------------------------------------------------------------------------------------
// 3. Distributions.

static void test_keys(void) {
    SmithKey key = smith_key_make(7);
    assert(smith_key_bits(key, 0) == smith_key_bits(key, 0)); // pure
    assert(smith_key_bits(key, 0) != smith_key_bits(key, 1));
    assert(smith_key_bits(key, 1) != smith_key_bits(key, 2));
    assert(smith_key_derive(key, 0).bits != smith_key_derive(key, 1).bits);
    assert(smith_key_derive(key, 0).bits != smith_key_bits(key, 0)); // derive's domain is not bits'
    assert(smith_key_make(1).bits != smith_key_make(2).bits);
    for (uint64_t n = 0; n < 1000; n++) {
        assert(smith_key_unit_float(key, n) >= 0.0f && smith_key_unit_float(key, n) < 1.0f);
        assert(smith_key_unit_double(key, n) >= 0.0 && smith_key_unit_double(key, n) < 1.0);
        assert(1.0 - smith_key_unit_double(key, n) > 0.0);
    }
    uint64_t top = 0;
    for (uint64_t n = 0; n < 1000; n++)
        top += smith_key_bits(key, n) >> 63;
    assert(top > 400 && top < 600); // the top bit is fair
}

static void test_uniform_int64(void) {
    SmithKey key = smith_key_make(test_seed);
    size_t observed[10] = {0};
    for (uint64_t i = 0; i < test_draws; i++) {
        int64_t value = smith_uniform_int64_sample(smith_key_derive(key, i), (SmithUniformInt64){-3, 6});
        assert(value >= -3 && value <= 6);
        observed[value + 3]++;
    }
    double expected[10];
    for (size_t i = 0; i < 10; i++)
        expected[i] = (double)test_draws / 10.0;
    assert(test_chi_square(observed, expected, 10) < test_chi_limit(9));
    for (uint64_t i = 0; i < 100; i++) {
        assert(smith_uniform_int64_sample(smith_key_derive(key, i), (SmithUniformInt64){5, 5}) == 5);
        assert(smith_uniform_int64_sample(smith_key_derive(key, i), (SmithUniformInt64){INT64_MIN, INT64_MIN}) ==
               INT64_MIN);
        assert(smith_uniform_int64_sample(smith_key_derive(key, i), (SmithUniformInt64){INT64_MAX, INT64_MAX}) ==
               INT64_MAX);
    }
    // The full range is the word, zigzag decoded, so both signs and extremes of magnitude appear.
    size_t negative = 0;
    for (uint64_t i = 0; i < 10000; i++) {
        int64_t value = smith_uniform_int64_sample(smith_key_derive(key, i), (SmithUniformInt64){INT64_MIN, INT64_MAX});
        assert(value == smith_random_zigzag(smith_key_bits(smith_key_derive(key, i), 0)));
        negative += value < 0;
    }
    assert(negative > 4700 && negative < 5300);
}

// A span of 2^63 + 1 rejects about half of the candidates (2^64 mod span is 2^63 - 1); the offsets must still be
// uniform, so the half above span / 2 is hit half of the time.
static void test_uniform_int64_wide(void) {
    SmithKey key = smith_key_make(test_seed + 1);
    int64_t high = 0; // [INT64_MIN, 0] has 2^63 + 1 values
    constexpr size_t draws = 400000;
    size_t upper = 0;
    size_t retried = 0;
    for (uint64_t i = 0; i < draws; i++) {
        SmithKey k = smith_key_derive(key, i);
        int64_t value = smith_uniform_int64_sample(k, (SmithUniformInt64){INT64_MIN, high});
        assert(value <= high);
        upper += (uint64_t)value - (uint64_t)INT64_MIN >= ((uint64_t)1 << 62);
        unsigned _BitInt(128) product = (unsigned _BitInt(128))smith_key_bits(k, 0) * (((uint64_t)1 << 63) + 1);
        retried +=
            (uint64_t)product >= ((uint64_t)1 << 63) + 1; // the first candidate is rejected: low >= 2^64 - threshold
    }
    double deviation = sqrt((double)draws * 0.25);
    assert(fabs((double)upper - (double)draws / 2.0) < 5.0 * deviation);
    assert(retried > 0);
}

static void test_uniform_real(void) {
    SmithKey key = smith_key_make(test_seed + 2);
    double sum = 0;
    double square = 0;
    for (uint64_t i = 0; i < test_draws; i++) {
        double value = smith_uniform_double_sample(smith_key_derive(key, i), (SmithUniformDouble){2.0, 5.0});
        assert(value >= 2.0 && value < 5.0);
        sum += value;
        square += (value - 3.5) * (value - 3.5);
    }
    double n = (double)test_draws;
    assert(fabs(sum / n - 3.5) < 5.5 * 0.866 / sqrt(n)); // standard error of the mean: sigma / sqrt(n)
    assert(fabs(square / n - 0.75) < 6.0 * 0.75 * 0.895 / sqrt(n));
    sum = 0;
    square = 0;
    for (uint64_t i = 0; i < test_draws; i++) {
        float value = smith_uniform_float_sample(smith_key_derive(key, i), (SmithUniformFloat){-1.0f, 3.0f});
        assert(value >= -1.0f && value < 3.0f);
        sum += (double)value;
        square += ((double)value - 1.0) * ((double)value - 1.0);
    }
    assert(fabs(sum / n - 1.0) < 5.5 * 1.1547 / sqrt(n));
    assert(fabs(square / n - 4.0 / 3.0) < 6.0 * (4.0 / 3.0) * 0.895 / sqrt(n));
}

static void test_uniform_grid_and_rounding(void) {
    SmithKey key = smith_key_make(test_seed + 3);
    for (uint64_t i = 0; i < 10000; i++) { // on the grid: the unit scaled by 2^24 / 2^53 is an integer
        float f = smith_key_unit_float(smith_key_derive(key, i), 0);
        assert(f == floorf(f * 0x1p24f) / 0x1p24f);
        double d = smith_key_unit_double(smith_key_derive(key, i), 0);
        assert(d == floor(d * 0x1p53) / 0x1p53);
    }
    // Ranges built so that low + (high - low) * unit rounds up to high: high is a power of two just above low's
    // magnitude gap, so a unit near 1 rounds to high. The result must still be below high.
    const float lows[] = {1.0f, -0x1p-30f, 16777216.0f, -1.0f, 0.0f};
    const float highs[] = {1.0000001f, 0.0f, 16777218.0f, -0.99999994f, 0x1p-149f};
    size_t reached = 0;
    for (size_t r = 0; r < 5; r++) {
        for (uint64_t i = 0; i < 200000; i++) {
            SmithKey k = smith_key_derive(key, i);
            float value = smith_uniform_float_sample(k, (SmithUniformFloat){lows[r], highs[r]});
            assert(value >= lows[r] && value < highs[r]);
            reached += lows[r] + (highs[r] - lows[r]) * smith_key_unit_float(k, 0) >= highs[r];
        }
    }
    assert(reached > 0); // the rounding case happened, and was handled
    assert(smith_random_below_float(1.0f) == 0x1.fffffep-1f);
    assert(smith_random_below_float(0.0f) == -0x1p-149f);
    assert(smith_random_below_float(-1.0f) == -0x1.000002p+0f);
    assert(smith_random_below_double(1.0) == 0x1.fffffffffffffp-1);
    assert(smith_random_below_double(0.0) == -0x1p-1074);
    assert(smith_random_below_double(-1.0) == -0x1.0000000000001p+0);
    const double dlows[] = {1.0, -0x1p-60, 9007199254740992.0, -1.0};
    const double dhighs[] = {1.0000000000000002, 0.0, 9007199254740994.0, -0.9999999999999999};
    for (size_t r = 0; r < 4; r++) {
        for (uint64_t i = 0; i < 200000; i++) {
            double value =
                smith_uniform_double_sample(smith_key_derive(key, i), (SmithUniformDouble){dlows[r], dhighs[r]});
            assert(value >= dlows[r] && value < dhighs[r]);
        }
    }
}

// Normal: the moments, the inverse CDF against libm's erfc (only the reference here), symmetry, and the reach.
static double test_cdf(double z) {
    return 0.5 * erfc(-z / sqrt(2.0));
}

static void test_normal_moments(void) {
    SmithKey key = smith_key_make(test_seed + 4);
    double sum = 0;
    double square = 0;
    double sum_float = 0;
    double square_float = 0;
    for (uint64_t i = 0; i < test_draws; i++) {
        double d = smith_normal_double_sample(smith_key_derive(key, i), (SmithNormalDouble){1.0, 2.0});
        float f = smith_normal_float_sample(smith_key_derive(key, i), (SmithNormalFloat){-3.0f, 0.5f});
        sum += d;
        square += (d - 1.0) * (d - 1.0);
        sum_float += (double)f;
        square_float += ((double)f + 3.0) * ((double)f + 3.0);
    }
    double n = (double)test_draws;
    assert(fabs(sum / n - 1.0) < 5.5 * 2.0 / sqrt(n));
    assert(fabs(square / n - 4.0) < 5.5 * 4.0 * sqrt(2.0 / n));
    assert(fabs(sum_float / n + 3.0) < 5.5 * 0.5 / sqrt(n));
    assert(fabs(square_float / n - 0.25) < 5.5 * 0.25 * sqrt(2.0 / n));
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

static void test_bernoulli(void) {
    SmithKey key = smith_key_make(test_seed + 8);
    size_t hits = 0;
    for (uint64_t i = 0; i < test_draws; i++) {
        SmithKey k = smith_key_derive(key, i);
        assert(!smith_bernoulli_sample(k, (SmithBernoulli){0.0}));
        assert(smith_bernoulli_sample(k, (SmithBernoulli){1.0}));
        hits += smith_bernoulli_sample(k, (SmithBernoulli){0.3});
    }
    double n = (double)test_draws;
    assert(fabs((double)hits - 0.3 * n) < 5.5 * sqrt(n * 0.3 * 0.7));
}

// Categorical: frequencies by chi-square, a zero weight never drawn, one category, and a pinned table.
static void test_categorical(void) {
    static TestMemory memory;
    SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
    const double weights[] = {1.0, 2.0, 3.0, 0.0, 4.0, 0.5, 9.5};
    constexpr size_t count = sizeof weights / sizeof weights[0];
    SmithCategorical categorical = smith_categorical_make(&arena, weights, count);
    assert(categorical.count == count && arena.scopes == 0);
    SmithKey key = smith_key_make(test_seed + 9);
    size_t observed[count] = {0};
    for (uint64_t i = 0; i < test_draws; i++) {
        size_t index = smith_categorical_sample(smith_key_derive(key, i), categorical);
        assert(index < count);
        observed[index]++;
    }
    assert(observed[3] == 0);
    double expected[count - 1];
    size_t cells[count - 1];
    size_t used = 0;
    for (size_t i = 0; i < count; i++) {
        if (weights[i] == 0.0) continue;
        expected[used] = (double)test_draws * weights[i] / 20.0;
        cells[used++] = observed[i];
    }
    assert(test_chi_square(cells, expected, used) < test_chi_limit(used - 1));
    SmithCategorical one = smith_categorical_make(&arena, (const double[]){3.5}, 1);
    for (uint64_t i = 0; i < 100; i++)
        assert(smith_categorical_sample(smith_key_derive(key, i), one) == 0);
    SmithCategorical same = smith_categorical_make(&arena, weights, count); // construction is deterministic
    for (size_t i = 0; i < count; i++)
        assert(same.probability[i] == categorical.probability[i] && same.alias[i] == categorical.alias[i]);
    SmithCategorical tiny = smith_categorical_make(&arena, (const double[]){0.0, 1.0}, 2);
    for (uint64_t i = 0; i < 1000; i++)
        assert(smith_categorical_sample(smith_key_derive(key, i), tiny) == 1);
}

static void test_permutation(void) {
    SmithKey key = smith_key_make(test_seed + 10);
    size_t out[16];
    smith_permutation_sample(key, (SmithPermutation){0}, nullptr); // writes nothing
    smith_permutation_sample(key, (SmithPermutation){1}, out);
    assert(out[0] == 0);
    for (uint64_t i = 0; i < 1000; i++) {
        smith_permutation_sample(smith_key_derive(key, i), (SmithPermutation){16}, out);
        bool seen[16] = {false};
        for (size_t j = 0; j < 16; j++) {
            assert(out[j] < 16 && !seen[out[j]]);
            seen[out[j]] = true;
        }
    }
    size_t orders[24] = {0};
    constexpr size_t draws = 240000;
    for (uint64_t i = 0; i < draws; i++) {
        smith_permutation_sample(smith_key_derive(key, i), (SmithPermutation){4}, out);
        size_t rank = 0; // the Lehmer code gives each of the 24 orders its own number
        for (size_t a = 0; a < 4; a++) {
            size_t smaller = 0;
            for (size_t b = a + 1; b < 4; b++)
                smaller += out[b] < out[a];
            rank = rank * (4 - a) + smaller;
        }
        assert(rank < 24);
        orders[rank]++;
    }
    double expected[24];
    for (size_t i = 0; i < 24; i++)
        expected[i] = (double)draws / 24.0;
    assert(test_chi_square(orders, expected, 24) < test_chi_limit(23));
}

// smith_log_*: within 1 ulp of libm's log and logf over about 1e6 inputs, across exponents and subnormals. Returns the
// worst distance seen (the double reference is libm's log; the float one is logf, and the double log rounded to float).
static int64_t test_log_worst_double(void) {
    int64_t worst = 0;
    SmithKey key = smith_key_make(test_seed + 11);
    for (uint64_t i = 0; i < 1000000; i++) {
        uint64_t bits = smith_key_bits(key, i);
        double x = 0;
        switch (i % 4) {
        case 0:
            x = smith_random_bits_double(bits & 0x7fffffffffffffffULL);
            break; // every exponent
        case 1:
            x = smith_random_bits_double(bits & 0x000fffffffffffffULL);
            break; // subnormals (and 0)
        case 2:
            x = 0.5 + 1.5 * smith_key_unit_double(smith_key_derive(key, i), 0);
            break; // around 1
        default:
            x = 1.0 + ldexp(smith_key_unit_double(smith_key_derive(key, i), 0), -(int)(bits % 50));
            break;
        }
        if (!isfinite(x) || x == 0.0) continue;
        int64_t distance = test_ulps_double(smith_log_double(x), log(x));
        if (distance > worst) worst = distance;
    }
    return worst;
}

static int64_t test_log_worst_float(void) {
    int64_t worst = 0;
    SmithKey key = smith_key_make(test_seed + 12);
    for (uint64_t i = 0; i < 1000000; i++) {
        uint64_t bits = smith_key_bits(key, i);
        float x = 0;
        switch (i % 4) {
        case 0:
            x = smith_random_bits_float((uint32_t)bits & 0x7fffffffU);
            break;
        case 1:
            x = smith_random_bits_float((uint32_t)bits & 0x007fffffU);
            break; // subnormals
        case 2:
            x = 0.5f + 1.5f * smith_key_unit_float(smith_key_derive(key, i), 0);
            break;
        default:
            x = 1.0f + ldexpf(smith_key_unit_float(smith_key_derive(key, i), 0), -(int)(bits % 24));
            break;
        }
        if (!isfinite(x) || x == 0.0f) continue;
        int64_t distance = test_ulps_float(smith_log_float(x), logf(x));
        if (distance > worst) worst = distance;
    }
    return worst;
}

static void test_log(void) {
    assert(test_log_worst_double() <= 1);
    assert(test_log_worst_float() <= 1);
    assert(smith_log_double(0.0) == -INFINITY && smith_log_double(-0.0) == -INFINITY);
    assert(smith_log_float(0.0f) == -INFINITY && smith_log_float(-0.0f) == -INFINITY);
    assert(isnan(smith_log_double(-1.0)) && isnan(smith_log_double(-INFINITY)) && isnan(smith_log_double(-0x1p-1074)));
    assert(isnan(smith_log_float(-1.0f)) && isnan(smith_log_float(-INFINITY)) && isnan(smith_log_float(-0x1p-149f)));
    assert(smith_log_double(INFINITY) == INFINITY && smith_log_float(INFINITY) == INFINITY);
    assert(isnan(smith_log_double(NAN)) && isnan(smith_log_float(NAN)));
    assert(smith_log_double(1.0) == 0.0 && smith_log_float(1.0f) == 0.0f);
    assert(test_ulps_double(smith_log_double(0x1p-1074), log(0x1p-1074)) <= 1); // the smallest subnormal
    assert(test_ulps_float(smith_log_float(0x1p-149f), logf(0x1p-149f)) <= 1);
    assert(test_ulps_double(smith_log_double(DBL_MAX), log(DBL_MAX)) <= 1);
    assert(test_ulps_float(smith_log_float(FLT_MAX), logf(FLT_MAX)) <= 1);
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

static const uint64_t test_pinned_values[] = {
    0xe169c58d6627e8d5ULL, // bits
    0x9b00dbd8bc57ac4cULL, // bits
    0x5cb200dbf8e4cca4ULL, // bits
    0x097eff67b1a574ebULL, // bits
    0xf08d6eaa844515e1ULL, // derive
    0xd9c77dedca7cf69eULL, // derive
    0xbe4bbd74c5fa1393ULL, // derive
    0xe50a0ebce3e80670ULL, // bits
    0xb615aa2795f222c0ULL, // bits
    0xdfc5ccbeac08141bULL, // bits
    0xa7f6609379c07a47ULL, // bits
    0x428264b607071c12ULL, // derive
    0xf04a50803b539b4fULL, // derive
    0xa9b8bbe87d34a749ULL, // derive
    0x1547473972a47709ULL, // bits
    0x22799a5a9f41b01fULL, // bits
    0x4b67e03419fed511ULL, // bits
    0x9fe857b49d2c02e2ULL, // bits
    0x7e853102a74236edULL, // derive
    0xb9575d3a213c880dULL, // derive
    0x71cfe040fa00339aULL, // derive
    0x3fe0000000000000ULL, // categorical probability
    0x0000000000000004ULL, // categorical alias
    0x3ff0000000000000ULL, // categorical probability
    0x0000000000000001ULL, // categorical alias
    0x3ff0000000000000ULL, // categorical probability
    0x0000000000000002ULL, // categorical alias
    0x0000000000000000ULL, // categorical probability
    0x0000000000000004ULL, // categorical alias
    0x3fe0000000000000ULL, // categorical probability
    0x0000000000000002ULL, // categorical alias
    0x000000003ef30fa6ULL, // unit_float
    0x3fde61f4fa7e2600ULL, // unit_double
    0x00000000bf97e9c8ULL, // uniform_float
    0x3ff2fd391c8ed7c0ULL, // uniform_double
    0x0000000000000002ULL, // uniform_int64
    0x00000000be8a7c9cULL, // normal_float
    0x400229f29201b1cbULL, // normal_double
    0x0000000000000000ULL, // bernoulli
    0x0000000000000002ULL, // categorical
    0x00000000bf8c4f9fULL, // log_float
    0xbff189f3b6c24ca8ULL, // log_double
    0x000000003f6f5147ULL, // unit_float
    0x3fedea28f42121bcULL, // unit_double
    0x00000000402b2598ULL, // uniform_float
    0x400564b331296a2cULL, // uniform_double
    0x0000000000000006ULL, // uniform_int64
    0x00000000c02c0c06ULL, // normal_float
    0x4012c0c0ac77efeaULL, // normal_double
    0x0000000000000001ULL, // bernoulli
    0x0000000000000000ULL, // categorical
    0x000000003d634c72ULL, // log_float
    0x3fac698d2d755d83ULL, // log_double
    0x000000003f77aa93ULL, // unit_float
    0x3feef552732b2b7aULL, // unit_double
    0x0000000040359538ULL, // uniform_float
    0x4006b2a70ff5f658ULL, // uniform_double
    0x0000000000000006ULL, // uniform_int64
    0x00000000c0519b22ULL, // normal_float
    0xc00a33655f8a9c90ULL, // normal_double
    0x0000000000000001ULL, // bernoulli
    0x0000000000000000ULL, // categorical
    0x00000000bed47034ULL, // log_float
    0xbfda8e06607ae3c6ULL, // log_double
    0x000000003f6c4500ULL, // unit_float
    0x3fed88a0125f909bULL, // unit_double
    0x0000000040275640ULL, // uniform_float
    0x4004eac816f774c2ULL, // uniform_double
    0x0000000000000006ULL, // uniform_int64
    0x00000000409125d2ULL, // normal_float
    0xc00449741659234aULL, // normal_double
    0x0000000000000001ULL, // bernoulli
    0x0000000000000002ULL, // categorical
    0x000000003e57a913ULL, // log_float
    0x3fcaf52321baddefULL, // log_double
    0x0000000000000004ULL, // permutation
    0x0000000000000005ULL, // permutation
    0x0000000000000003ULL, // permutation
    0x0000000000000000ULL, // permutation
    0x0000000000000006ULL, // permutation
    0x0000000000000002ULL, // permutation
    0x0000000000000001ULL, // permutation
    0x0000000000000007ULL, // permutation
};

static void test_pinned(void) {
    static TestPins pins;
    test_pin_keys(&pins);
    test_pin_distributions(&pins);
    assert(pins.count == sizeof test_pinned_values / sizeof test_pinned_values[0]);
    for (size_t i = 0; i < pins.count; i++)
        assert(pins.value[i] == test_pinned_values[i]);
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

static uint64_t test_digest_draws(void) {
    static TestMemory memory;
    SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
    const double weights[] = {1.0, 2.0, 3.0, 0.0, 4.0, 0.5, 9.5};
    SmithCategorical categorical = smith_categorical_make(&arena, weights, 7);
    uint64_t digest = 0xcbf29ce484222325ULL;
    SmithKey key = smith_key_make(test_seed);
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

static uint64_t test_hook_constant(SmithKey key, uint64_t n, uint64_t bits) {
    (void)key;
    (void)n;
    (void)bits;
    return test_hook_word;
}

static uint64_t test_hook_identity(SmithKey key, uint64_t n, uint64_t bits) {
    (void)key;
    (void)n;
    return bits;
}

// The word that makes Lemire's index equal `index` for a span: the smallest word whose high half is the index.
static uint64_t test_word_for_index(uint64_t index, uint64_t span) {
    unsigned _BitInt(128) shifted = (unsigned _BitInt(128))index << 64;
    return (uint64_t)((shifted + span - 1) / span);
}

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
    smith_random_hook = test_hook_constant;
    SmithKey key = smith_key_make(1);
    const uint64_t spans[] = {1, 2, 3, 5, 6, 7, 10, 1000, ((uint64_t)1 << 63) + 1, UINT64_MAX};
    for (size_t i = 0; i < sizeof spans / sizeof spans[0]; i++)
        assert(smith_random_draw_index(key, spans[i]) == 0);
    smith_random_hook = nullptr;
}

static void test_zero_word_simplest(void) {
    SmithKey key = smith_key_make(test_seed + 13);
    static TestMemory memory;
    SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
    SmithCategorical categorical = smith_categorical_make(&arena, (const double[]){1.0, 2.0, 3.0}, 3);
    test_hook_word = 0;
    smith_random_hook = test_hook_constant;
    assert(smith_uniform_int64_sample(key, (SmithUniformInt64){-3, 6}) == 0);
    assert(smith_uniform_int64_sample(key, (SmithUniformInt64){5, 9}) == 5);
    assert(smith_uniform_int64_sample(key, (SmithUniformInt64){-9, -5}) == -5);
    assert(smith_uniform_int64_sample(key, (SmithUniformInt64){INT64_MIN, INT64_MAX}) == 0);
    assert(smith_uniform_int64_sample(key, (SmithUniformInt64){INT64_MIN, 1}) == 0);
    assert(smith_uniform_int64_sample(key, (SmithUniformInt64){INT64_MAX - 1, INT64_MAX}) == INT64_MAX - 1);
    assert(smith_uniform_double_sample(key, (SmithUniformDouble){2.0, 5.0}) == 2.0);
    assert(smith_uniform_float_sample(key, (SmithUniformFloat){2.0f, 5.0f}) == 2.0f);
    assert(smith_uniform_double_sample(key, (SmithUniformDouble){0.0, 5.0}) == 0.0);
    double d = smith_uniform_double_sample(key, (SmithUniformDouble){-1.0, 3.0}); // within one grid step of 0
    assert(fabs(d) <= 4.0 * 0x1p-53);
    float f = smith_uniform_float_sample(key, (SmithUniformFloat){-1.0f, 3.0f});
    assert(fabsf(f) <= 4.0f * 0x1p-24f);
    d = smith_uniform_double_sample(key, (SmithUniformDouble){-5.0, -1.0}); // the bound nearest 0: just below -1
    assert(d < -1.0 && d >= -1.0 - 4.0 * 0x1p-53 * 4.0);
    f = smith_uniform_float_sample(key, (SmithUniformFloat){-5.0f, -1.0f});
    assert(f < -1.0f && f >= -1.0f - 4.0f * 0x1p-24f * 4.0f);
    double nd = smith_normal_double_sample(key, (SmithNormalDouble){10.0, 2.0});
    assert(nd >= 10.0 && nd - 10.0 < 2.0 * 1e-15);
    float nf = smith_normal_float_sample(key, (SmithNormalFloat){10.0f, 2.0f});
    assert(nf >= 10.0f && nf - 10.0f < 2.0f * 1e-6f);
    assert(!smith_bernoulli_sample(key, (SmithBernoulli){0.3}));
    assert(!smith_bernoulli_sample(key, (SmithBernoulli){0.0}));
    assert(smith_bernoulli_sample(key, (SmithBernoulli){1.0}));
    assert(smith_categorical_sample(key, categorical) == 0); // column 0 and its own coin, as without a hook
    size_t order[9];
    smith_permutation_sample(key, (SmithPermutation){9}, order);
    for (size_t i = 0; i < 9; i++)
        assert(order[i] == i);
    smith_random_hook = nullptr;
}

// Words 1, 2 and 3 move the values away from the simplest one in the zigzag order.
static void test_small_words_zigzag(void) {
    SmithKey key = smith_key_make(test_seed + 14);
    smith_random_hook = test_hook_constant;
    const uint64_t words[] = {0, 1, 2, 3, 4};
    const int64_t full[] = {0, -1, 1, -2, 2};
    for (size_t i = 0; i < 5; i++) {
        test_hook_word = words[i];
        assert(smith_uniform_int64_sample(key, (SmithUniformInt64){INT64_MIN, INT64_MAX}) == full[i]);
        test_hook_word = test_word_for_index(i, 10); // indices 0 to 4 of a span of 10: [-3, 6]
        assert(smith_uniform_int64_sample(key, (SmithUniformInt64){-3, 6}) == full[i]);
    }
    double previous = -1.0;
    for (uint64_t w = 0; w < 5; w++) { // the real uniform: grid points 0, -1, +1, -2, +2 steps from 0
        test_hook_word = w << 11;
        double d = smith_uniform_double_sample(key, (SmithUniformDouble){0.0, 1.0});
        assert(d == (double)w * 0x1p-53 && d > previous);
        previous = d;
        d = smith_uniform_double_sample(key, (SmithUniformDouble){-1.0, 1.0}); // origin at the 2^52 grid point
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
    test_hook_word = (uint64_t)1 << 11; // the Bernoulli: only the last grid index is true, at probability 1 - 2^-53
    assert(!smith_bernoulli_sample(key, (SmithBernoulli){0.5}));
    test_hook_word = UINT64_MAX;
    assert(smith_bernoulli_sample(key, (SmithBernoulli){0.5}));
    test_hook_word = (uint64_t)1 << 11;
    assert(smith_bernoulli_sample(key, (SmithBernoulli){1.0}));
    smith_random_hook = nullptr;
}

static void test_zero_word_permutation_steps(void) {
    SmithKey key = smith_key_make(test_seed + 15);
    smith_random_hook = test_hook_constant;
    size_t out[4];
    test_hook_word = test_word_for_index(1, 4); // one step off the identity: only the last position swaps
    smith_permutation_sample(key, (SmithPermutation){4}, out);
    size_t moved = 0;
    for (size_t i = 0; i < 4; i++)
        moved += out[i] != i;
    assert(moved > 0);
    smith_random_hook = nullptr;
}

static void test_hook_identity_matches(void) {
    uint64_t without = test_digest_draws();
    smith_random_hook = test_hook_identity;
    uint64_t with = test_digest_draws();
    smith_random_hook = nullptr;
    assert(with == without);
    assert(smith_random_hook == nullptr);
}

static void test_print_pins(void) {
    static TestPins pins;
    test_pin_keys(&pins);
    test_pin_distributions(&pins);
    printf("// Generated by `random pins`: the numbers are part of the API.\nstatic const uint64_t "
           "test_pinned_values[] = {\n");
    for (size_t i = 0; i < pins.count; i++)
        printf("    0x%016" PRIx64 "ULL, // %s\n", pins.value[i], pins.label[i]);
    printf("};\n");
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "digest") == 0) {
        printf("%016" PRIx64 "\n", test_digest_draws());
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
        printf("log double worst ulps vs libm log: %" PRId64 "\n", test_log_worst_double());
        printf("log float worst ulps vs libm logf: %" PRId64 "\n", test_log_worst_float());
        double allowed = 0;
        printf("normal double worst relative CDF error: %.3e (%.2f of the allowed)\n",
               test_normal_error_double(&allowed), allowed);
        printf("normal float worst relative CDF error: %.3e (%.2f of the allowed)\n", test_normal_error_float(&allowed),
               allowed);
        return 0;
    }
    test_philox();
    test_keys();
    test_pinned();
    test_uniform_int64();
    test_uniform_int64_wide();
    test_uniform_real();
    test_uniform_grid_and_rounding();
    test_normal_moments();
    test_normal_cdf_symmetry_reach();
    test_bernoulli();
    test_categorical();
    test_permutation();
    test_log();
    test_around_bijection();
    test_index_zero_word();
    test_zero_word_simplest();
    test_small_words_zigzag();
    test_zero_word_permutation_steps();
    test_hook_identity_matches();
    printf("random: all checks passed\n");
    return 0;
}
