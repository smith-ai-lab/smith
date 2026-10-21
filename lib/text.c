#include <smith/text.h>

#include <assert.h>
#include <float.h>
#include <stdint.h>

SmithText smith_text_make(const char *string) {
    assert(string != nullptr);
    size_t length = 0;
    while (string[length] != '\0') {
        length += 1;
    }
    return (SmithText){string, length};
}

static bool smith_text_is_space(char c) {
    return c == ' ' || c == '\t';
}

bool smith_text_take_line(SmithText *text, SmithText *line) {
    assert(text != nullptr && line != nullptr);
    if (text->length == 0) return false;
    size_t length = 0;
    while (length < text->length && text->begin[length] != '\n') {
        length += 1;
    }
    *line = (SmithText){text->begin, length};
    size_t consumed = length < text->length ? length + 1 : length; // the newline too
    text->begin += consumed;
    text->length -= consumed;
    size_t comment = 0;
    while (comment < line->length && line->begin[comment] != '#') {
        comment += 1;
    }
    line->length = comment;
    while (line->length > 0 &&
           (smith_text_is_space(line->begin[line->length - 1]) || line->begin[line->length - 1] == '\r')) {
        line->length -= 1;
    }
    return true;
}

bool smith_text_take_word(SmithText *line, SmithText *word) {
    assert(line != nullptr && word != nullptr);
    while (line->length > 0 && smith_text_is_space(line->begin[0])) {
        line->begin += 1;
        line->length -= 1;
    }
    if (line->length == 0) return false;
    size_t length = 0;
    while (length < line->length && !smith_text_is_space(line->begin[length])) {
        length += 1;
    }
    *word = (SmithText){line->begin, length};
    line->begin += length;
    line->length -= length;
    return true;
}

bool smith_text_is(SmithText word, const char *string) {
    assert(string != nullptr);
    for (size_t i = 0; i < word.length; i++) {
        if (string[i] != word.begin[i]) return false; // a shorter string ends in its terminator, which no word has
    }
    return string[word.length] == '\0';
}

// ---------------------------------------------------------------------------------------------------------------
// Whole numbers

bool smith_text_parse_u64(SmithText word, uint64_t *value) {
    assert(value != nullptr);
    if (word.length == 0) return false;
    uint64_t result = 0;
    for (size_t i = 0; i < word.length; i++) {
        char c = word.begin[i];
        if (c < '0' || c > '9') return false;
        uint64_t digit = (uint64_t)(c - '0');
        if (result > (UINT64_MAX - digit) / 10) return false;
        result = result * 10 + digit;
    }
    *value = result;
    return true;
}

bool smith_text_parse_i64(SmithText word, int64_t *value) {
    assert(value != nullptr);
    bool negative = word.length > 0 && word.begin[0] == '-';
    SmithText digits = negative ? (SmithText){word.begin + 1, word.length - 1} : word;
    uint64_t magnitude = 0;
    if (!smith_text_parse_u64(digits, &magnitude)) return false;
    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : (uint64_t)INT64_MAX;
    if (magnitude > limit) return false;
    *value = negative ? (int64_t)(0 - magnitude) : (int64_t)magnitude;
    return true;
}

bool smith_text_parse_hex(SmithText word, uint64_t *value) {
    assert(value != nullptr);
    if (word.length == 0) return false;
    uint64_t result = 0;
    for (size_t i = 0; i < word.length; i++) {
        char c = word.begin[i];
        uint64_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = (uint64_t)(c - 'a') + 10;
        else
            return false;
        if ((result >> 60) != 0) return false; // another digit would push a one out of the top
        result = (result << 4) | digit;
    }
    *value = result;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Decimal to float or double, correctly rounded. A decimal is digits D (an integer) times 10^exp10. Two paths give the
// nearest value:
//  - the fast path, for D below 10^15 and a power of ten up to 22, where D and the power are exact doubles, so one
//    multiply or divide is the double nearest the number. For a double that is the answer; for a float, rounding it
//    again is right unless it sits exactly halfway between two floats (which it hands to the slow path);
//  - the slow path, exact: the number as a ratio of two big integers, divided to p + 2 bits and rounded by hand.
// Digits past the first 800 cannot change a result: a double, or the point halfway between two, has at most 767
// significant digits (a float's, 115), so any longer decimal differs from all of them, and the value between D and
// D + 1 rounds like D followed by a 1, which is what a nonzero tail becomes.

constexpr size_t smith_text_digits_max = 800;
constexpr size_t smith_text_big_limbs = 128; // 4096 bits; the largest number below is about 3800, for a double
constexpr int64_t smith_text_exponent_max = 1'000'000;
constexpr size_t smith_text_fast_digits_max = 15;
constexpr int64_t smith_text_fast_exponent_max = 22;
constexpr uint32_t smith_text_chunk_digits = 9;

// What differs between the two formats the slow path rounds to.
typedef struct {
    uint32_t mantissa_bits;    // 24 for a float, 53 for a double
    int64_t smallest_exponent; // the last place of a subnormal is 2^smallest_exponent
    int64_t field_offset;      // the exponent field of kept * 2^e is e + field_offset
    int64_t field_infinity;    // the field of infinity
} SmithTextFormat;

constexpr uint32_t smith_text_infinity_bits = 0x7f800000;
constexpr uint64_t smith_text_double_infinity_bits = 0x7ff0000000000000;

static const SmithTextFormat smith_text_float = {24, -149, 150, 255};
static const SmithTextFormat smith_text_double = {53, -1074, 1075, 2047};

static const double smith_text_powers[smith_text_fast_exponent_max + 1] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
};

static const uint32_t smith_text_powers_u32[smith_text_chunk_digits + 1] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
};

// An unsigned integer of up to 1024 bits, little-endian in 32-bit limbs. `count` limbs are in use, the top one nonzero.
typedef struct {
    uint32_t limb[smith_text_big_limbs];
    size_t count;
} SmithTextBig;

// big = big * multiplier + addend.
static void smith_text_big_mul_add(SmithTextBig *big, uint32_t multiplier, uint32_t addend) {
    uint64_t carry = addend;
    for (size_t i = 0; i < big->count; i++) {
        uint64_t product = (uint64_t)big->limb[i] * multiplier + carry;
        big->limb[i] = (uint32_t)product;
        carry = product >> 32;
    }
    if (carry != 0) {
        assert(big->count < smith_text_big_limbs);
        big->limb[big->count] = (uint32_t)carry;
        big->count += 1;
    }
}

// big = big * 10^power.
static void smith_text_big_mul_pow10(SmithTextBig *big, uint64_t power) {
    while (power >= smith_text_chunk_digits) {
        smith_text_big_mul_add(big, smith_text_powers_u32[smith_text_chunk_digits], 0);
        power -= smith_text_chunk_digits;
    }
    smith_text_big_mul_add(big, smith_text_powers_u32[power], 0);
}

static uint64_t smith_text_big_bits(const SmithTextBig *big) {
    if (big->count == 0) return 0;
    uint32_t top = big->limb[big->count - 1];
    uint64_t bits = (uint64_t)(big->count - 1) * 32;
    while (top != 0) {
        bits += 1;
        top >>= 1;
    }
    return bits;
}

static void smith_text_big_shift_left(SmithTextBig *big, uint64_t shift) {
    if (big->count == 0 || shift == 0) return;
    size_t whole = (size_t)(shift / 32);
    uint32_t part = (uint32_t)(shift % 32);
    assert(big->count + whole + 1 <= smith_text_big_limbs);
    big->limb[big->count + whole] = 0;
    for (size_t i = big->count; i-- > 0;) {
        uint64_t moved = (uint64_t)big->limb[i] << part;
        big->limb[i + whole + 1] |= (uint32_t)(moved >> 32);
        big->limb[i + whole] = (uint32_t)moved;
    }
    for (size_t i = 0; i < whole; i++) {
        big->limb[i] = 0;
    }
    big->count += whole + 1;
    while (big->count > 0 && big->limb[big->count - 1] == 0) {
        big->count -= 1;
    }
}

static void smith_text_big_shift_right_one(SmithTextBig *big) {
    for (size_t i = 0; i < big->count; i++) {
        uint32_t above = i + 1 < big->count ? big->limb[i + 1] : 0;
        big->limb[i] = (big->limb[i] >> 1) | (above << 31);
    }
    while (big->count > 0 && big->limb[big->count - 1] == 0) {
        big->count -= 1;
    }
}

static bool smith_text_big_at_least(const SmithTextBig *a, const SmithTextBig *b) {
    if (a->count != b->count) return a->count > b->count;
    for (size_t i = a->count; i-- > 0;) {
        if (a->limb[i] != b->limb[i]) return a->limb[i] > b->limb[i];
    }
    return true;
}

// a = a - b, where a >= b.
static void smith_text_big_subtract(SmithTextBig *a, const SmithTextBig *b) {
    uint64_t borrow = 0;
    for (size_t i = 0; i < a->count; i++) {
        uint64_t take = (i < b->count ? b->limb[i] : 0) + borrow;
        borrow = a->limb[i] < take ? 1 : 0;
        a->limb[i] = (uint32_t)((uint64_t)a->limb[i] - take);
    }
    assert(borrow == 0);
    while (a->count > 0 && a->limb[a->count - 1] == 0) {
        a->count -= 1;
    }
}

// The bits nearest to numerator / denominator in a format, ties to even, or the infinity bits if that is out of range.
static uint64_t smith_text_ratio_to_bits(const SmithTextFormat *format, SmithTextBig numerator,
                                         SmithTextBig denominator) {
    // Scale so that numerator / denominator, times 2^scale, lies in (2^p, 2^(p+2)) for p mantissa bits: its integer part
    // q has p + 1 or p + 2 bits, and the remainder says what is left of the last one.
    int64_t p = format->mantissa_bits;
    int64_t scale = p + 1 - ((int64_t)smith_text_big_bits(&numerator) - (int64_t)smith_text_big_bits(&denominator));
    if (scale >= 0)
        smith_text_big_shift_left(&numerator, (uint64_t)scale);
    else
        smith_text_big_shift_left(&denominator, (uint64_t)(0 - scale));
    SmithTextBig step = denominator;
    smith_text_big_shift_left(&step, (uint64_t)(p + 1));
    uint64_t q = 0;
    for (int64_t bit = p + 2; bit-- > 0;) {
        if (smith_text_big_at_least(&numerator, &step)) {
            smith_text_big_subtract(&numerator, &step);
            q |= (uint64_t)1 << bit;
        }
        smith_text_big_shift_right_one(&step);
    }
    // The number is (q + numerator / denominator) * 2^-scale. Keep p bits, or fewer where the result is subnormal,
    // and round by the bits dropped.
    int64_t drop = (q >> (p + 1)) != 0 ? 2 : 1;
    if (scale + format->smallest_exponent > drop) drop = scale + format->smallest_exponent;
    if (drop > p + 2) return 0;
    uint64_t shift = (uint64_t)drop;
    uint64_t kept = q >> shift;
    uint64_t low = q & (((uint64_t)1 << shift) - 1);
    uint64_t half = (uint64_t)1 << (shift - 1);
    if (low > half || (low == half && (numerator.count != 0 || (kept & 1) != 0))) kept += 1;
    int64_t exponent = drop - scale; // the number is kept * 2^exponent
    uint64_t one = (uint64_t)1 << (p - 1);
    if (exponent + format->field_offset >= format->field_infinity) return (uint64_t)format->field_infinity * one;
    // A normal number's top bit carries into the exponent field, which also rounds 2^p up to the next power of two.
    return (uint64_t)(exponent + format->field_offset) * one + kept - one;
}

// What the scan of a decimal found.
typedef struct {
    SmithTextBig digits;
    uint64_t small; // the digits as an integer, good while there are at most 19
    size_t significant;
    uint32_t chunk;
    uint32_t chunk_length;
    int64_t exp10;
    bool sticky; // nonzero digits were dropped past the 800th
} SmithTextDecimal;

// An empty decimal. The limbs are left alone: only the first `count` of them are ever read, and zeroing 512 bytes
// would cost more than the whole parse of a short number.
static void smith_text_decimal_init(SmithTextDecimal *decimal) {
    decimal->digits.count = 0;
    decimal->small = 0;
    decimal->significant = 0;
    decimal->chunk = 0;
    decimal->chunk_length = 0;
    decimal->exp10 = 0;
    decimal->sticky = false;
}

static void smith_text_decimal_add_digit(SmithTextDecimal *decimal, uint32_t digit, bool in_fraction) {
    if (decimal->significant == 0 && digit == 0) { // a leading zero adds nothing but a place
        if (in_fraction) decimal->exp10 -= 1;
        return;
    }
    if (decimal->significant >= smith_text_digits_max) {
        if (digit != 0) decimal->sticky = true;
        if (!in_fraction) decimal->exp10 += 1;
        return;
    }
    decimal->significant += 1;
    decimal->small = decimal->small * 10 + digit;
    decimal->chunk = decimal->chunk * 10 + digit;
    decimal->chunk_length += 1;
    if (decimal->chunk_length == smith_text_chunk_digits) {
        smith_text_big_mul_add(&decimal->digits, smith_text_powers_u32[smith_text_chunk_digits], decimal->chunk);
        decimal->chunk = 0;
        decimal->chunk_length = 0;
    }
    if (in_fraction) decimal->exp10 -= 1;
}

// Reads digits with at most one point from word[*index], and returns whether there was a digit.
static bool smith_text_decimal_scan_mantissa(SmithText word, size_t *index, SmithTextDecimal *decimal) {
    bool in_fraction = false;
    bool any = false;
    size_t i = *index;
    for (; i < word.length; i++) {
        char c = word.begin[i];
        if (c == '.' && !in_fraction) {
            in_fraction = true;
        } else if (c >= '0' && c <= '9') {
            smith_text_decimal_add_digit(decimal, (uint32_t)(c - '0'), in_fraction);
            any = true;
        } else {
            break;
        }
    }
    *index = i;
    return any;
}

// Reads an optional exponent at word[*index] into exp10, and returns false for an e with no digits.
static bool smith_text_decimal_scan_exponent(SmithText word, size_t *index, SmithTextDecimal *decimal) {
    size_t i = *index;
    if (i == word.length || (word.begin[i] != 'e' && word.begin[i] != 'E')) return true;
    i += 1;
    bool negative = i < word.length && word.begin[i] == '-';
    if (i < word.length && (word.begin[i] == '-' || word.begin[i] == '+')) i += 1;
    int64_t magnitude = 0;
    size_t digits = 0;
    for (; i < word.length && word.begin[i] >= '0' && word.begin[i] <= '9'; i++) {
        if (magnitude < smith_text_exponent_max) magnitude = magnitude * 10 + (word.begin[i] - '0');
        digits += 1;
    }
    if (digits == 0) return false;
    decimal->exp10 += negative ? -magnitude : magnitude;
    *index = i;
    return true;
}

// The exact path: the digits over a power of ten, as a ratio of big integers.
static uint64_t smith_text_decimal_exact(const SmithTextDecimal *decimal, const SmithTextFormat *format) {
    SmithTextBig numerator = decimal->digits;
    SmithTextBig denominator = {.limb = {1}, .count = 1};
    if (decimal->exp10 >= 0)
        smith_text_big_mul_pow10(&numerator, (uint64_t)decimal->exp10);
    else
        smith_text_big_mul_pow10(&denominator, (uint64_t)(0 - decimal->exp10));
    return smith_text_ratio_to_bits(format, numerator, denominator);
}

// Whether the decimal is a number of at most 15 digits times an exactly represented power of ten.
static bool smith_text_decimal_is_fast(const SmithTextDecimal *decimal) {
    return !decimal->sticky && decimal->significant <= smith_text_fast_digits_max &&
           decimal->exp10 >= -smith_text_fast_exponent_max && decimal->exp10 <= smith_text_fast_exponent_max;
}

// The double nearest the decimal when it is_fast: one multiply or divide of two exact doubles.
static double smith_text_decimal_fast(const SmithTextDecimal *decimal) {
    double exact = (double)decimal->small;
    double power = smith_text_powers[decimal->exp10 < 0 ? -decimal->exp10 : decimal->exp10];
    return decimal->exp10 < 0 ? exact / power : exact * power;
}

// Finishes the digits of a decimal that the scan left in a partial chunk, and adds the sticky digit.
static void smith_text_decimal_finish(SmithTextDecimal *decimal) {
    if (decimal->chunk_length != 0)
        smith_text_big_mul_add(&decimal->digits, smith_text_powers_u32[decimal->chunk_length], decimal->chunk);
    if (decimal->sticky) {
        smith_text_big_mul_add(&decimal->digits, 10, 1);
        decimal->exp10 -= 1;
        decimal->significant += 1;
    }
}

// The float for a scanned decimal, as bits, or nothing for a number that would be infinity.
static bool smith_text_decimal_to_f32_bits(SmithTextDecimal *decimal, uint32_t *bits) {
    if (decimal->significant == 0) {
        *bits = 0;
        return true;
    }
    smith_text_decimal_finish(decimal);
    int64_t magnitude = (int64_t)decimal->significant + decimal->exp10; // the number lies below 10^magnitude
    if (magnitude >= 40) return false;                                  // at least 10^39, above the largest float
    if (magnitude <= -47) {                                             // below 10^-47, under half the smallest
        *bits = 0;
        return true;
    }
    if (smith_text_decimal_is_fast(decimal)) {
        double nearest = smith_text_decimal_fast(decimal);
        uint64_t nearest_bits = __builtin_bit_cast(uint64_t, nearest);
        bool halfway = (nearest_bits & 0x1fffffff) == 0x10000000; // between two floats: look closer
        if (nearest >= (double)FLT_MIN && nearest <= 1e38 && !halfway) {
            *bits = __builtin_bit_cast(uint32_t, (float)nearest);
            return true;
        }
    }
    *bits = (uint32_t)smith_text_decimal_exact(decimal, &smith_text_float);
    return *bits < smith_text_infinity_bits;
}

// The same for a double.
static bool smith_text_decimal_to_f64_bits(SmithTextDecimal *decimal, uint64_t *bits) {
    if (decimal->significant == 0) {
        *bits = 0;
        return true;
    }
    smith_text_decimal_finish(decimal);
    int64_t magnitude = (int64_t)decimal->significant + decimal->exp10;
    if (magnitude >= 310) return false; // at least 10^309, above the largest double
    if (magnitude <= -325) {            // below 10^-325, under half the smallest
        *bits = 0;
        return true;
    }
    if (smith_text_decimal_is_fast(decimal)) {
        double nearest = smith_text_decimal_fast(decimal); // one rounding of exact operands: the answer
        if (nearest >= DBL_MIN && nearest <= 1e300) {
            *bits = __builtin_bit_cast(uint64_t, nearest);
            return true;
        }
    }
    *bits = smith_text_decimal_exact(decimal, &smith_text_double);
    return *bits < smith_text_double_infinity_bits;
}

// Scans a whole word: a sign, digits and an exponent. Returns whether it is a number, and whether it is negative.
static bool smith_text_decimal_scan(SmithText word, SmithTextDecimal *decimal, bool *negative) {
    size_t index = 0;
    *negative = word.length > 0 && word.begin[0] == '-';
    if (word.length > 0 && (word.begin[0] == '-' || word.begin[0] == '+')) index = 1;
    if (!smith_text_decimal_scan_mantissa(word, &index, decimal)) return false;
    if (!smith_text_decimal_scan_exponent(word, &index, decimal)) return false;
    return index == word.length;
}

bool smith_text_parse_f32(SmithText word, float *value) {
    assert(value != nullptr);
    SmithTextDecimal decimal;
    smith_text_decimal_init(&decimal);
    bool negative = false;
    if (!smith_text_decimal_scan(word, &decimal, &negative)) return false;
    uint32_t bits = 0;
    if (!smith_text_decimal_to_f32_bits(&decimal, &bits)) return false;
    if (negative) bits |= 0x80000000;
    *value = __builtin_bit_cast(float, bits);
    return true;
}

bool smith_text_parse_f64(SmithText word, double *value) {
    assert(value != nullptr);
    SmithTextDecimal decimal;
    smith_text_decimal_init(&decimal);
    bool negative = false;
    if (!smith_text_decimal_scan(word, &decimal, &negative)) return false;
    uint64_t bits = 0;
    if (!smith_text_decimal_to_f64_bits(&decimal, &bits)) return false;
    if (negative) bits |= (uint64_t)1 << 63;
    *value = __builtin_bit_cast(double, bits);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Writing numbers

size_t smith_text_write_u64(char *out, uint64_t value) {
    assert(out != nullptr);
    char reversed[smith_text_number_capacity];
    size_t count = 0;
    do {
        reversed[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < smith_text_number_capacity);
    assert(value == 0);
    for (size_t i = 0; i < count; i++) {
        out[i] = reversed[count - 1 - i];
    }
    return count;
}

size_t smith_text_write_i64(char *out, int64_t value) {
    assert(out != nullptr);
    if (value >= 0) return smith_text_write_u64(out, (uint64_t)value);
    out[0] = '-';
    return 1 + smith_text_write_u64(out + 1, 0 - (uint64_t)value);
}

size_t smith_text_write_hex(char *out, uint64_t value) {
    assert(out != nullptr);
    for (size_t i = 0; i < 16; i++) {
        uint64_t digit = (value >> (60 - 4 * i)) & 0xf;
        out[i] = (char)(digit < 10 ? '0' + digit : 'a' + (digit - 10));
    }
    return 16;
}

// A double, as the fewest digits (at most 17) that read back as the same double. With the value m * 2^e, the p-digit
// decimal nearest it is q * 10^s: q = round(m * 2^e / 10^s), ties to even, found with the big integers above. The
// number of digits is the least p that parses back to the same bits (more digits only come nearer, so a binary search
// over 1..17 finds it), and the exponent of the first digit decides between positional and scientific text.

constexpr uint32_t smith_text_f64_digits_max = 17;
constexpr int64_t smith_text_f64_positional_max = 16;
constexpr int64_t smith_text_f64_positional_min = -5;

// q = floor(numerator / denominator), which has at most 63 bits; the remainder is left in numerator.
static uint64_t smith_text_big_divide(SmithTextBig *numerator, const SmithTextBig *denominator) {
    int64_t shift = (int64_t)smith_text_big_bits(numerator) - (int64_t)smith_text_big_bits(denominator);
    if (shift < 0) return 0;
    assert(shift < 63);
    SmithTextBig step = *denominator;
    smith_text_big_shift_left(&step, (uint64_t)shift);
    uint64_t q = 0;
    for (int64_t bit = shift; bit >= 0; bit--) {
        if (smith_text_big_at_least(numerator, &step)) {
            smith_text_big_subtract(numerator, &step);
            q |= (uint64_t)1 << bit;
        }
        smith_text_big_shift_right_one(&step);
    }
    return q;
}

// The quotient of m * 2^e / 10^s, rounded to the nearest integer, ties to even.
static uint64_t smith_text_f64_scaled(uint64_t m, int64_t e, int64_t s, bool round) {
    SmithTextBig numerator = {.limb = {(uint32_t)m, (uint32_t)(m >> 32)}, .count = (m >> 32) != 0 ? 2U : 1U};
    SmithTextBig denominator = {.limb = {1}, .count = 1};
    if (e >= 0)
        smith_text_big_shift_left(&numerator, (uint64_t)e);
    else
        smith_text_big_shift_left(&denominator, (uint64_t)(0 - e));
    if (s >= 0)
        smith_text_big_mul_pow10(&denominator, (uint64_t)s);
    else
        smith_text_big_mul_pow10(&numerator, (uint64_t)(0 - s));
    uint64_t q = smith_text_big_divide(&numerator, &denominator);
    if (!round) return q;
    smith_text_big_shift_left(&numerator, 1); // twice the remainder against the divisor
    bool above = smith_text_big_at_least(&numerator, &denominator);
    bool equal = above && smith_text_big_at_least(&denominator, &numerator);
    if (above && (!equal || (q & 1) != 0)) q += 1;
    return q;
}

static uint64_t smith_text_pow10_u64(uint32_t power) {
    uint64_t result = 1;
    for (uint32_t i = 0; i < power; i++) {
        result *= 10;
    }
    return result;
}

// The p-digit decimal nearest m * 2^e, as q * 10^s, given the decimal exponent of the first digit.
static uint64_t smith_text_f64_round_to(uint64_t m, int64_t e, int64_t first, uint32_t p, int64_t *s) {
    *s = first - (int64_t)p + 1;
    uint64_t q = smith_text_f64_scaled(m, e, *s, true);
    if (q == smith_text_pow10_u64(p)) { // 9.99 rounded up to 10.0
        q = smith_text_pow10_u64(p - 1);
        *s += 1;
    }
    return q;
}

// The decimal exponent of the first digit of m * 2^e: floor(log10), from the binary exponent and then corrected.
static int64_t smith_text_f64_first_exponent(uint64_t m, int64_t e) {
    int64_t binary = e + (int64_t)(64 - (uint32_t)__builtin_clzll(m)) - 1;
    int64_t first = (binary * 78913) >> 18; // binary * log10(2), floor, within one
    for (int attempt = 0; attempt < 4; attempt++) {
        uint64_t q = smith_text_f64_scaled(m, e, first, false);
        if (q == 0)
            first -= 1;
        else if (q >= 10)
            first += 1;
        else
            return first;
    }
    assert(false);
    return first;
}

static bool smith_text_f64_reads_back(uint64_t q, int64_t s, uint64_t bits) {
    char text[smith_text_number_capacity * 2];
    size_t length = smith_text_write_u64(text, q);
    text[length++] = 'e';
    length += smith_text_write_i64(text + length, s);
    double back = 0;
    return smith_text_parse_f64((SmithText){text, length}, &back) && __builtin_bit_cast(uint64_t, back) == bits;
}

// Writes the digits q (p of them, the first one of exponent `first`) as positional or scientific text.
static size_t smith_text_f64_format(char *out, uint64_t q, uint32_t p, int64_t first) {
    char digits[smith_text_number_capacity];
    size_t count = smith_text_write_u64(digits, q);
    assert(count == p);
    (void)p; // only the assertion reads it
    size_t used = 0;
    if (first >= smith_text_f64_positional_min && first <= smith_text_f64_positional_max) {
        if (first < 0) {
            out[used++] = '0';
            out[used++] = '.';
            for (int64_t i = 0; i < -first - 1; i++) {
                out[used++] = '0';
            }
        }
        for (size_t i = 0; i < count; i++) {
            if (first >= 0 && (int64_t)i == first + 1) out[used++] = '.';
            out[used++] = digits[i];
        }
        for (int64_t i = (int64_t)count; i < first + 1; i++) { // an integer with zeros after its digits
            out[used++] = '0';
        }
        return used;
    }
    out[used++] = digits[0];
    if (count > 1) out[used++] = '.';
    for (size_t i = 1; i < count; i++) {
        out[used++] = digits[i];
    }
    out[used++] = 'e';
    return used + smith_text_write_i64(out + used, first);
}

size_t smith_text_write_f64(char *out, double value) {
    assert(out != nullptr);
    uint64_t bits = __builtin_bit_cast(uint64_t, value);
    uint64_t field = (bits >> 52) & 0x7ff;
    assert(field != 0x7ff); // infinity and nan have no text
    size_t used = 0;
    if ((bits >> 63) != 0) out[used++] = '-';
    uint64_t fraction = bits & 0xfffffffffffffULL;
    if (field == 0 && fraction == 0) {
        out[used++] = '0';
        return used;
    }
    uint64_t m = field == 0 ? fraction : fraction | ((uint64_t)1 << 52);
    int64_t e = field == 0 ? -1074 : (int64_t)field - 1075;
    uint64_t positive = bits & ~((uint64_t)1 << 63);
    int64_t first = smith_text_f64_first_exponent(m, e);
    uint32_t low = 1;
    uint32_t high = smith_text_f64_digits_max;
    while (low < high) {
        uint32_t middle = (low + high) / 2;
        int64_t s = 0;
        uint64_t q = smith_text_f64_round_to(m, e, first, middle, &s);
        if (smith_text_f64_reads_back(q, s, positive))
            high = middle;
        else
            low = middle + 1;
    }
    int64_t s = 0;
    uint64_t q = smith_text_f64_round_to(m, e, first, low, &s);
    return used + smith_text_f64_format(out + used, q, low, s + (int64_t)low - 1);
}
