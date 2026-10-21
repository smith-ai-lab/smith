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
    uint64_t stride = smith_test_full() ? 1021 : 4093; // prime, so the sweep touches every exponent and mantissa
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
