// Tests of <smith/text.h>, in smith's own test library; tests/text.sh builds them in a scratch project as
// tests/test_text.c. strtof in the C locale is the reference for the float parser (this program never sets a locale),
// and the halfway cases are also checked against what they must round to, so the reference is not the only witness.
#include <smith/test.h>
#include <smith/text.h>

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr size_t test_text_capacity = 1024;

static SmithText test_text(const char *string) {
    return smith_text_make(string);
}

static uint64_t test_text_draw_u64(SmithKey key) {
    uint64_t shift = smith_key_bits(smith_key_derive(key, 1), 0) % 64; // spreads values over every length
    return smith_key_bits(smith_key_derive(key, 0), 0) >> shift;
}

static int64_t test_text_uniform(SmithKey key, int64_t low, int64_t high) {
    return smith_uniform_int64_sample(key, (SmithUniformInt64){.low = low, .high = high});
}

// ---------------------------------------------------------------------------------------------------------------
// Lines and words

void test_text_lines_and_words(void) {
    SmithText text = test_text("first line\r\n\n  # only a comment\n\tmove  3\t-4   # go\nlast");
    SmithText line = {};
    SmithText word = {};
    SMITH_CHECK(smith_text_take_line(&text, &line));
    SMITH_CHECK(smith_text_is(line, "first line")); // the carriage return is gone
    SMITH_CHECK(smith_text_take_line(&text, &line));
    SMITH_CHECK(line.length, ==, 0);
    SMITH_CHECK(smith_text_take_line(&text, &line));
    SMITH_CHECK(line.length, ==, 0); // a comment and the spaces before it are gone
    SMITH_CHECK(smith_text_take_line(&text, &line));
    SMITH_CHECK(smith_text_take_word(&line, &word));
    SMITH_CHECK(smith_text_is(word, "move"));
    SMITH_CHECK(smith_text_take_word(&line, &word));
    int64_t three = 0;
    SMITH_CHECK(smith_text_parse_i64(word, &three));
    SMITH_CHECK(three, ==, 3);
    SMITH_CHECK(smith_text_take_word(&line, &word));
    SMITH_CHECK(smith_text_is(word, "-4"));
    SMITH_CHECK(!smith_text_take_word(&line, &word)); // only the comment was left
    SMITH_CHECK(smith_text_take_line(&text, &line));  // the last line has no newline
    SMITH_CHECK(smith_text_is(line, "last"));
    SMITH_CHECK(!smith_text_take_line(&text, &line));
    SMITH_CHECK(text.length, ==, 0);
}

void test_text_empty_and_unterminated(void) {
    SmithText empty = test_text("");
    SmithText line = {};
    SMITH_CHECK(!smith_text_take_line(&empty, &line));
    SmithText newline = test_text("\n");
    SMITH_CHECK(smith_text_take_line(&newline, &line)); // one empty line
    SMITH_CHECK(line.length, ==, 0);
    SMITH_CHECK(!smith_text_take_line(&newline, &line));
    SmithText blank = test_text(" \t ");
    SmithText word = {};
    SMITH_CHECK(!smith_text_take_word(&blank, &word));
}

void test_text_is_compares_whole_words(void) {
    SMITH_CHECK(smith_text_is(test_text("move"), "move"));
    SMITH_CHECK(!smith_text_is(test_text("move"), "mov"));
    SMITH_CHECK(!smith_text_is(test_text("mov"), "move"));
    SMITH_CHECK(!smith_text_is(test_text("move"), "Move"));
    SMITH_CHECK(smith_text_is((SmithText){"moves", 4}, "move")); // a word is a pointer and a length
    SMITH_CHECK(smith_text_is(test_text(""), ""));
    SMITH_CHECK(!smith_text_is(test_text(""), "x"));
}

// A line of words is read back from what was written, for any words.
void test_text_words_round_trip(SmithKey key) {
    char buffer[test_text_capacity];
    size_t used = 0;
    size_t count = (size_t)test_text_uniform(smith_key_derive(key, 0), 0, 20);
    for (size_t i = 0; i < count; i++) {
        used += smith_text_write_u64(buffer + used, test_text_draw_u64(smith_key_derive(key, 100 + i)));
        buffer[used++] = i % 2 == 0 ? ' ' : '\t';
    }
    SmithText line = {buffer, used};
    SmithText word = {};
    size_t seen = 0;
    while (smith_text_take_word(&line, &word)) {
        uint64_t value = 0;
        SMITH_CHECK(smith_text_parse_u64(word, &value));
        SMITH_CHECK(value, ==, test_text_draw_u64(smith_key_derive(key, 100 + seen)));
        seen += 1;
    }
    SMITH_CHECK(seen, ==, count);
}

// ---------------------------------------------------------------------------------------------------------------
// Whole numbers

static void test_text_check_written(char *out, size_t count, const char *expected) {
    SMITH_CHECK(count, ==, strlen(expected));
    SMITH_CHECK(memcmp(out, expected, count), ==, 0);
}

void test_text_write_known_values(void) {
    char out[smith_text_number_capacity];
    test_text_check_written(out, smith_text_write_u64(out, 0), "0");
    test_text_check_written(out, smith_text_write_u64(out, 10), "10");
    test_text_check_written(out, smith_text_write_u64(out, UINT64_MAX), "18446744073709551615");
    test_text_check_written(out, smith_text_write_i64(out, 0), "0");
    test_text_check_written(out, smith_text_write_i64(out, -1), "-1");
    test_text_check_written(out, smith_text_write_i64(out, INT64_MAX), "9223372036854775807");
    test_text_check_written(out, smith_text_write_i64(out, INT64_MIN), "-9223372036854775808");
    test_text_check_written(out, smith_text_write_hex(out, 0), "0000000000000000");
    test_text_check_written(out, smith_text_write_hex(out, 0xdeadbeef), "00000000deadbeef");
    test_text_check_written(out, smith_text_write_hex(out, UINT64_MAX), "ffffffffffffffff");
}

void test_text_write_stays_in_capacity(void) {
    char out[smith_text_number_capacity + 8];
    memset(out, 'x', sizeof out);
    SMITH_CHECK(smith_text_write_i64(out, INT64_MIN), ==, smith_text_number_capacity); // the longest text
    for (size_t i = smith_text_number_capacity; i < sizeof out; i++)
        SMITH_CHECK(out[i], ==, 'x');
}

void test_text_u64_round_trip(SmithKey key) {
    uint64_t value = test_text_draw_u64(key);
    char out[smith_text_number_capacity];
    size_t count = smith_text_write_u64(out, value);
    SMITH_CHECK(count, <=, smith_text_number_capacity);
    uint64_t back = 0;
    SMITH_CHECK(smith_text_parse_u64((SmithText){out, count}, &back));
    SMITH_CHECK(back, ==, value);
    // Leading zeros change nothing, however many.
    char padded[test_text_capacity];
    size_t zeros = (size_t)test_text_uniform(smith_key_derive(key, 5), 0, 200);
    memset(padded, '0', zeros);
    memcpy(padded + zeros, out, count);
    uint64_t again = 0;
    SMITH_CHECK(smith_text_parse_u64((SmithText){padded, zeros + count}, &again));
    SMITH_CHECK(again, ==, value);
}

void test_text_i64_round_trip(SmithKey key) {
    uint64_t bits = test_text_draw_u64(key);
    int64_t value = (int64_t)bits;
    if (smith_key_bits(smith_key_derive(key, 7), 0) % 2 == 1) value = (int64_t)(0 - bits); // both signs, all lengths
    char out[smith_text_number_capacity];
    size_t count = smith_text_write_i64(out, value);
    SMITH_CHECK(count, <=, smith_text_number_capacity);
    int64_t back = 0;
    SMITH_CHECK(smith_text_parse_i64((SmithText){out, count}, &back));
    SMITH_CHECK(back, ==, value);
    char padded[test_text_capacity];
    size_t zeros = (size_t)test_text_uniform(smith_key_derive(key, 5), 0, 200);
    size_t sign = value < 0 ? 1 : 0;
    memcpy(padded, out, sign);
    memset(padded + sign, '0', zeros);
    memcpy(padded + sign + zeros, out + sign, count - sign);
    int64_t again = 0;
    SMITH_CHECK(smith_text_parse_i64((SmithText){padded, zeros + count}, &again));
    SMITH_CHECK(again, ==, value);
}

void test_text_hex_round_trip(SmithKey key) {
    uint64_t value = test_text_draw_u64(key);
    char out[16];
    SMITH_CHECK(smith_text_write_hex(out, value), ==, 16);
    uint64_t back = 0;
    SMITH_CHECK(smith_text_parse_hex((SmithText){out, 16}, &back));
    SMITH_CHECK(back, ==, value);
    for (size_t i = 0; i < 16; i++)
        SMITH_CHECK((out[i] >= '0' && out[i] <= '9') || (out[i] >= 'a' && out[i] <= 'f'));
    char padded[test_text_capacity];
    size_t zeros = (size_t)test_text_uniform(smith_key_derive(key, 5), 0, 200);
    memset(padded, '0', zeros);
    memcpy(padded + zeros, out, 16);
    uint64_t again = 0;
    SMITH_CHECK(smith_text_parse_hex((SmithText){padded, zeros + 16}, &again));
    SMITH_CHECK(again, ==, value);
}

// Every value that has exactly n digits, for each n: the lengths of a number are the edges.
void test_text_integer_edges(void) {
    uint64_t u = 0;
    SMITH_CHECK(smith_text_parse_u64(test_text("0"), &u));
    SMITH_CHECK(u, ==, 0);
    SMITH_CHECK(smith_text_parse_u64(test_text("18446744073709551615"), &u));
    SMITH_CHECK(u, ==, UINT64_MAX);
    SMITH_CHECK(smith_text_parse_u64(test_text("00000000000000000000000000018446744073709551615"), &u));
    SMITH_CHECK(u, ==, UINT64_MAX);
    int64_t i = 0;
    SMITH_CHECK(smith_text_parse_i64(test_text("9223372036854775807"), &i));
    SMITH_CHECK(i, ==, INT64_MAX);
    SMITH_CHECK(smith_text_parse_i64(test_text("-9223372036854775808"), &i));
    SMITH_CHECK(i, ==, INT64_MIN);
    SMITH_CHECK(smith_text_parse_i64(test_text("-0"), &i));
    SMITH_CHECK(i, ==, 0);
    SMITH_CHECK(smith_text_parse_i64(test_text("-0009"), &i));
    SMITH_CHECK(i, ==, -9);
    uint64_t h = 0;
    SMITH_CHECK(smith_text_parse_hex(test_text("0"), &h));
    SMITH_CHECK(h, ==, 0);
    SMITH_CHECK(smith_text_parse_hex(test_text("ffffffffffffffff"), &h));
    SMITH_CHECK(h, ==, UINT64_MAX);
    SMITH_CHECK(smith_text_parse_hex(test_text("000000000000000000000000000000ffffffffffffffff"), &h));
    SMITH_CHECK(h, ==, UINT64_MAX);
    SMITH_CHECK(!smith_text_parse_hex(test_text("DEAD"), &h)); // lowercase only, as it is written
}

// Each bad word fails and leaves the result as it was.
void test_text_failed_parse_writes_nothing(void) {
    const char *bad_unsigned[] = {"",
                                  "-",
                                  "-1",
                                  "+1",
                                  " 1",
                                  "1 ",
                                  "1_0",
                                  "0x10",
                                  "1.0",
                                  "18446744073709551616",
                                  "18446744073709551620",
                                  "99999999999999999999",
                                  "184467440737095516150",
                                  "100000000000000000000000000000000000000",
                                  nullptr};
    for (const char **s = bad_unsigned; *s != nullptr; s++) {
        uint64_t value = 77;
        SMITH_CHECK(!smith_text_parse_u64(test_text(*s), &value));
        SMITH_CHECK(value, ==, 77);
    }
    const char *bad_signed[] = {"",
                                "-",
                                "--1",
                                "+1",
                                "- 1",
                                "1-",
                                "9223372036854775808",
                                "-9223372036854775809",
                                "-18446744073709551615",
                                "-99999999999999999999999",
                                nullptr};
    for (const char **s = bad_signed; *s != nullptr; s++) {
        int64_t value = 77;
        SMITH_CHECK(!smith_text_parse_i64(test_text(*s), &value));
        SMITH_CHECK(value, ==, 77);
    }
    const char *bad_hex[] = {"", "g", "-1", "0x1", "F", "1 ", "10000000000000000", "1ffffffffffffffff", nullptr};
    for (const char **s = bad_hex; *s != nullptr; s++) {
        uint64_t value = 77;
        SMITH_CHECK(!smith_text_parse_hex(test_text(*s), &value));
        SMITH_CHECK(value, ==, 77);
    }
}

// One past the top of a type fails, and so does everything above it.
void test_text_overflow_fails(SmithKey key) {
    uint64_t value = test_text_draw_u64(key);
    char out[smith_text_number_capacity + 4];
    size_t count = smith_text_write_u64(out, value);
    out[count++] = (char)('0' + test_text_uniform(smith_key_derive(key, 3), 0, 9));
    uint64_t result = 0;
    bool fits = smith_text_parse_u64((SmithText){out, count}, &result);
    // The text of value followed by a digit is value * 10 + digit, which fits exactly when it does not wrap.
    uint64_t digit = (uint64_t)(out[count - 1] - '0');
    bool should_fit = value <= (UINT64_MAX - digit) / 10;
    SMITH_CHECK(fits == should_fit);
    if (fits) SMITH_CHECK(result, ==, value * 10 + digit);
}

// ---------------------------------------------------------------------------------------------------------------
// Floats

static bool test_text_reference(const char *string, float *value) {
    float reference = strtof(string, nullptr);
    if (reference > FLT_MAX || reference < -FLT_MAX) return false; // rounded to infinity
    *value = reference;
    return true;
}

// The parser against strtof: the same float, or a refusal exactly when strtof says infinity.
static void test_text_check_f32(const char *string) {
    smith_test_note("text \"%.200s\"", string);
    float expected = 0;
    bool finite = test_text_reference(string, &expected);
    float actual = 12345.0F;
    bool parsed = smith_text_parse_f32(test_text(string), &actual);
    SMITH_CHECK(parsed == finite);
    if (finite)
        SMITH_CHECK_SAME_BITS(actual, expected);
    else
        SMITH_CHECK_SAME_BITS(actual, 12345.0F); // a failed parse writes nothing
}

static float test_text_f32(const char *string) {
    float value = 0;
    SMITH_CHECK(smith_text_parse_f32(test_text(string), &value));
    return value;
}

void test_text_f32_known_values(void) {
    SMITH_CHECK_SAME_BITS(test_text_f32("0"), 0.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32("-0"), -0.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32("+0.0"), 0.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32("-0.000e5"), -0.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32("0.25"), 0.25F);
    SMITH_CHECK_SAME_BITS(test_text_f32("-90"), -90.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32("5."), 5.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32(".5"), 0.5F);
    SMITH_CHECK_SAME_BITS(test_text_f32("1e3"), 1000.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32("2.5E+3"), 2500.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32("25e-1"), 2.5F);
    SMITH_CHECK_SAME_BITS(test_text_f32("0.1"), 0.1F);
    SMITH_CHECK_SAME_BITS(test_text_f32("0.2"), 0.2F);
    SMITH_CHECK_SAME_BITS(test_text_f32("3.14159265358979"), 3.14159274F);
    SMITH_CHECK_SAME_BITS(test_text_f32("16777216"), 16777216.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32("16777217"), 16777216.0F); // halfway: ties to even
    SMITH_CHECK_SAME_BITS(test_text_f32("16777219"), 16777220.0F); // halfway: ties to even, up
    SMITH_CHECK_SAME_BITS(test_text_f32("16777217.0000000000000000000001"), 16777218.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32("3.4028234663852886e38"), FLT_MAX);
    SMITH_CHECK_SAME_BITS(test_text_f32("3.4028235e38"), FLT_MAX);
    SMITH_CHECK_SAME_BITS(test_text_f32("1.17549435e-38"), FLT_MIN);
    SMITH_CHECK_SAME_BITS(test_text_f32("1.4e-45"), 0x1p-149F);
    SMITH_CHECK_SAME_BITS(test_text_f32("7e-46"), 0.0F); // under half the smallest subnormal
    SMITH_CHECK_SAME_BITS(test_text_f32("7.01e-46"), 0x1p-149F);
    SMITH_CHECK_SAME_BITS(test_text_f32("1e-99999999999"), 0.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32("0e99999999999"), 0.0F);
    SMITH_CHECK_SAME_BITS(test_text_f32("000000000000000000000000000000000000000000000001"), 1.0F);
}

// 15-digit texts next to a halfway point, where the first version (a double sum, rounded again to float) was wrong.
void test_text_f32_double_rounding_cases(void) {
    SMITH_CHECK_SAME_BITS(test_text_f32("1.00047367811203"), 1.00047362F);
    SMITH_CHECK_SAME_BITS(test_text_f32("1.00057715177536"), 1.00057709F);
    SMITH_CHECK_SAME_BITS(test_text_f32("1.00076562166214"), 1.00076568F);
    test_text_check_f32("1.00086909532547");
    test_text_check_f32("1.00147145986557");
    test_text_check_f32("1.00176340341568");
}

void test_text_f32_rejects(void) {
    const char *bad[] = {"",
                         "-",
                         "+",
                         ".",
                         "-.",
                         "e5",
                         ".e5",
                         "1e",
                         "1e+",
                         "1e-",
                         "1.2.3",
                         "inf",
                         "-inf",
                         "nan",
                         "0x10",
                         "1f",
                         " 1",
                         "1 ",
                         "1,5",
                         "--1",
                         "+-1",
                         "1e5.5",
                         "1ee5",
                         "1e5e5",
                         "3.4028236e38",
                         "1e39",
                         "-1e39",
                         "340282356779733661637539395458142568448",
                         "1e99999999999",
                         "0.0000000000000000000000000001e68",
                         nullptr};
    for (const char **s = bad; *s != nullptr; s++) {
        float value = 12345.0F;
        SMITH_CHECK(!smith_text_parse_f32(test_text(*s), &value));
        SMITH_CHECK_SAME_BITS(value, 12345.0F);
    }
}

// The first float that is too large: halfway between FLT_MAX and 2^128.
void test_text_f32_overflow_edge(void) {
    SMITH_CHECK_SAME_BITS(test_text_f32("340282356779733661637539395458142568447"), FLT_MAX);
    float value = 0;
    SMITH_CHECK(!smith_text_parse_f32(test_text("340282356779733661637539395458142568448"), &value));
    SMITH_CHECK_SAME_BITS(test_text_f32("-340282356779733661637539395458142568447"), -FLT_MAX);
}

// Random decimals: any digits, a point anywhere, an exponent that reaches subnormals and the overflow.
static size_t test_text_random_decimal(SmithKey key, char *out) {
    size_t used = 0;
    if (smith_bernoulli_sample(smith_key_derive(key, 0), (SmithBernoulli){.probability = 0.3})) out[used++] = '-';
    size_t digits = (size_t)test_text_uniform(smith_key_derive(key, 1), 1, 60);
    size_t point = (size_t)test_text_uniform(smith_key_derive(key, 2), 0, (int64_t)digits + 1);      // digits + 1: none
    size_t zero_from = (size_t)test_text_uniform(smith_key_derive(key, 3), 1, (int64_t)digits + 20); // zeros after
    for (size_t i = 0; i < digits; i++) {
        if (i == point) out[used++] = '.';
        char digit = (char)('0' + test_text_uniform(smith_key_derive(key, 1000 + i), 0, 9));
        out[used++] = i >= zero_from ? '0' : digit;
    }
    if (smith_bernoulli_sample(smith_key_derive(key, 4), (SmithBernoulli){.probability = 0.7})) {
        int64_t exponent = test_text_uniform(smith_key_derive(key, 5), -60, 50);
        used += (size_t)snprintf(out + used, 16, "e%lld", (long long)exponent);
    }
    out[used] = '\0';
    return used;
}

void test_text_f32_matches_strtof_on_random_decimals(SmithKey key) {
    char text[test_text_capacity];
    (void)test_text_random_decimal(key, text);
    test_text_check_f32(text);
}

// More digits than any float needs, and zeros between: the tail past the 128th digit still decides a tie.
void test_text_f32_long_digit_strings(SmithKey key) {
    char text[test_text_capacity];
    size_t used = 0;
    uint64_t bits = smith_key_bits(smith_key_derive(key, 0), 0) % 0x7f7fffffULL; // a finite, nonnegative float
    float f = __builtin_bit_cast(float, (uint32_t)bits);
    used += (size_t)snprintf(text, 64, "%.9g", (double)f);
    char *exponent = strchr(text, 'e');
    char tail[8] = "";
    if (exponent != nullptr) {
        snprintf(tail, sizeof tail, "%s", exponent);
        used = (size_t)(exponent - text);
    }
    if (strchr(text, '.') == nullptr) text[used++] = '.';
    size_t zeros = (size_t)test_text_uniform(smith_key_derive(key, 1), 0, 300);
    memset(text + used, '0', zeros);
    used += zeros;
    text[used++] = smith_bernoulli_sample(smith_key_derive(key, 2), (SmithBernoulli){.probability = 0.5}) ? '1' : '0';
    snprintf(text + used, 8, "%s", tail);
    test_text_check_f32(text);
}

// Between two floats lies the point halfway; a string that is exactly it rounds to the even float, one a hair above
// it up, one a hair below it down. The hair is the neighbouring double, and then a 1 after hundreds of zeros.
void test_text_f32_halfway(SmithKey key) {
    uint32_t low_bits = (uint32_t)(smith_key_bits(smith_key_derive(key, 0), 0) % 0x7f7fffffULL);
    float low = __builtin_bit_cast(float, low_bits);
    float high = __builtin_bit_cast(float, low_bits + 1);
    double middle = ((double)low + (double)high) / 2; // exact: 25 bits
    uint64_t middle_bits = __builtin_bit_cast(uint64_t, middle);
    double above = __builtin_bit_cast(double, middle_bits + 1);
    double below = middle_bits == 0 ? 0 : __builtin_bit_cast(double, middle_bits - 1);
    char text[test_text_capacity];
    smith_test_note("low %.9g (0x%08x)", (double)low, low_bits);
    float expected_middle = (low_bits & 1) == 0 ? low : high;
    snprintf(text, sizeof text, "%.320f", middle);
    SMITH_CHECK_SAME_BITS(test_text_f32(text), expected_middle);
    test_text_check_f32(text);
    snprintf(text, sizeof text, "%.320f", above);
    SMITH_CHECK_SAME_BITS(test_text_f32(text), high);
    test_text_check_f32(text);
    if (middle_bits != 0) {
        snprintf(text, sizeof text, "%.320f", below);
        SMITH_CHECK_SAME_BITS(test_text_f32(text), low);
        test_text_check_f32(text);
    }
    size_t zeros = (size_t)test_text_uniform(smith_key_derive(key, 1), 0, 300);
    snprintf(text, sizeof text, "%.320f", middle);
    size_t used = strlen(text);
    memset(text + used, '0', zeros);
    text[used + zeros] = '1';
    text[used + zeros + 1] = '\0';
    SMITH_CHECK_SAME_BITS(test_text_f32(text), high); // a 1 far past the tie breaks it upward
    test_text_check_f32(text);
}

// What a program writes with %.9g reads back as the same float, signs of zero included.
void test_text_f32_round_trips_nine_digits(SmithKey key) {
    uint32_t bits = (uint32_t)smith_key_bits(key, 0);
    float f = __builtin_bit_cast(float, bits);
    if (f != f || f > FLT_MAX || f < -FLT_MAX) return; // infinity and nan have no decimal here
    char text[64];
    snprintf(text, sizeof text, "%.9g", (double)f);
    float back = 0;
    SMITH_CHECK(smith_text_parse_f32(test_text(text), &back));
    SMITH_CHECK_SAME_BITS(back, f);
    test_text_check_f32(text);
}

// A sweep of the whole range of float patterns, formatted and parsed back. tests/text.sh's `exhaustive` mode does all
// 2^32.
void test_text_f32_sweep_of_every_pattern(void) {
    uint64_t stride = smith_test_full() ? 1021 : 16381; // prime, so the sweep touches every exponent and mantissa
    char text[64];
    for (uint64_t pattern = 0; pattern <= UINT32_MAX; pattern += stride) {
        float f = __builtin_bit_cast(float, (uint32_t)pattern);
        if (f != f || f > FLT_MAX || f < -FLT_MAX) continue;
        snprintf(text, sizeof text, "%.9g", (double)f);
        float back = 0;
        SMITH_CHECK(smith_text_parse_f32(test_text(text), &back));
        SMITH_CHECK_SAME_BITS(back, f);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Doubles

static bool test_text_reference_f64(const char *string, double *value) {
    double reference = strtod(string, nullptr);
    if (reference > DBL_MAX || reference < -DBL_MAX) return false; // rounded to infinity
    *value = reference;
    return true;
}

// The parser against strtod: the same double, or a refusal exactly when strtod says infinity.
static void test_text_check_f64(const char *string) {
    smith_test_note("text \"%.200s\"", string);
    double expected = 0;
    bool finite = test_text_reference_f64(string, &expected);
    double actual = 12345.0;
    bool parsed = smith_text_parse_f64(test_text(string), &actual);
    SMITH_CHECK(parsed == finite);
    if (finite)
        SMITH_CHECK_SAME_BITS(actual, expected);
    else
        SMITH_CHECK_SAME_BITS(actual, 12345.0); // a failed parse writes nothing
}

static double test_text_f64(const char *string) {
    double value = 0;
    SMITH_CHECK(smith_text_parse_f64(test_text(string), &value));
    return value;
}

void test_text_f64_known_values(void) {
    SMITH_CHECK_SAME_BITS(test_text_f64("0"), 0.0);
    SMITH_CHECK_SAME_BITS(test_text_f64("-0"), -0.0);
    SMITH_CHECK_SAME_BITS(test_text_f64("+0.0"), 0.0);
    SMITH_CHECK_SAME_BITS(test_text_f64("-0.000e5"), -0.0);
    SMITH_CHECK_SAME_BITS(test_text_f64("1"), 1.0);
    SMITH_CHECK_SAME_BITS(test_text_f64("0.25"), 0.25);
    SMITH_CHECK_SAME_BITS(test_text_f64("-90"), -90.0);
    SMITH_CHECK_SAME_BITS(test_text_f64("5."), 5.0);
    SMITH_CHECK_SAME_BITS(test_text_f64(".5"), 0.5);
    SMITH_CHECK_SAME_BITS(test_text_f64("2.5E+3"), 2500.0);
    SMITH_CHECK_SAME_BITS(test_text_f64("0.1"), 0x1.999999999999ap-4);
    SMITH_CHECK_SAME_BITS(test_text_f64("0.2"), 0x1.999999999999ap-3);
    SMITH_CHECK_SAME_BITS(test_text_f64("0.3"), 0x1.3333333333333p-2);
    SMITH_CHECK_SAME_BITS(test_text_f64("3.141592653589793"), 0x1.921fb54442d18p+1);
    SMITH_CHECK_SAME_BITS(test_text_f64("9007199254740992"), 0x1p53);
    SMITH_CHECK_SAME_BITS(test_text_f64("9007199254740993"), 0x1p53);               // halfway: ties to even
    SMITH_CHECK_SAME_BITS(test_text_f64("9007199254740995"), 0x1.0000000000002p53); // halfway: ties to even, up
    SMITH_CHECK_SAME_BITS(test_text_f64("9007199254740993.0000000000000000001"), 0x1.0000000000001p53);
    SMITH_CHECK_SAME_BITS(test_text_f64("1.7976931348623157e308"), DBL_MAX);
    SMITH_CHECK_SAME_BITS(test_text_f64("1.7976931348623158e308"), DBL_MAX); // under the halfway point
    SMITH_CHECK_SAME_BITS(test_text_f64("2.2250738585072014e-308"), DBL_MIN);
    SMITH_CHECK_SAME_BITS(test_text_f64("2.2250738585072011e-308"), 0x0.fffffffffffffp-1022); // the famous hang
    SMITH_CHECK_SAME_BITS(test_text_f64("4.9406564584124654e-324"), 0x1p-1074);
    SMITH_CHECK_SAME_BITS(test_text_f64("5e-324"), 0x1p-1074);
    SMITH_CHECK_SAME_BITS(test_text_f64("2.4703282292062327e-324"), 0.0); // under half the smallest subnormal
    SMITH_CHECK_SAME_BITS(test_text_f64("2.4703282292062328e-324"), 0x1p-1074);
    SMITH_CHECK_SAME_BITS(test_text_f64("1e-99999999999"), 0.0);
    SMITH_CHECK_SAME_BITS(test_text_f64("0e99999999999"), 0.0);
    SMITH_CHECK_SAME_BITS(test_text_f64("000000000000000000000000000000000000000000000001"), 1.0);
    SMITH_CHECK_SAME_BITS(test_text_f64("1e22"), 1e22);
    SMITH_CHECK_SAME_BITS(test_text_f64("1e23"), 0x1.52d02c7e14af6p+76);
}

void test_text_f64_rejects(void) {
    const char *bad[] = {"",
                         "-",
                         "+",
                         ".",
                         "-.",
                         "e5",
                         ".e5",
                         "1e",
                         "1e+",
                         "1e-",
                         "1.2.3",
                         "inf",
                         "nan",
                         "0x10",
                         " 1",
                         "1 ",
                         "--1",
                         "1e5.5",
                         "1e309",
                         "-1e309",
                         "1.7976931348623159e308",
                         "1e99999999999",
                         "0.0000000000000000000000000001e340",
                         nullptr};
    for (const char **s = bad; *s != nullptr; s++) {
        double value = 12345.0;
        SMITH_CHECK(!smith_text_parse_f64(test_text(*s), &value));
        SMITH_CHECK_SAME_BITS(value, 12345.0);
    }
}

// Random decimals: any digits, a point anywhere, an exponent that reaches subnormals and the overflow.
static size_t test_text_random_decimal_f64(SmithKey key, char *out) {
    size_t used = 0;
    if (smith_bernoulli_sample(smith_key_derive(key, 0), (SmithBernoulli){.probability = 0.3})) out[used++] = '-';
    size_t digits = (size_t)test_text_uniform(smith_key_derive(key, 1), 1, 40);
    if (smith_bernoulli_sample(smith_key_derive(key, 6), (SmithBernoulli){.probability = 0.1}))
        digits = (size_t)test_text_uniform(smith_key_derive(key, 7), 40, 400); // a long one
    size_t point = (size_t)test_text_uniform(smith_key_derive(key, 2), 0, (int64_t)digits + 1);
    size_t zero_from = (size_t)test_text_uniform(smith_key_derive(key, 3), 1, (int64_t)digits + 20);
    for (size_t i = 0; i < digits; i++) {
        if (i == point) out[used++] = '.';
        char digit = (char)('0' + test_text_uniform(smith_key_derive(key, 1000 + i), 0, 9));
        out[used++] = i >= zero_from ? '0' : digit;
    }
    if (smith_bernoulli_sample(smith_key_derive(key, 4), (SmithBernoulli){.probability = 0.8})) {
        int64_t exponent = test_text_uniform(smith_key_derive(key, 5), -340, 320);
        used += (size_t)snprintf(out + used, 16, "e%lld", (long long)exponent);
    }
    out[used] = '\0';
    return used;
}

void test_text_f64_matches_strtod_on_random_decimals(SmithKey key) {
    char text[test_text_capacity];
    (void)test_text_random_decimal_f64(key, text);
    test_text_check_f64(text);
}

// Short decimals, which a multiply and a divide of exact doubles can answer: the fast path.
void test_text_f64_matches_strtod_on_short_decimals(SmithKey key) {
    char text[64];
    int64_t mantissa = test_text_uniform(smith_key_derive(key, 0), 0, 999999999999999);
    int64_t exponent = test_text_uniform(smith_key_derive(key, 1), -30, 30);
    snprintf(text, sizeof text, "%lldE%lld", (long long)mantissa, (long long)exponent);
    test_text_check_f64(text);
}

// The exact decimal expansion of (2m + 1) * 2^(e - 1), the point halfway between the doubles m * 2^e and
// (m + 1) * 2^e, written as digits and an exponent of ten, "<digits>e<exponent>". It is computed here with a digit
// array, apart from the library's arithmetic. `zeros` zeros follow the digits; with `bump` +1, a 1 replaces the last of
// them, and with -1 the number is one in that last place less.
static void test_text_halfway_decimal(uint64_t m, int exponent, size_t zeros, int bump, char *out) {
    unsigned char digit[2400] = {}; // least significant first
    size_t count = 0;
    uint64_t n = 2 * m + 1;
    while (n != 0) {
        digit[count++] = (unsigned char)(n % 10);
        n /= 10;
    }
    int shift = exponent - 1; // the value is n * 2^shift
    int decimal_exponent = 0;
    for (int i = 0; i < (shift < 0 ? -shift : shift); i++) { // n * 2 each time, or n * 5 and one more place
        unsigned factor = shift < 0 ? 5 : 2;
        unsigned carry = 0;
        for (size_t j = 0; j < count; j++) {
            unsigned v = digit[j] * factor + carry;
            digit[j] = (unsigned char)(v % 10);
            carry = v / 10;
        }
        while (carry != 0) {
            digit[count++] = (unsigned char)(carry % 10);
            carry /= 10;
        }
        if (shift < 0) decimal_exponent -= 1;
    }
    for (size_t j = count + zeros; j-- > zeros;) {
        digit[j] = digit[j - zeros];
    }
    for (size_t j = 0; j < zeros; j++) {
        digit[j] = 0;
    }
    count += zeros;
    decimal_exponent -= (int)zeros;
    if (bump > 0) {
        digit[0] = 1;
    } else if (bump < 0) {
        size_t j = 0;
        while (digit[j] == 0) {
            digit[j++] = 9;
        }
        digit[j] -= 1;
    }
    size_t used = 0;
    for (size_t j = count; j-- > 0;) {
        if (used == 0 && digit[j] == 0) continue; // a borrow can leave a leading zero
        out[used++] = (char)('0' + digit[j]);
    }
    snprintf(out + used, 16, "e%d", decimal_exponent);
}

// The significand and exponent of a finite positive double: bits = m * 2^exponent.
static void test_text_split_f64(uint64_t bits, uint64_t *m, int *exponent) {
    uint64_t fraction = bits & 0xfffffffffffffULL;
    uint64_t field = bits >> 52;
    *m = field == 0 ? fraction : fraction | (1ULL << 52);
    *exponent = field == 0 ? -1074 : (int)field - 1075;
}

// Between two doubles lies the point halfway; a string that is exactly it rounds to the even double, one a hair above
// it up, one a hair below it down. The expected answers are computed here, and strtod is checked against the same.
void test_text_f64_halfway(SmithKey key) {
    uint64_t low_bits = smith_key_bits(smith_key_derive(key, 0), 0) % 0x7fefffffffffffffULL; // finite, positive
    uint64_t m = 0;
    int exponent = 0;
    test_text_split_f64(low_bits, &m, &exponent);
    size_t zeros = (size_t)test_text_uniform(smith_key_derive(key, 1), 1, 300);
    char text[2800];
    smith_test_note("low bits 0x%016llx", (unsigned long long)low_bits);
    uint64_t middle_winner = (low_bits & 1) == 0 ? low_bits : low_bits + 1;
    test_text_halfway_decimal(m, exponent, 0, 0, text);
    SMITH_CHECK_SAME_BITS(test_text_f64(text), __builtin_bit_cast(double, middle_winner));
    test_text_check_f64(text);
    test_text_halfway_decimal(m, exponent, zeros, 1, text); // a 1 far past the tie breaks it upward
    SMITH_CHECK_SAME_BITS(test_text_f64(text), __builtin_bit_cast(double, low_bits + 1));
    test_text_check_f64(text);
    test_text_halfway_decimal(m, exponent, zeros, -1, text); // and a hair below breaks it downward
    SMITH_CHECK_SAME_BITS(test_text_f64(text), __builtin_bit_cast(double, low_bits));
    test_text_check_f64(text);
}

// The overflow edge: halfway between DBL_MAX and 2^1024 rounds to even, which is infinity, so it is refused, and a hair
// below it is DBL_MAX.
void test_text_f64_overflow_edge(void) {
    char text[2800];
    uint64_t m = (1ULL << 53) - 1;
    test_text_halfway_decimal(m, 971, 0, 0, text);
    double value = 0;
    SMITH_CHECK(!smith_text_parse_f64(test_text(text), &value));
    test_text_check_f64(text);
    test_text_halfway_decimal(m, 971, 5, -1, text);
    SMITH_CHECK_SAME_BITS(test_text_f64(text), DBL_MAX);
    test_text_check_f64(text);
    test_text_halfway_decimal(m, 971, 5, 1, text);
    SMITH_CHECK(!smith_text_parse_f64(test_text(text), &value));
}

// More digits than the 800 the parser keeps, with a nonzero digit only at the very end: the tail still decides a tie.
void test_text_f64_long_digit_strings(SmithKey key) {
    uint64_t low_bits = smith_key_bits(smith_key_derive(key, 0), 0) % 0x7fefffffffffffffULL;
    uint64_t m = 0;
    int exponent = 0;
    test_text_split_f64(low_bits, &m, &exponent);
    size_t zeros = (size_t)test_text_uniform(smith_key_derive(key, 1), 700, 1500);
    char text[2800];
    smith_test_note("low bits 0x%016llx, %zu zeros", (unsigned long long)low_bits, zeros);
    test_text_halfway_decimal(m, exponent, zeros, 1, text); // the midpoint, a 1 at the end of `zeros` zeros
    test_text_check_f64(text);
    SMITH_CHECK_SAME_BITS(test_text_f64(text), __builtin_bit_cast(double, low_bits + 1));
    test_text_halfway_decimal(m, exponent, zeros, -1, text);
    test_text_check_f64(text);
    SMITH_CHECK_SAME_BITS(test_text_f64(text), __builtin_bit_cast(double, low_bits));
}

// What a program writes with %.17g reads back as the same double, signs of zero included.
void test_text_f64_round_trips_seventeen_digits(SmithKey key) {
    uint64_t bits = smith_key_bits(key, 0);
    double d = __builtin_bit_cast(double, bits);
    if (d != d || d > DBL_MAX || d < -DBL_MAX) return;
    char text[64];
    snprintf(text, sizeof text, "%.17g", d);
    double back = 0;
    SMITH_CHECK(smith_text_parse_f64(test_text(text), &back));
    SMITH_CHECK_SAME_BITS(back, d);
    test_text_check_f64(text);
}

// The patterns that bound the format: zero, the smallest and largest subnormal and normal, the largest double, powers
// of two, and the neighbours of each. Every one is written with %.17g and with %.17e and read back.
void test_text_f64_round_trips_the_edges(void) {
    uint64_t edges[] = {0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000002ULL, 0x000fffffffffffffULL,
                        0x0010000000000000ULL, 0x0010000000000001ULL, 0x3ff0000000000000ULL, 0x3fefffffffffffffULL,
                        0x3ff0000000000001ULL, 0x4340000000000000ULL, 0x433fffffffffffffULL, 0x7fe0000000000000ULL,
                        0x7fefffffffffffffULL, 0x7feffffffffffffeULL};
    char text[64];
    for (size_t i = 0; i < sizeof edges / sizeof edges[0]; i++) {
        for (uint64_t sign = 0; sign < 2; sign++) {
            double d = __builtin_bit_cast(double, edges[i] | (sign << 63));
            for (int format = 0; format < 2; format++) {
                snprintf(text, sizeof text, format == 0 ? "%.17g" : "%.17e", d);
                double back = 0;
                SMITH_CHECK(smith_text_parse_f64(test_text(text), &back));
                SMITH_CHECK_SAME_BITS(back, d);
            }
        }
    }
    for (uint64_t field = 0; field < 2047; field++) { // every exponent, with the smallest, next and largest fractions
        for (uint64_t kind = 0; kind < 3; kind++) {
            uint64_t fraction = kind == 2 ? 0xfffffffffffffULL : kind;
            double d = __builtin_bit_cast(double, (field << 52) | fraction);
            snprintf(text, sizeof text, "%.17g", d);
            double back = 0;
            SMITH_CHECK(smith_text_parse_f64(test_text(text), &back));
            SMITH_CHECK_SAME_BITS(back, d);
        }
    }
}

// A sweep of the whole range of double patterns, formatted and parsed back: 2^13 of them, or 2^18 with -DFULL.
// `smith build text-exhaustive` does 2^28.
void test_text_f64_sweep_of_every_pattern(void) {
    uint64_t count = smith_test_full() ? (1ULL << 18) : (1ULL << 13);
    uint64_t stride = (UINT64_MAX / count) | 1; // odd, so the sweep touches every exponent and mantissa
    char text[64];
    for (uint64_t i = 0; i < count; i++) {
        double d = __builtin_bit_cast(double, i *stride);
        if (d != d || d > DBL_MAX || d < -DBL_MAX) continue;
        snprintf(text, sizeof text, "%.17g", d);
        double back = 0;
        SMITH_CHECK(smith_text_parse_f64(test_text(text), &back));
        SMITH_CHECK_SAME_BITS(back, d);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Writing doubles

static void test_text_check_f64_text(const char *expected, double value) {
    char out[smith_text_f64_capacity + 1];
    size_t length = smith_text_write_f64(out, value);
    out[length] = '\0';
    smith_test_note("value %.17g", value);
    SMITH_CHECK(strcmp(out, expected), ==, 0);
}

void test_text_f64_write_known_values(void) {
    test_text_check_f64_text("0", 0.0);
    test_text_check_f64_text("-0", -0.0);
    test_text_check_f64_text("1", 1.0);
    test_text_check_f64_text("-1", -1.0);
    test_text_check_f64_text("0.1", 0.1);
    test_text_check_f64_text("0.5", 0.5);
    test_text_check_f64_text("123.456", 123.456);
    test_text_check_f64_text("100", 100.0);
    test_text_check_f64_text("1e22", 1e22);
    test_text_check_f64_text("10000000000000000", 1e16);
    test_text_check_f64_text("1e17", 1e17);
    test_text_check_f64_text("0.00001", 1e-5);
    test_text_check_f64_text("1e-6", 1e-6);
    test_text_check_f64_text("3.141592653589793", 0x1.921fb54442d18p+1);
    test_text_check_f64_text("0.30000000000000004", 0.1 + 0.2);
    test_text_check_f64_text("5e-324", 0x1p-1074);
    test_text_check_f64_text("1.7976931348623157e308", DBL_MAX);
    test_text_check_f64_text("2.2250738585072014e-308", DBL_MIN);
    test_text_check_f64_text("9007199254740992", 0x1p53);
}

// Every double written reads back as the same bits, in the fewest digits: one digit fewer, correctly rounded, would
// read back as another double (strtod and snprintf are the witnesses).
void test_text_f64_write_is_shortest_and_round_trips(SmithKey key) {
    uint64_t bits = smith_key_bits(key, 0);
    if (smith_bernoulli_sample(smith_key_derive(key, 1), (SmithBernoulli){.probability = 0.5})) { // short decimals too
        int64_t thousandths = test_text_uniform(smith_key_derive(key, 2), -100000, 100000);
        bits = __builtin_bit_cast(uint64_t, (double)thousandths / 1000.0);
    }
    double d = __builtin_bit_cast(double, bits);
    if (d != d || d > DBL_MAX || d < -DBL_MAX) return;
    char out[smith_text_f64_capacity + 1];
    size_t length = smith_text_write_f64(out, d);
    SMITH_CHECK(length, <=, smith_text_f64_capacity);
    out[length] = '\0';
    smith_test_note("text %s", out);
    double back = 0;
    SMITH_CHECK(smith_text_parse_f64(test_text(out), &back));
    SMITH_CHECK_SAME_BITS(back, d);
    SMITH_CHECK_SAME_BITS(strtod(out, nullptr), d);
    int digits = 0;
    bool started = false;
    int trailing_zeros = 0;
    for (size_t i = 0; i < length && out[i] != 'e'; i++) {
        if (out[i] < '0' || out[i] > '9') continue;
        if (out[i] != '0') started = true;
        if (!started) continue;
        digits += 1;
        trailing_zeros = out[i] == '0' ? trailing_zeros + 1 : 0;
    }
    if (strchr(out, '.') == nullptr) digits -= trailing_zeros; // 1500 has two digits
    if (digits > 1) {
        char shorter[64];
        snprintf(shorter, sizeof shorter, "%.*e", digits - 2, d); // digits - 1 significant digits
        SMITH_CHECK(strtod(shorter, nullptr) != d);
    }
}

void test_text_f64_write_edges_round_trip(void) {
    for (uint64_t field = 0; field < 2047; field++) {
        // Under the sanitizers every exponent takes seconds, so the quick run takes every 13th and the edges (zero and
        // subnormals, around 1.0, the largest); --full takes them all.
        bool edge = field <= 1 || (field >= 1022 && field <= 1024) || field >= 2045;
        if (!smith_test_full() && !edge && field % 13 != 0) continue;
        for (uint64_t kind = 0; kind < 4; kind++) {
            uint64_t fraction = kind == 3 ? 0xfffffffffffffULL : kind;
            for (uint64_t sign = 0; sign < 2; sign++) {
                double d = __builtin_bit_cast(double, (sign << 63) | (field << 52) | fraction);
                char out[smith_text_f64_capacity];
                size_t length = smith_text_write_f64(out, d);
                double back = 0;
                SMITH_CHECK(smith_text_parse_f64((SmithText){out, length}, &back));
                SMITH_CHECK_SAME_BITS(back, d);
            }
        }
    }
}
