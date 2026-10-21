#include "../include/smith/random.h"

// The same bits on every platform: a fused multiply-add rounds once where the standard rounds twice, and changes the
// last bit of a draw. The pragma turns contraction off, which holds against -ffp-contract=on (clang's default) and
// against fast-honor-pragmas, but -ffp-contract=fast ignores pragmas by definition. So every product that feeds a sum
// goes through smith_random_mul_*, whose empty asm makes the product opaque to the optimizer: nothing fuses, in any
// flags. -ffast-math, which also reorders, is refused.
#pragma STDC FP_CONTRACT OFF
#ifdef __FAST_MATH__
#error \
    "smith/random: -ffast-math changes floating-point results, so a key would stop giving the same numbers; compile lib/random.c without it"
#endif

#include <assert.h>
#include <math.h>
#include <string.h>

static_assert(FLT_EVAL_METHOD == 0, "float arithmetic must happen in float");

// a * b as a value the compiler cannot fuse into a following addition. The register class keeps it out of memory on the
// architecture that is tested; "X" is correct everywhere, if slower.
#if defined(__aarch64__)
#define SMITH_RANDOM_OPAQUE(variable) __asm__("" : "+w"(variable))
#else
#define SMITH_RANDOM_OPAQUE(variable) __asm__("" : "+X"(variable))
#endif

// A value passed through an empty asm is opaque to the optimizer, which then cannot fold it into immediates: the Philox
// constants stay in registers (left alone the key schedule becomes ten immediates, three instructions each, where an
// add of a register is one), and the polynomial coefficients are loaded (rebuilt from immediates they cost four
// instructions each). This only changes how the same bits are computed.
#define SMITH_RANDOM_REGISTER(variable) __asm__("" : "+r"(variable))

static inline double smith_random_mul_double(double a, double b) {
    double product = a * b;
    SMITH_RANDOM_OPAQUE(product);
    return product;
}

static inline float smith_random_mul_float(float a, float b) {
    float product = a * b;
    SMITH_RANDOM_OPAQUE(product);
    return product;
}

// The polynomial c[0] + c[1] x + ... + c[count - 1] x^(count - 1) by Horner's rule: one rounded product and one rounded
// sum per step.
static double smith_random_horner_double(const double *c, size_t count, double x) {
    SMITH_RANDOM_REGISTER(c); // the coefficients are loaded, not rebuilt from immediates (4 instructions each)
    double sum = c[count - 1];
    for (size_t i = count - 1; i > 0; i--)
        sum = smith_random_mul_double(sum, x) + c[i - 1];
    return sum;
}

static float smith_random_horner_float(const float *c, size_t count, float x) {
    SMITH_RANDOM_REGISTER(c); // the coefficients are loaded, not rebuilt from immediates (4 instructions each)
    float sum = c[count - 1];
    for (size_t i = count - 1; i > 0; i--)
        sum = smith_random_mul_float(sum, x) + c[i - 1];
    return sum;
}

// ---------------------------------------------------------------------------------------------------------------
// Philox4x32-10: Salmon, Moraes, Dror and Shaw, "Parallel Random Numbers: As Easy as 1, 2, 3", SC'11 (Random123). A
// counter-based generator: a 64-bit key and a 128-bit counter give four 32-bit words, with no state between calls.

constexpr uint32_t smith_random_multiplier_0 = 0xD2511F53;
constexpr uint32_t smith_random_multiplier_1 = 0xCD9E8D57;
constexpr uint32_t smith_random_increment_0 = 0x9E3779B9;
constexpr uint32_t smith_random_increment_1 = 0xBB67AE85;
constexpr int smith_random_rounds = 10;

typedef struct {
    uint32_t word[4];
} SmithRandomBlock;

// One Philox round on the counter words c[0..3] with the round key k0, k1.
#define SMITH_RANDOM_ROUND(c0, c1, c2, c3, k0, k1, m0, m1) \
    do { \
        uint64_t product_0 = (uint64_t)(m0) * (c0); \
        uint64_t product_1 = (uint64_t)(m1) * (c2); \
        (c0) = (uint32_t)((uint64_t)((c1) ^ (k0)) ^ (product_1 >> 32)); \
        (c1) = (uint32_t)product_1; \
        (c2) = (uint32_t)((uint64_t)((c3) ^ (k1)) ^ (product_0 >> 32)); \
        (c3) = (uint32_t)product_0; \
    } while (0)

// Philox4x32-10 for a counter and a key. The first nine rounds are the same for every caller; the tenth is done by the
// caller, which usually needs only two of the four words (the rest of that round is then skipped).
typedef struct {
    uint32_t word[4];
    uint32_t key_0, key_1;
} SmithRandomRounds;

static inline SmithRandomRounds smith_random_nine_rounds(SmithRandomBlock counter, uint32_t key_0, uint32_t key_1) {
    uint32_t multiplier_0 = smith_random_multiplier_0;
    uint32_t multiplier_1 = smith_random_multiplier_1;
    uint32_t increment_0 = smith_random_increment_0;
    uint32_t increment_1 = smith_random_increment_1;
    SMITH_RANDOM_REGISTER(multiplier_0);
    SMITH_RANDOM_REGISTER(multiplier_1);
    SMITH_RANDOM_REGISTER(increment_0);
    SMITH_RANDOM_REGISTER(increment_1);
    uint32_t c0 = counter.word[0];
    uint32_t c1 = counter.word[1];
    uint32_t c2 = counter.word[2];
    uint32_t c3 = counter.word[3];
    for (int round = 0; round < smith_random_rounds - 1; round++) {
        SMITH_RANDOM_ROUND(c0, c1, c2, c3, key_0, key_1, multiplier_0, multiplier_1);
        key_0 += increment_0;
        key_1 += increment_1;
    }
    return (SmithRandomRounds){{c0, c1, c2, c3}, key_0, key_1};
}

// The whole block, with all ten rounds: the library draws through the two halves below, and the tests check this one
// against Random123's known answers.
[[maybe_unused]] static SmithRandomBlock smith_random_philox(SmithRandomBlock counter, uint32_t key_0, uint32_t key_1) {
    SmithRandomRounds r = smith_random_nine_rounds(counter, key_0, key_1);
    SMITH_RANDOM_ROUND(r.word[0], r.word[1], r.word[2], r.word[3], r.key_0, r.key_1, smith_random_multiplier_0,
                       smith_random_multiplier_1);
    return (SmithRandomBlock){{r.word[0], r.word[1], r.word[2], r.word[3]}};
}

// Output words 0 and 1 as one 64-bit value, which the tenth round makes from the second product alone.
static uint64_t smith_random_philox_low(SmithRandomBlock counter, uint32_t key_0, uint32_t key_1) {
    SmithRandomRounds r = smith_random_nine_rounds(counter, key_0, key_1);
    uint64_t product_1 = (uint64_t)smith_random_multiplier_1 * r.word[2];
    return (uint64_t)((uint32_t)(product_1 >> 32) ^ r.word[1] ^ r.key_0) | (product_1 << 32);
}

// Output words 2 and 3 as one 64-bit value, from the first product alone.
static uint64_t smith_random_philox_high(SmithRandomBlock counter, uint32_t key_0, uint32_t key_1) {
    SmithRandomRounds r = smith_random_nine_rounds(counter, key_0, key_1);
    uint64_t product_0 = (uint64_t)smith_random_multiplier_0 * r.word[0];
    return (uint64_t)((uint32_t)(product_0 >> 32) ^ r.word[3] ^ r.key_1) | (product_0 << 32);
}

SmithKey smith_key_make(uint64_t seed) {
    return (SmithKey){.bits = seed};
}

// Counter word 2 separates derive's domain (1) from bits' domain (0).
SmithKey smith_key_derive(SmithKey key, uint64_t i) {
    SmithRandomBlock counter = {{(uint32_t)i, (uint32_t)(i >> 32), 1, 0}};
    return (SmithKey){.bits = smith_random_philox_low(counter, (uint32_t)key.bits, (uint32_t)(key.bits >> 32))};
}

SmithRandomHook smith_random_hook = nullptr;

// Every distribution draws through here, so the runner's hook sees every word. smith_key_derive is not hooked: keys
// stay the same, and only words change.
uint64_t smith_key_bits(SmithKey key, uint64_t n) {
    uint64_t block = n >> 1;
    SmithRandomBlock counter = {{(uint32_t)block, (uint32_t)(block >> 32), 0, 0}};
    uint32_t key_0 = (uint32_t)key.bits;
    uint32_t key_1 = (uint32_t)(key.bits >> 32);
    uint64_t word =
        (n & 1) == 0 ? smith_random_philox_low(counter, key_0, key_1) : smith_random_philox_high(counter, key_0, key_1);
    return smith_random_hook != nullptr ? smith_random_hook(key, n, word) : word;
}

float smith_key_unit_float(SmithKey key, uint64_t n) {
    return (float)(smith_key_bits(key, n) >> 40) * 0x1p-24f;
}

double smith_key_unit_double(SmithKey key, uint64_t n) {
    return (double)(smith_key_bits(key, n) >> 11) * 0x1p-53;
}

// ---------------------------------------------------------------------------------------------------------------
// The logarithm: fdlibm's e_log.c and e_logf.c (Copyright (C) 1993 by Sun Microsystems, Inc., permission to use, copy,
// modify and distribute is granted provided this notice is preserved), in the FreeBSD msun version. The exponent is
// split off through the bits, then an odd polynomial in s = f / (2 + f) gives log(1 + f). No tables.

static uint64_t smith_random_double_bits(double x) {
    uint64_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    return bits;
}

static double smith_random_bits_double(uint64_t bits) {
    double x = 0;
    memcpy(&x, &bits, sizeof x);
    return x;
}

static uint32_t smith_random_float_bits(float x) {
    uint32_t bits = 0;
    memcpy(&bits, &x, sizeof bits);
    return bits;
}

static float smith_random_bits_float(uint32_t bits) {
    float x = 0;
    memcpy(&x, &bits, sizeof x);
    return x;
}

constexpr double smith_log_double_ln2_hi = 6.93147180369123816490e-01; // 3fe62e42 fee00000
constexpr double smith_log_double_ln2_lo = 1.90821492927058770002e-10; // 3dea39ef 35793c76
constexpr double smith_log_double_two54 = 1.80143985094819840000e+16;  // 2^54
constexpr double smith_log_double_lg1 = 6.666666666666735130e-01;
constexpr double smith_log_double_lg2 = 3.999999999940941908e-01;
constexpr double smith_log_double_lg3 = 2.857142874366239149e-01;
constexpr double smith_log_double_lg4 = 2.222219843214978396e-01;
constexpr double smith_log_double_lg5 = 1.818357216161805012e-01;
constexpr double smith_log_double_lg6 = 1.531383769920937332e-01;
constexpr double smith_log_double_lg7 = 1.479819860511658591e-01;

// log(1 + f) for f in the range after reduction, with the exponent k added back.
static double smith_log_double_finish(double f, int32_t k, uint32_t hx) {
    double s = f / (2.0 + f);
    double dk = (double)k;
    double z = smith_random_mul_double(s, s);
    double w = smith_random_mul_double(z, z);
    uint32_t mixed =
        (hx - 0x6147a) | (0x6b851 - hx); // positive as a signed value: the range where log(1+f) = f - hfsq + ...
    static const double even[] = {smith_log_double_lg2, smith_log_double_lg4, smith_log_double_lg6};
    static const double odd[] = {smith_log_double_lg1, smith_log_double_lg3, smith_log_double_lg5,
                                 smith_log_double_lg7};
    double t1 = smith_random_mul_double(w, smith_random_horner_double(even, 3, w));
    double t2 = smith_random_mul_double(z, smith_random_horner_double(odd, 4, w));
    double r = t2 + t1;
    if (mixed != 0 && mixed < 0x80000000U) {
        double hfsq = smith_random_mul_double(smith_random_mul_double(0.5, f), f);
        double inner = smith_random_mul_double(s, hfsq + r);
        if (k == 0) return f - (hfsq - inner);
        return smith_random_mul_double(dk, smith_log_double_ln2_hi) -
               ((hfsq - (inner + smith_random_mul_double(dk, smith_log_double_ln2_lo))) - f);
    }
    double inner = smith_random_mul_double(s, f - r);
    if (k == 0) return f - inner;
    return smith_random_mul_double(dk, smith_log_double_ln2_hi) -
           ((inner - smith_random_mul_double(dk, smith_log_double_ln2_lo)) - f);
}

double smith_log_double(double x) {
    uint64_t bits = smith_random_double_bits(x);
    uint32_t hx = (uint32_t)(bits >> 32); // fdlibm's signed hx is negative when hx >= 0x80000000 here
    uint32_t lx = (uint32_t)bits;
    int32_t k = 0;
    if (hx < 0x00100000 || hx >= 0x80000000U) { // x < 2^-1022, or negative
        if (((hx & 0x7fffffffU) | lx) == 0) return -(double)INFINITY;
        if (hx >= 0x80000000U) return (double)NAN;
        k -= 54; // subnormal: scale up
        x *= smith_log_double_two54;
        bits = smith_random_double_bits(x);
        hx = (uint32_t)(bits >> 32);
    }
    if (hx >= 0x7ff00000) return x + x; // inf or NaN
    k += (int32_t)(hx >> 20) - 1023;
    hx &= 0x000fffff;
    uint32_t half = (hx + 0x95f64) & 0x100000; // 0x100000 when x is above sqrt(2): use x/2
    bits = (bits & 0xffffffffULL) | ((uint64_t)(hx | (half ^ 0x3ff00000)) << 32); // x or x/2, in [sqrt(2)/2, sqrt(2))
    x = smith_random_bits_double(bits);
    k += (int32_t)(half >> 20);
    double f = x - 1.0;
    if ((0x000fffff & (2 + hx)) < 3) { // -2^-20 <= f < 2^-20
        if (f == 0.0) {
            if (k == 0) return 0.0;
            double dk = (double)k;
            return smith_random_mul_double(dk, smith_log_double_ln2_hi) +
                   smith_random_mul_double(dk, smith_log_double_ln2_lo);
        }
        double r = smith_random_mul_double(smith_random_mul_double(f, f),
                                           0.5 - smith_random_mul_double(0.33333333333333333, f));
        if (k == 0) return f - r;
        double dk = (double)k;
        return smith_random_mul_double(dk, smith_log_double_ln2_hi) -
               ((r - smith_random_mul_double(dk, smith_log_double_ln2_lo)) - f);
    }
    return smith_log_double_finish(f, k, hx);
}

constexpr float smith_log_float_ln2_hi = 6.9313812256e-01f; // 0x3f317180
constexpr float smith_log_float_ln2_lo = 9.0580006145e-06f; // 0x3717f7d1
constexpr float smith_log_float_two25 = 3.355443200e+07f;   // 2^25
constexpr float smith_log_float_lg1 = 0xaaaaaa.0p-24f;      // 0.66666662693
constexpr float smith_log_float_lg2 = 0xccce13.0p-25f;      // 0.40000972152
constexpr float smith_log_float_lg3 = 0x91e9ee.0p-25f;      // 0.28498786688
constexpr float smith_log_float_lg4 = 0xf89e26.0p-26f;      // 0.24279078841

static float smith_log_float_finish(float f, int32_t k, uint32_t ix) {
    float s = f / (2.0f + f);
    float dk = (float)k;
    float z = smith_random_mul_float(s, s);
    float w = smith_random_mul_float(z, z);
    uint32_t mixed = (ix - (0x6147aU << 3)) | ((0x6b851U << 3) - ix);
    float t1 = smith_random_mul_float(w, smith_log_float_lg2 + smith_random_mul_float(w, smith_log_float_lg4));
    float t2 = smith_random_mul_float(z, smith_log_float_lg1 + smith_random_mul_float(w, smith_log_float_lg3));
    float r = t2 + t1;
    if (mixed != 0 && mixed < 0x80000000U) {
        float hfsq = smith_random_mul_float(smith_random_mul_float(0.5f, f), f);
        float inner = smith_random_mul_float(s, hfsq + r);
        if (k == 0) return f - (hfsq - inner);
        return smith_random_mul_float(dk, smith_log_float_ln2_hi) -
               ((hfsq - (inner + smith_random_mul_float(dk, smith_log_float_ln2_lo))) - f);
    }
    float inner = smith_random_mul_float(s, f - r);
    if (k == 0) return f - inner;
    return smith_random_mul_float(dk, smith_log_float_ln2_hi) -
           ((inner - smith_random_mul_float(dk, smith_log_float_ln2_lo)) - f);
}

float smith_log_float(float x) {
    uint32_t ix = smith_random_float_bits(x);
    int32_t k = 0;
    if (ix < 0x00800000 || ix >= 0x80000000U) { // x < 2^-126, or negative
        if ((ix & 0x7fffffffU) == 0) return -INFINITY;
        if (ix >= 0x80000000U) return NAN;
        k -= 25; // subnormal: scale up
        x *= smith_log_float_two25;
        ix = smith_random_float_bits(x);
    }
    if (ix >= 0x7f800000) return x + x; // inf or NaN
    k += (int32_t)(ix >> 23) - 127;
    ix &= 0x007fffff;
    uint32_t half = (ix + (0x95f64U << 3)) & 0x800000; // 0x800000 when x is above sqrt(2): use x/2
    x = smith_random_bits_float(ix | (half ^ 0x3f800000U));
    k += (int32_t)(half >> 23);
    float f = x - 1.0f;
    if ((0x007fffffU & (0x8000U + ix)) < 0xc000U) { // -2^-9 <= f < 2^-9
        if (f == 0.0f) {
            if (k == 0) return 0.0f;
            float dk = (float)k;
            return smith_random_mul_float(dk, smith_log_float_ln2_hi) +
                   smith_random_mul_float(dk, smith_log_float_ln2_lo);
        }
        float r = smith_random_mul_float(smith_random_mul_float(f, f),
                                         0.5f - smith_random_mul_float(0.33333333333333333f, f));
        if (k == 0) return f - r;
        float dk = (float)k;
        return smith_random_mul_float(dk, smith_log_float_ln2_hi) -
               ((r - smith_random_mul_float(dk, smith_log_float_ln2_lo)) - f);
    }
    return smith_log_float_finish(f, k, ix);
}

// ---------------------------------------------------------------------------------------------------------------
// Zigzag decoding: 0, 1, 2, 3, 4... give 0, -1, 1, -2, 2... so a word moves away from 0 as it grows.
static int64_t smith_random_zigzag(uint64_t word) {
    int64_t half = (int64_t)(word >> 1);
    return (word & 1) != 0 ? -half - 1 : half;
}

// The position in [0, count) that comes index-th when the positions are ordered by their distance from origin: origin,
// origin - 1, origin + 1, origin - 2, origin + 2... until one side runs out, then the rest of the other side in order.
// A bijection of [0, count), so a uniform index gives a uniform position, and index 0 gives the origin.
static uint64_t smith_random_around(uint64_t index, uint64_t origin, uint64_t count) {
    assert(origin < count && index < count);
    uint64_t below = origin;
    uint64_t above = count - 1 - origin;
    uint64_t both = below < above ? below : above; // positions come in pairs this many times
    if (index <= 2 * both) return origin + (uint64_t)smith_random_zigzag(index);
    return below > above ? origin - (index - both) : origin + (index - both);
}

// ---------------------------------------------------------------------------------------------------------------
// An index below n, unbiased: Lemire's multiply-and-reject, "Fast Random Integer Generation in an Interval", 2019. The
// high half of bits * n is the index, so the index grows with the word and word 0 gives index 0. Each index owns a run
// of words, and a run is one word longer than the others when 2^64 mod n is not 0; that extra word is the one with the
// largest low half, and it is rejected (Lemire rejects the one with the smallest, which would reject word 0 and spend
// the next draw; this rejects the same number of words, so the index is as uniform). A rejection has probability below
// 1/2. Attempt k uses bits k of the key. Needing all 64 attempts has probability below 2^-64; after the 64th the last
// candidate is returned, which is then biased by less than that.

constexpr uint64_t smith_random_attempts = 64;

static uint64_t smith_random_index(SmithKey key, uint64_t n) {
    assert(n > 0);
    uint64_t threshold = (0 - n) % n; // 2^64 mod n
    uint64_t index = 0;
    for (uint64_t attempt = 0; attempt < smith_random_attempts; attempt++) {
        unsigned _BitInt(128) product = (unsigned _BitInt(128))smith_key_bits(key, attempt) * n;
        index = (uint64_t)(product >> 64);
        if (threshold == 0 || (uint64_t)product < 0 - threshold) break;
    }
    return index;
}

// The value that comes index-th, in [low, high], ordered by distance from 0 clamped into the range. span is high - low
// + 1, which is not 0 here.
static int64_t smith_random_int_value(int64_t low, int64_t high, uint64_t index) {
    uint64_t span = (uint64_t)high - (uint64_t)low + 1;
    uint64_t origin = 0; // the position of 0, clamped
    if (high < 0) {
        origin = span - 1;
    } else if (low <= 0) {
        origin = 0 - (uint64_t)low;
    }
    return (int64_t)((uint64_t)low + smith_random_around(index, origin, span));
}

int64_t smith_uniform_int64_sample(SmithKey key, SmithUniformInt64 uniform) {
    assert(uniform.low <= uniform.high);
    uint64_t span = (uint64_t)uniform.high - (uint64_t)uniform.low + 1;
    if (span == 0) return smith_random_zigzag(smith_key_bits(key, 0)); // the whole int64_t range
    return smith_random_int_value(uniform.low, uniform.high, smith_random_index(key, span));
}

// ---------------------------------------------------------------------------------------------------------------
// Real uniforms: low + (high - low) * unit, which rounding can push up to high; that value becomes the largest one
// below high, found from the bits (no nextafter).

static float smith_random_below_float(float high) {
    uint32_t bits = smith_random_float_bits(high);
    if (high > 0.0f) return smith_random_bits_float(bits - 1);
    if (high == 0.0f) return smith_random_bits_float(0x80000001U); // -the smallest subnormal
    return smith_random_bits_float(bits + 1);
}

static double smith_random_below_double(double high) {
    uint64_t bits = smith_random_double_bits(high);
    if (high > 0.0) return smith_random_bits_double(bits - 1);
    if (high == 0.0) return smith_random_bits_double(0x8000000000000001ULL);
    return smith_random_bits_double(bits + 1);
}

// The grid index nearest 0: where the zigzag starts. 0 when low >= 0, the last index when high <= 0.
static uint64_t smith_random_origin_float(float low, float high) {
    constexpr uint64_t last = ((uint64_t)1 << 24) - 1;
    if (low >= 0.0f) return 0;
    if (high <= 0.0f) return last;
    float scaled = smith_random_mul_float(-low / (high - low), 0x1p24f);
    uint64_t origin = (uint64_t)(scaled + 0.5f);
    return origin < last ? origin : last;
}

static uint64_t smith_random_origin_double(double low, double high) {
    constexpr uint64_t last = ((uint64_t)1 << 53) - 1;
    if (low >= 0.0) return 0;
    if (high <= 0.0) return last;
    double scaled = smith_random_mul_double(-low / (high - low), 0x1p53);
    uint64_t origin = (uint64_t)(scaled + 0.5);
    return origin < last ? origin : last;
}

float smith_uniform_float_sample(SmithKey key, SmithUniformFloat uniform) {
    assert(isfinite(uniform.low) && isfinite(uniform.high) && uniform.low < uniform.high);
    assert(isfinite(uniform.high - uniform.low));
    uint64_t origin = smith_random_origin_float(uniform.low, uniform.high);
    uint64_t grid = smith_random_around(smith_key_bits(key, 0) >> 40, origin, (uint64_t)1 << 24);
    float value = uniform.low + smith_random_mul_float(uniform.high - uniform.low, (float)grid * 0x1p-24f);
    return value < uniform.high ? value : smith_random_below_float(uniform.high);
}

double smith_uniform_double_sample(SmithKey key, SmithUniformDouble uniform) {
    assert(isfinite(uniform.low) && isfinite(uniform.high) && uniform.low < uniform.high);
    assert(isfinite(uniform.high - uniform.low));
    uint64_t origin = smith_random_origin_double(uniform.low, uniform.high);
    uint64_t grid = smith_random_around(smith_key_bits(key, 0) >> 11, origin, (uint64_t)1 << 53);
    double value = uniform.low + smith_random_mul_double(uniform.high - uniform.low, (double)grid * 0x1p-53);
    return value < uniform.high ? value : smith_random_below_double(uniform.high);
}

// ---------------------------------------------------------------------------------------------------------------
// The normal distribution by inverse CDF: Michael J. Wichura, "The Percentage Points of the Normal Distribution",
// Algorithm AS 241, Applied Statistics 37 (3), 1988, pages 477-484. PPND16 (about 1e-16 relative) is in double, with
// the coefficients as in R's src/nmath/qnorm.c; PPND7 (about 1e-7) is in float, with the paper's coefficients. The
// probability comes from a midpoint grid, so it is never 0 or 1 and p and 1 - p are exact mirrors: the results are
// exact negatives. Reach: the smallest p is 2^-53 (about 8.2 deviations) for double and 2^-24 (about 5.3) for float.

// The coefficients of AS241's rational approximations, lowest power first, as in R's qnorm.c: the centre in r =
// 0.180625
// - q^2, then the tails in r = sqrt(-log(p)) - 1.6 (p above exp(-25)) and r - 5 beyond.
static const double smith_random_ppnd16_a[] = {3.387132872796366608,  133.14166789178437745, 1971.5909503065514427,
                                               13731.693765509461125, 45921.953931549871457, 67265.770927008700853,
                                               33430.575583588128105, 2509.0809287301226727};
static const double smith_random_ppnd16_b[] = {1.0,
                                               42.313330701600911252,
                                               687.1870074920579083,
                                               5394.1960214247511077,
                                               21213.794301586595867,
                                               39307.89580009271061,
                                               28729.085735721942674,
                                               5226.495278852854561};
static const double smith_random_ppnd16_c[] = {
    1.42343711074968357734, 4.6303378461565452959,  5.7694972214606914055,    3.64784832476320460504,
    1.27045825245236838258, 0.24178072517745061177, 0.0227238449892691845833, 7.7454501427834140764e-4};
static const double smith_random_ppnd16_d[] = {1.0,
                                               2.05319162663775882187,
                                               1.6763848301838038494,
                                               0.68976733498510000455,
                                               0.14810397642748007459,
                                               0.0151986665636164571966,
                                               5.475938084995344946e-4,
                                               1.05075007164441684324e-9};
static const double smith_random_ppnd16_e[] = {
    6.6579046435011037772,   5.4637849111641143699,    1.7848265399172913358,     0.29656057182850489123,
    0.026532189526576123093, 0.0012426609473880784386, 2.71155556874348757815e-5, 2.01033439929228813265e-7};
static const double smith_random_ppnd16_f[] = {1.0,
                                               0.59983220655588793769,
                                               0.13692988092273580531,
                                               0.0148753612908506148525,
                                               7.868691311456132591e-4,
                                               1.8463183175100546818e-5,
                                               1.4215117583164458887e-7,
                                               2.04426310338993978564e-15};

static double smith_random_ppnd16(double p) {
    double q = p - 0.5;
    if (fabs(q) <= 0.425) {
        double r = 0.180625 - smith_random_mul_double(q, q);
        return smith_random_mul_double(q, smith_random_horner_double(smith_random_ppnd16_a, 8, r)) /
               smith_random_horner_double(smith_random_ppnd16_b, 8, r);
    }
    double tail = q < 0 ? p : 1.0 - p;
    double r = sqrt(-smith_log_double(tail));
    double value = 0;
    if (r <= 5.0) {
        r += -1.6;
        value = smith_random_horner_double(smith_random_ppnd16_c, 8, r) /
                smith_random_horner_double(smith_random_ppnd16_d, 8, r);
    } else {
        r += -5.0;
        value = smith_random_horner_double(smith_random_ppnd16_e, 8, r) /
                smith_random_horner_double(smith_random_ppnd16_f, 8, r);
    }
    return q < 0 ? -value : value;
}

// PPND7: the paper's float coefficients, lowest power first (c and d for the tail to r = 5 and e and f beyond it, which
// the grid never reaches).
static const float smith_random_ppnd7_a[] = {3.3871327179f, 50.434271938f, 159.29113202f, 59.109374720f};
static const float smith_random_ppnd7_b[] = {1.0f, 17.895169469f, 78.757757664f, 67.187563600f};
static const float smith_random_ppnd7_c[] = {1.4234372777f, 2.7568153900f, 1.3067284816f, 0.17023821103f};
static const float smith_random_ppnd7_d[] = {1.0f, 0.73700164250f, 0.12021132975f};
static const float smith_random_ppnd7_e[] = {6.6579051150f, 3.0812263860f, 0.42868294337f, 0.017337203997f};
static const float smith_random_ppnd7_f[] = {1.0f, 0.24197894225f, 0.012258202635f};

static float smith_random_ppnd7(float p) {
    float q = p - 0.5f;
    if (fabsf(q) <= 0.425f) {
        float r = 0.180625f - smith_random_mul_float(q, q);
        return smith_random_mul_float(q, smith_random_horner_float(smith_random_ppnd7_a, 4, r)) /
               smith_random_horner_float(smith_random_ppnd7_b, 4, r);
    }
    float tail = q < 0 ? p : 1.0f - p;
    float r = sqrtf(-smith_log_float(tail));
    float value = 0;
    if (r <= 5.0f) {
        r -= 1.6f;
        value = smith_random_horner_float(smith_random_ppnd7_c, 4, r) /
                smith_random_horner_float(smith_random_ppnd7_d, 3, r);
    } else { // not reached by the grid, which stops at 2^-24; kept so the function is AS241's
        r -= 5.0f;
        value = smith_random_horner_float(smith_random_ppnd7_e, 4, r) /
                smith_random_horner_float(smith_random_ppnd7_f, 3, r);
    }
    return q < 0 ? -value : value;
}

// The midpoint-grid index is ordered by distance from the centre, so word 0 gives the u just above 0.5 (about the
// mean), word 1 the u just below it, and so on outwards. Only which word gives which u changes, not the u's.
static double smith_random_normal_u_double(SmithKey key) {
    uint64_t grid = smith_random_around(smith_key_bits(key, 0) >> 12, (uint64_t)1 << 51, (uint64_t)1 << 52);
    return (double)(grid * 2 + 1) * 0x1p-53;
}

static float smith_random_normal_u_float(SmithKey key) {
    uint64_t grid = smith_random_around(smith_key_bits(key, 0) >> 41, (uint64_t)1 << 22, (uint64_t)1 << 23);
    return (float)(grid * 2 + 1) * 0x1p-24f;
}

double smith_normal_double_sample(SmithKey key, SmithNormalDouble normal) {
    assert(isfinite(normal.mean) && isfinite(normal.deviation) && normal.deviation > 0);
    return normal.mean +
           smith_random_mul_double(normal.deviation, smith_random_ppnd16(smith_random_normal_u_double(key)));
}

float smith_normal_float_sample(SmithKey key, SmithNormalFloat normal) {
    assert(isfinite(normal.mean) && isfinite(normal.deviation) && normal.deviation > 0);
    return normal.mean + smith_random_mul_float(normal.deviation, smith_random_ppnd7(smith_random_normal_u_float(key)));
}

// ---------------------------------------------------------------------------------------------------------------

bool smith_bernoulli_sample(SmithKey key, SmithBernoulli bernoulli) {
    assert(bernoulli.probability >= 0.0 && bernoulli.probability <= 1.0);
    uint64_t grid = smith_key_bits(key, 0) >> 11; // the unit's grid index; reversed, so word 0 gives false
    return (double)(((uint64_t)1 << 53) - 1 - grid) * 0x1p-53 < bernoulli.probability;
}

// Vose's alias method, "A Linear Algorithm For Generating Random Numbers With a Given Distribution", 1991. Each index
// holds a chance of being kept and an alias to take otherwise. Indices with scaled weight under 1 (small) are paired
// with ones over 1 (large); the stacks are in a scope that ends before the table is returned.
static void smith_random_alias_build(double *probability, size_t *alias, size_t *small, size_t *large, size_t count) {
    size_t small_count = 0;
    size_t large_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (probability[i] < 1.0) {
            small[small_count++] = i;
        } else {
            large[large_count++] = i;
        }
    }
    while (small_count > 0 && large_count > 0) { // each pass finishes one index, so at most count passes
        size_t less = small[--small_count];
        size_t more = large[--large_count];
        alias[less] = more;
        probability[more] = (probability[more] + probability[less]) - 1.0;
        if (probability[more] < 1.0) {
            small[small_count++] = more;
        } else {
            large[large_count++] = more;
        }
    }
    while (large_count > 0) { // what is left has a chance of 1 (up to rounding)
        size_t index = large[--large_count];
        probability[index] = 1.0;
        alias[index] = index;
    }
    while (small_count > 0) {
        size_t index = small[--small_count];
        probability[index] = 1.0;
        alias[index] = index;
    }
}

SmithCategorical smith_categorical_make(SmithArena *arena, const double *weights, size_t count) {
    assert(arena != nullptr && weights != nullptr && count >= 1);
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        assert(isfinite(weights[i]) && weights[i] >= 0.0);
        sum += weights[i];
    }
    assert(isfinite(sum) && sum > 0.0);
    double *probability = SMITH_ARENA_PUSH_ARRAY(arena, double, count);
    size_t *alias = SMITH_ARENA_PUSH_ARRAY(arena, size_t, count);
    for (size_t i = 0; i < count; i++) {
        probability[i] = weights[i] * (double)count / sum;
        alias[i] = i;
    }
    SmithArenaScope scope = smith_arena_scope_begin(arena);
    size_t *small = SMITH_ARENA_PUSH_ARRAY(arena, size_t, count);
    size_t *large = SMITH_ARENA_PUSH_ARRAY(arena, size_t, count);
    smith_random_alias_build(probability, alias, small, large, count);
    smith_arena_scope_end(scope);
    return (SmithCategorical){.probability = probability, .alias = alias, .count = count};
}

size_t smith_categorical_sample(SmithKey key, SmithCategorical categorical) {
    assert(categorical.count >= 1);
    size_t index = (size_t)smith_random_index(smith_key_derive(key, 0), categorical.count);
    bool keep = smith_key_unit_double(smith_key_derive(key, 1), 0) < categorical.probability[index];
    return keep ? index : categorical.alias[index];
}

void smith_permutation_sample(SmithKey key, SmithPermutation permutation, size_t *out) {
    assert(out != nullptr || permutation.count == 0);
    for (size_t i = 0; i < permutation.count; i++)
        out[i] = i;
    for (size_t i = permutation.count; i > 1; i--) { // step i swaps position i - 1 with a position in [0, i)
        size_t j = i - 1 - (size_t)smith_random_index(smith_key_derive(key, i - 1), i); // index 0 swaps with itself
        size_t swap = out[i - 1];
        out[i - 1] = out[j];
        out[j] = swap;
    }
}
