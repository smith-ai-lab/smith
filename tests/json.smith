// Tests of <smith/json.h>, in smith's own test library; tests/json.sh builds them in a scratch project as
// tests/test_json.c. The checks are: known tokens, trees, errors with their offsets and reasons, and the writer's
// exact text; property tests on keys (any document written then read gives the same events, and every double and
// integer comes back exactly); fuzzing (damaged documents never crash, hang or read outside the input, and the pull
// parser and the tree agree on which are valid); and JSONTestSuite's cases when tests/json.sh gives their folder.
#include <smith/json.h>
#include <smith/test.h>

#include <dirent.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

constexpr SmithJsonLimits test_json_wide = {.depth = 64, .string_length = 1 << 16, .count = 1 << 16};

static SmithText test_json_text(const char *string) {
    return smith_text_make(string);
}

static int64_t test_json_uniform(SmithKey key, int64_t low, int64_t high) {
    return smith_uniform_int64_sample(key, (SmithUniformInt64){.low = low, .high = high});
}

// ---------------------------------------------------------------------------------------------------------------
// The pull parser

typedef struct {
    SmithJsonToken token;
    size_t offset;
} TestJsonStep;

void test_json_reader_tokens_of_a_document(void) {
    SmithText input = test_json_text("{\"a\": [1, true, null, \"x\"],\n \"b\": {}}");
    SmithJsonReader reader = smith_json_reader_make(input, test_json_wide);
    const TestJsonStep expected[] = {
        {SmithJsonToken_ObjectBegin, 0},  {SmithJsonToken_Key, 1},        {SmithJsonToken_ArrayBegin, 6},
        {SmithJsonToken_Number, 7},       {SmithJsonToken_True, 10},      {SmithJsonToken_Null, 16},
        {SmithJsonToken_String, 22},      {SmithJsonToken_ArrayEnd, 25},  {SmithJsonToken_Key, 29},
        {SmithJsonToken_ObjectBegin, 34}, {SmithJsonToken_ObjectEnd, 35}, {SmithJsonToken_ObjectEnd, 36},
        {SmithJsonToken_End, 37},
    };
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++) {
        SmithJsonToken token = smith_json_read(&reader);
        smith_test_note("step %zu", i);
        SMITH_CHECK(token == expected[i].token);
        SMITH_CHECK(reader.offset, ==, expected[i].offset);
    }
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_End); // End stays End
}

void test_json_reader_keys_and_numbers(void) {
    SmithJsonReader reader = smith_json_reader_make(test_json_text("{\"k\\u00e9y\": -12.5e+3}"), test_json_wide);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_ObjectBegin);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_Key);
    SMITH_CHECK(reader.escaped);
    SMITH_CHECK(reader.length, ==, 4); // k, the two bytes of e-acute, y
    SMITH_CHECK(smith_json_is(&reader, "k\xc3\xa9y"));
    SMITH_CHECK(!smith_json_is(&reader, "k\xc3\xa9"));
    SMITH_CHECK(!smith_json_is(&reader, "k\xc3\xa9yz"));
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_Number);
    SMITH_CHECK(smith_text_is(reader.text, "-12.5e+3"));
    double value = 0;
    SMITH_CHECK(smith_text_parse_f64(reader.text, &value));
    SMITH_CHECK_SAME_BITS(value, -12500.0);
    int64_t integer = 0;
    SMITH_CHECK(!smith_text_parse_i64(reader.text, &integer)); // not an integer as written
}

void test_json_reader_skips_values(void) {
    SmithJsonReader reader = smith_json_reader_make(
        test_json_text("{\"skip\": {\"x\": [1, [2, {\"y\": 3}]]}, \"keep\": 7, \"more\": [1]}"), test_json_wide);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_ObjectBegin);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_Key);
    SMITH_CHECK(smith_json_is(&reader, "skip"));
    SMITH_CHECK(smith_json_skip(&reader)); // a key skips its value
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_Key);
    SMITH_CHECK(smith_json_is(&reader, "keep"));
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_Number);
    SMITH_CHECK(smith_json_skip(&reader)); // a scalar skips nothing
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_Key);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_ArrayBegin);
    SMITH_CHECK(smith_json_skip(&reader));
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_ObjectEnd);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_End);
    SmithJsonReader broken = smith_json_reader_make(test_json_text("[1, [2, }]"), test_json_wide);
    SMITH_CHECK(smith_json_read(&broken) == SmithJsonToken_ArrayBegin);
    SMITH_CHECK(!smith_json_skip(&broken));
    SMITH_CHECK(broken.error.reason == SmithJsonReason_UnexpectedCharacter);
    SMITH_CHECK(broken.error.offset, ==, 8);
}

void test_json_reader_an_error_is_final(void) {
    SmithJsonReader reader = smith_json_reader_make(test_json_text("[1, x]"), test_json_wide);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_ArrayBegin);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_Number);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_Error);
    SmithJsonError first = reader.error;
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_Error);
    SMITH_CHECK(reader.error.offset, ==, first.offset);
    SMITH_CHECK(first.reason == SmithJsonReason_UnexpectedCharacter);
    SMITH_CHECK(first.offset, ==, 4);
}

// ---------------------------------------------------------------------------------------------------------------
// Errors: the offset and the reason

typedef struct {
    const char *input;
    SmithJsonReason reason;
    size_t offset;
} TestJsonError;

static void test_json_check_errors(const TestJsonError *cases, size_t count) {
    SmithArena arena = smith_test_arena(1 << 16);
    for (size_t i = 0; i < count; i++) {
        smith_test_note("input \"%s\"", cases[i].input);
        const SmithJsonValue *root = nullptr;
        SmithJsonError error = {};
        SmithText input = {cases[i].input, strlen(cases[i].input)};
        SMITH_CHECK(!smith_json_parse(&arena, input, test_json_wide, &root, &error));
        SMITH_CHECK(error.reason == cases[i].reason);
        SMITH_CHECK(error.offset, ==, cases[i].offset);
        SMITH_CHECK(arena.used, ==, 0);
        SmithJsonReader reader = smith_json_reader_make(input, test_json_wide);
        SmithJsonToken token = SmithJsonToken_Null;
        for (size_t step = 0; step <= input.length + 1 && token != SmithJsonToken_Error && token != SmithJsonToken_End;
             step++) {
            token = smith_json_read(&reader);
        }
        SMITH_CHECK(token == SmithJsonToken_Error);
        SMITH_CHECK(reader.error.offset, ==, cases[i].offset);
    }
}

void test_json_structure_errors(void) {
    const TestJsonError cases[] = {
        {"", SmithJsonReason_UnexpectedEnd, 0},
        {"   ", SmithJsonReason_UnexpectedEnd, 3},
        {"[", SmithJsonReason_UnexpectedEnd, 1},
        {"[1,", SmithJsonReason_UnexpectedEnd, 3},
        {"[1,]", SmithJsonReason_UnexpectedCharacter, 3},
        {"[,1]", SmithJsonReason_UnexpectedCharacter, 1},
        {"[1 2]", SmithJsonReason_UnexpectedCharacter, 3},
        {"[1}", SmithJsonReason_UnexpectedCharacter, 2},
        {"{\"a\":1]", SmithJsonReason_UnexpectedCharacter, 6},
        {"{\"a\" 1}", SmithJsonReason_UnexpectedCharacter, 5},
        {"{\"a\":}", SmithJsonReason_UnexpectedCharacter, 5},
        {"{\"a\":1,}", SmithJsonReason_UnexpectedCharacter, 7},
        {"{,}", SmithJsonReason_UnexpectedCharacter, 1},
        {"{1:2}", SmithJsonReason_UnexpectedCharacter, 1},
        {"{\"a\"", SmithJsonReason_UnexpectedEnd, 4},
        {"1 2", SmithJsonReason_TrailingContent, 2},
        {"[1]]", SmithJsonReason_TrailingContent, 3},
        {"{} x", SmithJsonReason_TrailingContent, 3},
        {"tru", SmithJsonReason_UnexpectedEnd, 3},
        {"nul!", SmithJsonReason_UnexpectedCharacter, 3},
        {"True", SmithJsonReason_UnexpectedCharacter, 0},
        {"truee", SmithJsonReason_TrailingContent, 4},
        {"'a'", SmithJsonReason_UnexpectedCharacter, 0},
        {"\xef\xbb\xbf{}", SmithJsonReason_UnexpectedCharacter, 0}, // a byte order mark is refused
        {"[1]\x01", SmithJsonReason_TrailingContent, 3},
        {"[\"a\"\n\"b\"]", SmithJsonReason_UnexpectedCharacter, 5},
    };
    test_json_check_errors(cases, sizeof cases / sizeof cases[0]);
}

void test_json_string_errors(void) {
    const TestJsonError cases[] = {
        {"\"abc", SmithJsonReason_UnexpectedEnd, 4},
        {"\"a\nb\"", SmithJsonReason_ControlCharacter, 2},
        {"\"a\tb\"", SmithJsonReason_ControlCharacter, 2},
        {"\"\\x\"", SmithJsonReason_BadEscape, 1},
        {"\"\\", SmithJsonReason_UnexpectedEnd, 1},
        {"\"\\u12\"", SmithJsonReason_BadEscape, 1},
        {"\"\\u12G4\"", SmithJsonReason_BadEscape, 1},
        {"\"\\ud800\"", SmithJsonReason_BadSurrogate, 1},
        {"\"\\ud800\\u0041\"", SmithJsonReason_BadSurrogate, 1},
        {"\"\\ud800x\"", SmithJsonReason_BadSurrogate, 1},
        {"\"\\udc00\"", SmithJsonReason_BadSurrogate, 1},
        {"\"\\udd1e\\ud834\"", SmithJsonReason_BadSurrogate, 1},
        {"\"\xc0\x80\"", SmithJsonReason_BadUtf8, 1},         // overlong
        {"\"\xc1\xbf\"", SmithJsonReason_BadUtf8, 1},         // overlong
        {"\"\xe0\x80\x80\"", SmithJsonReason_BadUtf8, 1},     // overlong
        {"\"\xf0\x80\x80\x80\"", SmithJsonReason_BadUtf8, 1}, // overlong
        {"\"\xed\xa0\x80\"", SmithJsonReason_BadUtf8, 1},     // a surrogate
        {"\"\xf4\x90\x80\x80\"", SmithJsonReason_BadUtf8, 1}, // past U+10FFFF
        {"\"\xf5\x80\x80\x80\"", SmithJsonReason_BadUtf8, 1}, // never a lead byte
        {"\"\xff\"", SmithJsonReason_BadUtf8, 1},
        {"\"\x80\"", SmithJsonReason_BadUtf8, 1},              // a lone continuation byte
        {"\"a\xe2\x82\"", SmithJsonReason_BadUtf8, 2},         // truncated
        {"\"\xe2\x82", SmithJsonReason_BadUtf8, 1},            // truncated by the end of the input
        {"{\"\\ud800\": 1}", SmithJsonReason_BadSurrogate, 2}, // a key is a string
    };
    test_json_check_errors(cases, sizeof cases / sizeof cases[0]);
}

void test_json_number_errors(void) {
    const TestJsonError cases[] = {
        {"-", SmithJsonReason_BadNumber, 1},
        {"-a", SmithJsonReason_BadNumber, 1},
        {"+1", SmithJsonReason_UnexpectedCharacter, 0},
        {".5", SmithJsonReason_UnexpectedCharacter, 0},
        {"1.", SmithJsonReason_BadNumber, 2},
        {"1.e5", SmithJsonReason_BadNumber, 2},
        {"-.5", SmithJsonReason_BadNumber, 1},
        {"1e", SmithJsonReason_BadNumber, 2},
        {"1e+", SmithJsonReason_BadNumber, 3},
        {"1e-x", SmithJsonReason_BadNumber, 3},
        {"01", SmithJsonReason_TrailingContent, 1},
        {"-01", SmithJsonReason_TrailingContent, 2},
        {"[00]", SmithJsonReason_UnexpectedCharacter, 2},
        {"1.5.5", SmithJsonReason_TrailingContent, 3},
        {"0x10", SmithJsonReason_TrailingContent, 1},
        {"NaN", SmithJsonReason_UnexpectedCharacter, 0},
        {"-Infinity", SmithJsonReason_BadNumber, 1},
        {"[1,2,3,0.5e]", SmithJsonReason_BadNumber, 11},
    };
    test_json_check_errors(cases, sizeof cases / sizeof cases[0]);
}

void test_json_accepts_the_grammar(void) {
    const char *good[] = {
        "0",
        "-0",
        "1",
        "-1",
        "10",
        "0.5",
        "-0.5",
        "1e5",
        "1E5",
        "1e+5",
        "1e-5",
        "1.5e10",
        "0e0",
        "0.0e-0",
        "123456789012345678901234567890",
        "1.7976931348623157e308",
        "\"\"",
        "\"a\"",
        "\"\\u0000\"",
        "\"\\/\"",
        "\"\xe2\x82\xac\"",
        "\"\xf4\x8f\xbf\xbf\"",
        "\"\xed\x9f\xbf\"",
        "\"\xee\x80\x80\"",
        "\"\xef\xbf\xbf\"",
        "\"\\ud834\\udd1e\"",
        "\"\x7f\"",
        "true",
        "false",
        "null",
        "[]",
        "{}",
        " \t\r\n[ ] ",
        "[[]]",
        "[{}]",
        "{\"a\":{}}",
        "[1,2,3]",
        "{\"a\":1,\"a\":2}",
        "{\"\":\"\"}",
        "[null,true,false]",
        "\n\n{ \"k\" : [ 1 , 2 ] }\n",
    };
    SmithArena arena = smith_test_arena(1 << 16);
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        smith_test_note("input \"%s\"", good[i]);
        const SmithJsonValue *root = nullptr;
        SmithJsonError error = {};
        SMITH_CHECK(smith_json_parse(&arena, test_json_text(good[i]), test_json_wide, &root, &error));
        SMITH_CHECK(root != nullptr);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Strings

static SmithText test_json_only_string(SmithArena *arena, const char *document) {
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    SMITH_CHECK(smith_json_parse(arena, test_json_text(document), test_json_wide, &root, &error));
    SMITH_CHECK(root->kind == SmithJsonKind_String);
    return root->text;
}

static void test_json_check_string(SmithArena *arena, const char *document, const char *bytes, size_t length) {
    smith_test_note("document %s", document);
    SmithText text = test_json_only_string(arena, document);
    SMITH_CHECK(text.length, ==, length);
    SMITH_CHECK(memcmp(text.begin, bytes, length), ==, 0);
}

void test_json_decodes_strings(void) {
    SmithArena arena = smith_test_arena(1 << 16);
    test_json_check_string(&arena, "\"\"", "", 0);
    test_json_check_string(&arena, "\"plain\"", "plain", 5);
    test_json_check_string(&arena, "\"a\\\"b\\\\c\\/d\"", "a\"b\\c/d", 7);
    test_json_check_string(&arena, "\"\\b\\f\\n\\r\\t\"", "\b\f\n\r\t", 5);
    test_json_check_string(&arena, "\"\\u0041\\u00e9\\u20AC\"", "A\xc3\xa9\xe2\x82\xac", 6);
    test_json_check_string(&arena, "\"\\ud834\\udd1e\"", "\xf0\x9d\x84\x9e", 4);
    test_json_check_string(&arena, "\"\\uD834\\uDD1E!\"", "\xf0\x9d\x84\x9e!", 5);
    test_json_check_string(&arena, "\"\\ud83d\\ude00 \xf0\x9f\x98\x80\"", "\xf0\x9f\x98\x80 \xf0\x9f\x98\x80", 9);
    test_json_check_string(&arena, "\"\\u0000x\"", "\0x", 2); // a zero byte inside
    test_json_check_string(&arena, "\"\\u001f\\u007f\"", "\x1f\x7f", 2);
    test_json_check_string(&arena, "\"\xef\xbf\xbf\xf4\x8f\xbf\xbf\"", "\xef\xbf\xbf\xf4\x8f\xbf\xbf", 7);
}

void test_json_decoded_strings_are_terminated_and_cheap(void) {
    SmithArena arena = smith_test_arena(1 << 16);
    const char *plain_document = "\"no escapes here\"";
    SmithText plain = test_json_only_string(&arena, plain_document);
    SMITH_CHECK(plain.begin == plain_document + 1); // no copy: a slice of the input
    SmithText escaped = test_json_only_string(&arena, "\"a\\nb\"");
    SMITH_CHECK(escaped.begin[escaped.length], ==, '\0');
}

void test_json_reader_decodes_into_the_arena(void) {
    SmithArena arena = smith_test_arena(1 << 12);
    SmithJsonReader reader = smith_json_reader_make(test_json_text("[\"x\\ty\", \"plain\"]"), test_json_wide);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_ArrayBegin);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_String);
    size_t before = arena.used;
    SmithText first = smith_json_decode(&reader, &arena);
    SMITH_CHECK(arena.used, >, before);
    SMITH_CHECK(first.length, ==, 3);
    SMITH_CHECK(memcmp(first.begin, "x\ty", 3), ==, 0);
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_String);
    before = arena.used;
    SmithText second = smith_json_decode(&reader, &arena);
    SMITH_CHECK(arena.used, ==, before); // nothing to decode: nothing pushed
    SMITH_CHECK(smith_text_is(second, "plain"));
}

// ---------------------------------------------------------------------------------------------------------------
// Numbers

static const SmithJsonValue *test_json_parse_ok(SmithArena *arena, const char *document) {
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    smith_test_note("document %s", document);
    SMITH_CHECK(smith_json_parse(arena, test_json_text(document), test_json_wide, &root, &error));
    return root;
}

void test_json_numbers_are_exact(void) {
    SmithArena arena = smith_test_arena(1 << 16);
    const SmithJsonValue *root = test_json_parse_ok(
        &arena, "[0, -0, 9223372036854775807, -9223372036854775808, 18446744073709551615, 18446744073709551616, "
                "-9223372036854775809, 1e2, 2.5, 1E400, -1e400, 1e-400]");
    SMITH_CHECK(root->count, ==, 12);
    int64_t i = 7;
    uint64_t u = 7;
    double d = 7;
    SMITH_CHECK(smith_text_parse_i64(root->items[0]->text, &i) && i == 0);
    SMITH_CHECK(smith_text_parse_i64(root->items[1]->text, &i) && i == 0);
    SMITH_CHECK(smith_text_parse_i64(root->items[2]->text, &i) && i == INT64_MAX);
    SMITH_CHECK(smith_text_parse_i64(root->items[3]->text, &i) && i == INT64_MIN);
    SMITH_CHECK(smith_text_parse_u64(root->items[4]->text, &u) && u == UINT64_MAX);
    SMITH_CHECK(!smith_text_parse_u64(root->items[5]->text, &u));
    SMITH_CHECK(!smith_text_parse_i64(root->items[5]->text, &i));
    SMITH_CHECK(!smith_text_parse_i64(root->items[6]->text, &i));
    SMITH_CHECK(!smith_text_parse_i64(root->items[7]->text, &i)); // 1e2 is a double, not an integer
    SMITH_CHECK(smith_text_parse_f64(root->items[7]->text, &d));
    SMITH_CHECK_SAME_BITS(d, 100.0);
    SMITH_CHECK(smith_text_parse_f64(root->items[5]->text, &d)); // an integer too big for u64 is still a double
    SMITH_CHECK_SAME_BITS(d, 18446744073709551616.0);
    SMITH_CHECK(!smith_text_parse_f64(root->items[9]->text, &d)); // out of range as a double: refused
    SMITH_CHECK(!smith_text_parse_f64(root->items[10]->text, &d));
    SMITH_CHECK(smith_text_parse_f64(root->items[11]->text, &d));
    SMITH_CHECK_SAME_BITS(d, 0.0);
}

// ---------------------------------------------------------------------------------------------------------------
// Limits

static bool test_json_parses(const char *document, SmithJsonLimits limits, SmithJsonError *error) {
    SmithArena arena = smith_test_arena(1 << 16);
    const SmithJsonValue *root = nullptr;
    return smith_json_parse(&arena, test_json_text(document), limits, &root, error);
}

void test_json_depth_limit(void) {
    SmithJsonError error = {};
    SmithJsonLimits limits = {.depth = 3, .string_length = 100, .count = 100};
    SMITH_CHECK(test_json_parses("[[[]]]", limits, &error));
    SMITH_CHECK(test_json_parses("[[{\"a\":1}]]", limits, &error));
    SMITH_CHECK(!test_json_parses("[[[[]]]]", limits, &error));
    SMITH_CHECK(error.reason == SmithJsonReason_TooDeep);
    SMITH_CHECK(error.offset, ==, 3); // the fourth bracket
    SMITH_CHECK(!test_json_parses("{\"a\":{\"b\":{\"c\":{}}}}", limits, &error));
    SMITH_CHECK(error.reason == SmithJsonReason_TooDeep);
    SmithJsonLimits scalars = {.depth = 0, .string_length = 100, .count = 100};
    SMITH_CHECK(test_json_parses("12", scalars, &error));
    SMITH_CHECK(!test_json_parses("[]", scalars, &error));
    SMITH_CHECK(error.reason == SmithJsonReason_TooDeep);
    SMITH_CHECK(error.offset, ==, 0);
    SmithJsonLimits deepest = {.depth = smith_json_depth_max, .string_length = 10, .count = 2000};
    char many[smith_json_depth_max * 2 + 1];
    memset(many, '[', smith_json_depth_max);
    memset(many + smith_json_depth_max, ']', smith_json_depth_max);
    many[smith_json_depth_max * 2] = '\0';
    SMITH_CHECK(test_json_parses(many, deepest, &error));
}

void test_json_string_length_limit(void) {
    SmithJsonError error = {};
    SmithJsonLimits limits = {.depth = 4, .string_length = 4, .count = 100};
    SMITH_CHECK(test_json_parses("\"abcd\"", limits, &error));
    SMITH_CHECK(!test_json_parses("\"abcde\"", limits, &error));
    SMITH_CHECK(error.reason == SmithJsonReason_StringTooLong);
    SMITH_CHECK(error.offset, ==, 5);
    SMITH_CHECK(test_json_parses("\"\\n\\n\\n\\n\"", limits, &error));   // 8 bytes written, 4 decoded
    SMITH_CHECK(test_json_parses("\"\\u00e9\\u00e9\"", limits, &error)); // two bytes each
    SMITH_CHECK(!test_json_parses("\"\\u00e9\\u00e9x\"", limits, &error));
    SMITH_CHECK(test_json_parses("\"\\ud834\\udd1e\"", limits, &error)); // four bytes
    SMITH_CHECK(!test_json_parses("\"a\\ud834\\udd1e\"", limits, &error));
    SMITH_CHECK(!test_json_parses("{\"abcde\": 1}", limits, &error)); // keys are strings
    SMITH_CHECK(error.reason == SmithJsonReason_StringTooLong);
    SmithJsonLimits none = {.depth = 4, .string_length = 0, .count = 100};
    SMITH_CHECK(test_json_parses("[\"\", {\"\": \"\"}]", none, &error));
    SMITH_CHECK(!test_json_parses("\"a\"", none, &error));
}

void test_json_count_limit(void) {
    SmithJsonError error = {};
    SmithJsonLimits limits = {.depth = 4, .string_length = 10, .count = 4};
    SMITH_CHECK(test_json_parses("[1,2,3]", limits, &error)); // the array and three numbers
    SMITH_CHECK(!test_json_parses("[1,2,3,4]", limits, &error));
    SMITH_CHECK(error.reason == SmithJsonReason_TooMany);
    SMITH_CHECK(error.offset, ==, 7);
    SMITH_CHECK(test_json_parses("{\"a\":1,\"b\":2,\"c\":3}", limits, &error)); // keys are not values
    SMITH_CHECK(!test_json_parses("{\"a\":1,\"b\":2,\"c\":3,\"d\":4}", limits, &error));
    SMITH_CHECK(error.reason == SmithJsonReason_TooMany);
    SmithJsonLimits nothing = {.depth = 4, .string_length = 10, .count = 0};
    SMITH_CHECK(!test_json_parses("0", nothing, &error));
    SMITH_CHECK(error.reason == SmithJsonReason_TooMany);
}

// ---------------------------------------------------------------------------------------------------------------
// The tree

void test_json_tree_of_a_document(void) {
    SmithArena arena = smith_test_arena(1 << 16);
    const SmithJsonValue *root = test_json_parse_ok(
        &arena, "{\"name\": \"smith\", \"tags\": [\"c\", \"json\"], \"n\": 42, \"ok\": true, \"no\": false, "
                "\"none\": null, \"nested\": {\"a\": {\"b\": [] }}, \"n\": 43, \"esc\\u0041\": 1}");
    SMITH_CHECK(root->kind == SmithJsonKind_Object);
    SMITH_CHECK(root->count, ==, 9);
    SMITH_CHECK(root->offset, ==, 0);
    SMITH_CHECK(root->span, ==, 14);
    const SmithJsonValue *name = smith_json_member(root, "name");
    SMITH_CHECK(name != nullptr && name->kind == SmithJsonKind_String && smith_text_is(name->text, "smith"));
    SMITH_CHECK(smith_text_is(name->key, "name"));
    const SmithJsonValue *tags = smith_json_member(root, "tags");
    SMITH_CHECK(tags != nullptr && tags->kind == SmithJsonKind_Array && tags->count == 2);
    SMITH_CHECK(smith_text_is(tags->items[1]->text, "json"));
    SMITH_CHECK(tags->items[1]->key.length, ==, 0);
    SMITH_CHECK(smith_json_member(root, "ok")->kind == SmithJsonKind_True);
    SMITH_CHECK(smith_json_member(root, "no")->kind == SmithJsonKind_False);
    SMITH_CHECK(smith_json_member(root, "none")->kind == SmithJsonKind_Null);
    SMITH_CHECK(smith_text_is(smith_json_member(root, "n")->text, "43")); // the last of a repeated key
    SMITH_CHECK(smith_json_member(root, "missing") == nullptr);
    SMITH_CHECK(smith_json_member(root, "escA") != nullptr); // keys are decoded
    SMITH_CHECK(smith_json_member(tags, "x") == nullptr);    // not an object
    const SmithJsonValue *b = smith_json_member(smith_json_member(smith_json_member(root, "nested"), "a"), "b");
    SMITH_CHECK(b != nullptr && b->kind == SmithJsonKind_Array && b->count == 0);
}

void test_json_tree_does_not_touch_the_arena_on_failure(void) {
    SmithArena arena = smith_test_arena(1 << 16);
    (void)smith_arena_push(&arena, sizeof(uint64_t), alignof(uint64_t));
    size_t used = arena.used;
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    SMITH_CHECK(!smith_json_parse(&arena, test_json_text("[1, 2, \"a\\n\", ]"), test_json_wide, &root, &error));
    SMITH_CHECK(arena.used, ==, used);
    SMITH_CHECK(root == nullptr);
}

void test_json_tree_that_does_not_fit_is_an_error(void) {
    alignas(smith_arena_align_max) unsigned char memory[1024];
    SmithArena arena = smith_arena_make(memory, sizeof memory);
    char big[2048];
    size_t used = 0;
    big[used++] = '[';
    for (int i = 0; i < 400; i++) {
        big[used++] = '1';
        big[used++] = ',';
    }
    big[used - 1] = ']';
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    SMITH_CHECK(!smith_json_parse(&arena, (SmithText){big, used}, test_json_wide, &root, &error));
    SMITH_CHECK(error.reason == SmithJsonReason_OutOfMemory);
    SMITH_CHECK(arena.used, ==, 0);
    SMITH_CHECK(smith_json_parse(&arena, test_json_text("[1,2,3]"), test_json_wide, &root, &error)); // still usable
}

void test_json_a_large_array_keeps_its_order(void) {
    SmithArena arena = smith_test_arena(1 << 20);
    char text[8192];
    size_t used = 0;
    text[used++] = '[';
    for (size_t i = 0; i < 1000; i++) {
        if (i > 0) text[used++] = ',';
        used += smith_text_write_u64(text + used, i * i);
    }
    text[used++] = ']';
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    SmithJsonLimits limits = {.depth = 2, .string_length = 1, .count = 1001};
    SMITH_CHECK(smith_json_parse(&arena, (SmithText){text, used}, limits, &root, &error));
    SMITH_CHECK(root->count, ==, 1000);
    for (size_t i = 0; i < 1000; i++) {
        uint64_t value = 0;
        SMITH_CHECK(smith_text_parse_u64(root->items[i]->text, &value));
        SMITH_CHECK(value, ==, i * i);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The writer

static bool test_json_writer_is(SmithJsonWriter *writer, const char *expected) {
    SmithText output = {};
    if (!smith_json_writer_finish(writer, &output)) return false;
    return output.length == strlen(expected) && memcmp(output.begin, expected, output.length) == 0;
}

void test_json_writer_compact(void) {
    char buffer[256];
    SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 0);
    smith_json_write_object_begin(&writer);
    smith_json_write_key(&writer, test_json_text("a"));
    smith_json_write_array_begin(&writer);
    smith_json_write_u64(&writer, 1);
    smith_json_write_i64(&writer, -2);
    smith_json_write_f64(&writer, 0.5);
    smith_json_write_bool(&writer, true);
    smith_json_write_bool(&writer, false);
    smith_json_write_null(&writer);
    smith_json_write_string(&writer, test_json_text("s"));
    smith_json_write_array_end(&writer);
    smith_json_write_key(&writer, test_json_text("e"));
    smith_json_write_object_begin(&writer);
    smith_json_write_object_end(&writer);
    smith_json_write_key(&writer, test_json_text("f"));
    smith_json_write_array_begin(&writer);
    smith_json_write_array_end(&writer);
    smith_json_write_object_end(&writer);
    SMITH_CHECK(test_json_writer_is(&writer, "{\"a\":[1,-2,0.5,true,false,null,\"s\"],\"e\":{},\"f\":[]}"));
}

void test_json_writer_indents(void) {
    char buffer[256];
    SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 2);
    smith_json_write_object_begin(&writer);
    smith_json_write_key(&writer, test_json_text("a"));
    smith_json_write_array_begin(&writer);
    smith_json_write_u64(&writer, 1);
    smith_json_write_object_begin(&writer);
    smith_json_write_key(&writer, test_json_text("b"));
    smith_json_write_null(&writer);
    smith_json_write_object_end(&writer);
    smith_json_write_array_end(&writer);
    smith_json_write_key(&writer, test_json_text("c"));
    smith_json_write_array_begin(&writer);
    smith_json_write_array_end(&writer);
    smith_json_write_object_end(&writer);
    const char *expected = "{\n  \"a\": [\n    1,\n    {\n      \"b\": null\n    }\n  ],\n  \"c\": []\n}";
    SMITH_CHECK(test_json_writer_is(&writer, expected));
    SmithJsonWriter scalar = smith_json_writer_make(buffer, sizeof buffer, 4);
    smith_json_write_u64(&scalar, 7);
    SMITH_CHECK(test_json_writer_is(&scalar, "7"));
}

static bool test_json_string_writes(const char *bytes, size_t length, const char *expected) {
    char buffer[128];
    SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 0);
    smith_json_write_string(&writer, (SmithText){bytes, length});
    return test_json_writer_is(&writer, expected);
}

void test_json_writer_escapes(void) {
    SMITH_CHECK(test_json_string_writes("", 0, "\"\""));
    SMITH_CHECK(test_json_string_writes("a\"b\\c/d", 7, "\"a\\\"b\\\\c/d\""));
    SMITH_CHECK(test_json_string_writes("\b\f\n\r\t", 5, "\"\\b\\f\\n\\r\\t\""));
    SMITH_CHECK(test_json_string_writes("\x01\x1f", 2, "\"\\u0001\\u001f\""));
    SMITH_CHECK(test_json_string_writes("\0x", 2, "\"\\u0000x\""));
    SMITH_CHECK(test_json_string_writes("\x7f", 1, "\"\x7f\""));
    SMITH_CHECK(test_json_string_writes("\xc3\xa9\xf0\x9d\x84\x9e", 6, "\"\xc3\xa9\xf0\x9d\x84\x9e\""));
    SMITH_CHECK(test_json_string_writes("\xe2\x80\xa8", 3, "\"\xe2\x80\xa8\""));
}

void test_json_writer_refuses_invalid_utf8(void) {
    const char *bad[] = {"\xff", "\xc0\x80", "\xed\xa0\x80", "\xe2\x82", "ok\x80"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char buffer[64];
        SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 0);
        smith_json_write_string(&writer, test_json_text(bad[i]));
        SmithText output = {};
        SMITH_CHECK(!smith_json_writer_finish(&writer, &output));
        SMITH_CHECK(writer.error.reason == SmithJsonReason_BadUtf8);
    }
}

void test_json_writer_numbers(void) {
    char buffer[512];
    SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 0);
    smith_json_write_array_begin(&writer);
    smith_json_write_u64(&writer, UINT64_MAX);
    smith_json_write_i64(&writer, INT64_MIN);
    smith_json_write_i64(&writer, INT64_MAX);
    smith_json_write_f64(&writer, -0.0);
    smith_json_write_f64(&writer, 0.1);
    smith_json_write_f64(&writer, 1e300);
    smith_json_write_f64(&writer, 5e-324);
    smith_json_write_array_end(&writer);
    SMITH_CHECK(test_json_writer_is(
        &writer, "[18446744073709551615,-9223372036854775808,9223372036854775807,-0,0.1,1e300,5e-324]"));
}

void test_json_writer_refuses_nan_and_infinity(void) {
    double values[] = {__builtin_inf(), -__builtin_inf(), __builtin_nan("")};
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
        char buffer[64];
        SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 0);
        smith_json_write_array_begin(&writer);
        smith_json_write_f64(&writer, values[i]);
        SMITH_CHECK(writer.error.reason == SmithJsonReason_NotFinite);
        smith_json_write_f64(&writer, 1.0);
        smith_json_write_array_end(&writer);
        SmithText output = {};
        SMITH_CHECK(!smith_json_writer_finish(&writer, &output));
        SMITH_CHECK(writer.error.reason == SmithJsonReason_NotFinite); // the first mistake is kept
    }
}

static SmithJsonError test_json_misuse(int which) {
    char buffer[64];
    SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 0);
    SmithText output = {};
    switch (which) {
    case 0:
        smith_json_write_key(&writer, test_json_text("a"));
        break; // a key outside an object
    case 1:
        smith_json_write_object_end(&writer);
        break; // an end that closes nothing
    case 2:
        smith_json_write_array_begin(&writer);
        smith_json_write_object_end(&writer);
        break; // the wrong end
    case 3:
        smith_json_write_object_begin(&writer);
        smith_json_write_u64(&writer, 1);
        break; // a value for a key
    case 4:
        smith_json_write_object_begin(&writer);
        smith_json_write_key(&writer, test_json_text("a"));
        smith_json_write_object_end(&writer);
        break; // an end after a key
    case 5:
        smith_json_write_u64(&writer, 1);
        smith_json_write_u64(&writer, 2);
        break; // two documents
    case 6:
        smith_json_write_array_begin(&writer);
        smith_json_write_key(&writer, test_json_text("a"));
        break;
    case 7:
        smith_json_write_object_begin(&writer);
        smith_json_write_key(&writer, test_json_text("a"));
        smith_json_write_key(&writer, test_json_text("b"));
        break; // two keys in a row
    case 8:
        smith_json_write_array_begin(&writer);
        break; // never finished
    default:
        break; // 9: nothing written
    }
    SMITH_CHECK(!smith_json_writer_finish(&writer, &output));
    return writer.error;
}

void test_json_writer_refuses_misplaced_calls(void) {
    for (int which = 0; which < 10; which++) {
        smith_test_note("case %d", which);
        SmithJsonError error = test_json_misuse(which);
        SMITH_CHECK(error.reason == SmithJsonReason_Misplaced);
    }
}

void test_json_writer_full_buffer(void) {
    char buffer[8];
    SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 0);
    smith_json_write_array_begin(&writer);
    smith_json_write_string(&writer, test_json_text("abcdef")); // 8 characters with its quotes: one too many
    SMITH_CHECK(writer.error.reason == SmithJsonReason_Full);
    SmithText output = {};
    SMITH_CHECK(!smith_json_writer_finish(&writer, &output));
    SMITH_CHECK(writer.length, <=, sizeof buffer);
    char exact[7];
    SmithJsonWriter fits = smith_json_writer_make(exact, sizeof exact, 0);
    smith_json_write_array_begin(&fits);
    smith_json_write_string(&fits, test_json_text("abcde"));
    smith_json_write_array_end(&fits);
    SMITH_CHECK(!smith_json_writer_finish(&fits, &output)); // "[" + 7 + "]" is 9
    char sized[9];
    SmithJsonWriter enough = smith_json_writer_make(sized, sizeof sized, 0);
    smith_json_write_array_begin(&enough);
    smith_json_write_string(&enough, test_json_text("abcde"));
    smith_json_write_array_end(&enough);
    SMITH_CHECK(smith_json_writer_finish(&enough, &output));
    SMITH_CHECK(output.length, ==, 9);
}

void test_json_writer_depth(void) {
    static char buffer[smith_json_depth_max * 2 + 8];
    SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 0);
    for (size_t i = 0; i < smith_json_depth_max; i++) {
        smith_json_write_array_begin(&writer);
    }
    SMITH_CHECK(writer.error.reason == SmithJsonReason_None);
    smith_json_write_array_begin(&writer);
    SMITH_CHECK(writer.error.reason == SmithJsonReason_TooDeep);
}

// ---------------------------------------------------------------------------------------------------------------
// Property tests: a random document, written, then read

constexpr size_t test_json_events_max = 160;
constexpr size_t test_json_string_max = 24;

typedef enum { TestJsonNumber_U64, TestJsonNumber_I64, TestJsonNumber_F64 } TestJsonNumber;

typedef struct {
    SmithJsonToken token;
    char string[test_json_string_max * 4 + 1]; // a Key or String, as UTF-8
    size_t length;
    TestJsonNumber number;
    uint64_t bits; // a number's value: the integer, or the double's bits
} TestJsonEvent;

typedef struct {
    TestJsonEvent events[test_json_events_max];
    size_t count;
} TestJsonDocument;

// A random code point that is not a surrogate and, optionally, not zero.
static uint32_t test_json_code_point(SmithKey key) {
    int64_t band = test_json_uniform(smith_key_derive(key, 0), 0, 9);
    SmithKey pick = smith_key_derive(key, 1);
    if (band <= 3) return (uint32_t)test_json_uniform(pick, 0x20, 0x7e);
    if (band == 4) return (uint32_t)test_json_uniform(pick, 1, 0x1f);
    if (band == 5) return (uint32_t)(unsigned char)"\"\\/\x7f"[test_json_uniform(pick, 0, 3)];
    if (band == 6) return (uint32_t)test_json_uniform(pick, 0x80, 0x7ff);
    if (band == 7) {
        uint32_t code = (uint32_t)test_json_uniform(pick, 0x800, 0xffff - 0x800);
        return code >= 0xd800 && code <= 0xdfff ? code + 0x800 : code;
    }
    return (uint32_t)test_json_uniform(pick, 0x10000, 0x10ffff);
}

static size_t test_json_utf8(uint32_t code, char *out) {
    if (code < 0x80) {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800) {
        out[0] = (char)(0xc0 | (code >> 6));
        out[1] = (char)(0x80 | (code & 0x3f));
        return 2;
    }
    if (code < 0x10000) {
        out[0] = (char)(0xe0 | (code >> 12));
        out[1] = (char)(0x80 | ((code >> 6) & 0x3f));
        out[2] = (char)(0x80 | (code & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (code >> 18));
    out[1] = (char)(0x80 | ((code >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((code >> 6) & 0x3f));
    out[3] = (char)(0x80 | (code & 0x3f));
    return 4;
}

static void test_json_random_string(SmithKey key, TestJsonEvent *event) {
    size_t length = (size_t)test_json_uniform(smith_key_derive(key, 0), 0, test_json_string_max);
    event->length = 0;
    for (size_t i = 0; i < length; i++) {
        event->length +=
            test_json_utf8(test_json_code_point(smith_key_derive(key, 1 + i)), event->string + event->length);
    }
    event->string[event->length] = '\0';
}

static uint64_t test_json_random_double_bits(SmithKey key) {
    int64_t kind = test_json_uniform(smith_key_derive(key, 0), 0, 5);
    if (kind == 0) { // a short decimal, the common case
        double d = (double)test_json_uniform(smith_key_derive(key, 1), -100000, 100000) / 100.0;
        return __builtin_bit_cast(uint64_t, d);
    }
    uint64_t bits = smith_key_bits(smith_key_derive(key, 2), 0);
    if (kind == 1) bits &= ~(0x7ffULL << 52); // subnormal
    double d = __builtin_bit_cast(double, bits);
    if (d != d || d > DBL_MAX || d < -DBL_MAX) bits &= ~(0x400ULL << 52); // finite: clear an exponent bit
    return bits;
}

// Writes the event to the writer, and appends it to the document.
static void test_json_emit(SmithJsonWriter *writer, TestJsonDocument *document, const TestJsonEvent *event) {
    document->events[document->count++] = *event;
    SmithText text = {event->string, event->length};
    switch (event->token) {
    case SmithJsonToken_ObjectBegin:
        smith_json_write_object_begin(writer);
        break;
    case SmithJsonToken_ObjectEnd:
        smith_json_write_object_end(writer);
        break;
    case SmithJsonToken_ArrayBegin:
        smith_json_write_array_begin(writer);
        break;
    case SmithJsonToken_ArrayEnd:
        smith_json_write_array_end(writer);
        break;
    case SmithJsonToken_Key:
        smith_json_write_key(writer, text);
        break;
    case SmithJsonToken_String:
        smith_json_write_string(writer, text);
        break;
    case SmithJsonToken_True:
        smith_json_write_bool(writer, true);
        break;
    case SmithJsonToken_False:
        smith_json_write_bool(writer, false);
        break;
    case SmithJsonToken_Null:
        smith_json_write_null(writer);
        break;
    case SmithJsonToken_Number:
        if (event->number == TestJsonNumber_U64) smith_json_write_u64(writer, event->bits);
        if (event->number == TestJsonNumber_I64) smith_json_write_i64(writer, (int64_t)event->bits);
        if (event->number == TestJsonNumber_F64) smith_json_write_f64(writer, __builtin_bit_cast(double, event->bits));
        break;
    case SmithJsonToken_Error:
    case SmithJsonToken_End:
        break;
    }
}

typedef struct {
    bool object;
    size_t remaining;
} TestJsonFrame;

// A random value, as events, with an explicit stack of the containers it is inside.
static void test_json_random_value(SmithJsonWriter *writer, TestJsonDocument *document, SmithKey key,
                                   size_t depth_max) {
    TestJsonFrame stack[16];
    size_t depth = 0;
    bool started = false;
    for (size_t step = 0; document->count + 16 < test_json_events_max; step++) {
        SmithKey here = smith_key_derive(key, step);
        TestJsonEvent event = {};
        if (started && depth == 0) break;
        if (depth > 0 && stack[depth - 1].remaining == 0) {
            event.token = stack[depth - 1].object ? SmithJsonToken_ObjectEnd : SmithJsonToken_ArrayEnd;
            depth -= 1;
            test_json_emit(writer, document, &event);
            continue;
        }
        if (depth > 0) {
            stack[depth - 1].remaining -= 1;
            if (stack[depth - 1].object) {
                event.token = SmithJsonToken_Key;
                test_json_random_string(smith_key_derive(here, 10), &event);
                test_json_emit(writer, document, &event);
                event = (TestJsonEvent){};
            }
        }
        started = true;
        int64_t kind = test_json_uniform(smith_key_derive(here, 0), 0, 9);
        bool room = depth < depth_max && document->count + 24 < test_json_events_max;
        if (kind <= 1 && room) {
            bool object = kind == 1;
            event.token = object ? SmithJsonToken_ObjectBegin : SmithJsonToken_ArrayBegin;
            stack[depth++] = (TestJsonFrame){object, (size_t)test_json_uniform(smith_key_derive(here, 1), 0, 4)};
        } else if (kind == 2) {
            event.token = SmithJsonToken_String;
            test_json_random_string(smith_key_derive(here, 2), &event);
        } else if (kind == 3) {
            event.token = (SmithJsonToken)(SmithJsonToken_True + test_json_uniform(smith_key_derive(here, 3), 0, 2));
        } else {
            event.token = SmithJsonToken_Number;
            event.number = (TestJsonNumber)test_json_uniform(smith_key_derive(here, 4), 0, 2);
            uint64_t bits = smith_key_bits(smith_key_derive(here, 5), 0);
            bits >>= smith_key_bits(smith_key_derive(here, 6), 0) % 64; // every length of integer
            event.bits =
                event.number == TestJsonNumber_F64 ? test_json_random_double_bits(smith_key_derive(here, 7)) : bits;
        }
        test_json_emit(writer, document, &event);
    }
    while (depth > 0) { // out of budget: close what is open (an object with a pending key never occurs here)
        TestJsonEvent event = {.token = stack[depth - 1].object ? SmithJsonToken_ObjectEnd : SmithJsonToken_ArrayEnd};
        depth -= 1;
        test_json_emit(writer, document, &event);
    }
}

static bool test_json_number_matches(SmithText text, const TestJsonEvent *event) {
    if (event->number == TestJsonNumber_U64) {
        uint64_t value = 0;
        return smith_text_parse_u64(text, &value) && value == event->bits;
    }
    if (event->number == TestJsonNumber_I64) {
        int64_t value = 0;
        return smith_text_parse_i64(text, &value) && (uint64_t)value == event->bits;
    }
    double value = 0;
    return smith_text_parse_f64(text, &value) && __builtin_bit_cast(uint64_t, value) == event->bits;
}

// The pull parser sees exactly the events.
static void test_json_check_reader(SmithText input, const TestJsonDocument *document, SmithArena *arena) {
    SmithJsonReader reader = smith_json_reader_make(input, test_json_wide);
    for (size_t i = 0; i < document->count; i++) {
        const TestJsonEvent *event = &document->events[i];
        smith_test_note("event %zu of %zu in %.*s", i, document->count,
                        (int)input.length > 600 ? 600 : (int)input.length, input.begin);
        SMITH_CHECK(smith_json_read(&reader) == event->token);
        if (event->token == SmithJsonToken_Key || event->token == SmithJsonToken_String) {
            SmithText text = smith_json_decode(&reader, arena);
            SMITH_CHECK(text.length, ==, event->length);
            SMITH_CHECK(memcmp(text.begin, event->string, event->length), ==, 0);
            if (reader.escaped) SMITH_CHECK(text.begin[text.length] == '\0');
            SMITH_CHECK(reader.length, ==, event->length);
        }
        if (event->token == SmithJsonToken_Number) SMITH_CHECK(test_json_number_matches(reader.text, event));
    }
    SMITH_CHECK(smith_json_read(&reader) == SmithJsonToken_End);
}

// The tree holds exactly the events, in order: walked with a stack of (container, next child).
static void test_json_check_tree(const SmithJsonValue *root, const TestJsonDocument *document) {
    const SmithJsonValue *containers[32];
    size_t next_child[32];
    size_t depth = 0;
    const SmithJsonValue *value = root;
    bool pending_key = false;
    for (size_t i = 0; i < document->count; i++) {
        const TestJsonEvent *event = &document->events[i];
        if (event->token == SmithJsonToken_ObjectEnd || event->token == SmithJsonToken_ArrayEnd) {
            SMITH_CHECK(depth > 0 && next_child[depth - 1] == containers[depth - 1]->count);
            depth -= 1;
            continue;
        }
        if (event->token == SmithJsonToken_Key) {
            SMITH_CHECK(depth > 0 && next_child[depth - 1] < containers[depth - 1]->count);
            value = containers[depth - 1]->items[next_child[depth - 1]];
            SMITH_CHECK(value->key.length, ==, event->length);
            SMITH_CHECK(memcmp(value->key.begin, event->string, event->length), ==, 0);
            pending_key = true;
            continue;
        }
        if (!pending_key && depth > 0) {
            SMITH_CHECK(next_child[depth - 1] < containers[depth - 1]->count);
            value = containers[depth - 1]->items[next_child[depth - 1]];
        }
        if (depth > 0) next_child[depth - 1] += 1;
        pending_key = false;
        switch (event->token) {
        case SmithJsonToken_ObjectBegin:
        case SmithJsonToken_ArrayBegin:
            SMITH_CHECK(value->kind ==
                        (event->token == SmithJsonToken_ObjectBegin ? SmithJsonKind_Object : SmithJsonKind_Array));
            containers[depth] = value;
            next_child[depth] = 0;
            depth += 1;
            break;
        case SmithJsonToken_String:
            SMITH_CHECK(value->kind == SmithJsonKind_String);
            SMITH_CHECK(value->text.length, ==, event->length);
            SMITH_CHECK(memcmp(value->text.begin, event->string, event->length), ==, 0);
            break;
        case SmithJsonToken_Number:
            SMITH_CHECK(value->kind == SmithJsonKind_Number);
            SMITH_CHECK(test_json_number_matches(value->text, event));
            break;
        case SmithJsonToken_True:
            SMITH_CHECK(value->kind == SmithJsonKind_True);
            break;
        case SmithJsonToken_False:
            SMITH_CHECK(value->kind == SmithJsonKind_False);
            break;
        case SmithJsonToken_Null:
            SMITH_CHECK(value->kind == SmithJsonKind_Null);
            break;
        default:
            SMITH_CHECK(false);
            break;
        }
    }
    SMITH_CHECK(depth, ==, 0);
}

void test_json_any_value_written_then_read_is_the_same(SmithKey key) {
    SmithArena arena = smith_test_arena(1 << 20);
    static char buffer[1 << 16];
    size_t indent = (size_t)test_json_uniform(smith_key_derive(key, 90), 0, 1) * 2;
    SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, indent);
    TestJsonDocument *document = smith_arena_push(&arena, sizeof(TestJsonDocument), alignof(TestJsonDocument));
    test_json_random_value(&writer, document, smith_key_derive(key, 91),
                           (size_t)test_json_uniform(smith_key_derive(key, 92), 0, 6));
    SmithText output = {};
    SMITH_CHECK(smith_json_writer_finish(&writer, &output));
    test_json_check_reader(output, document, &arena);
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    SMITH_CHECK(smith_json_parse(&arena, output, test_json_wide, &root, &error));
    test_json_check_tree(root, document);
    SmithJsonReader counter = smith_json_reader_make(output, test_json_wide);
    while (smith_json_read(&counter) != SmithJsonToken_End) {
    }
    SMITH_CHECK(root->span, ==, counter.values);
}

void test_json_write_value_puts_a_tree_back_as_it_was_written(SmithKey key) {
    SmithArena arena = smith_test_arena(1 << 20);
    static char first[1 << 16];
    static char second[1 << 16];
    size_t indent = (size_t)test_json_uniform(smith_key_derive(key, 90), 0, 1) * 2;
    SmithJsonWriter writer = smith_json_writer_make(first, sizeof first, indent);
    TestJsonDocument *document = smith_arena_push(&arena, sizeof(TestJsonDocument), alignof(TestJsonDocument));
    test_json_random_value(&writer, document, smith_key_derive(key, 91),
                           (size_t)test_json_uniform(smith_key_derive(key, 92), 0, 6));
    SmithText output = {};
    SMITH_CHECK(smith_json_writer_finish(&writer, &output));
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    SMITH_CHECK(smith_json_parse(&arena, output, test_json_wide, &root, &error));
    SmithJsonWriter again = smith_json_writer_make(second, sizeof second, indent);
    smith_json_write_value(&again, root);
    SmithText copy = {};
    SMITH_CHECK(smith_json_writer_finish(&again, &copy));
    SMITH_CHECK(copy.length, ==, output.length);
    SMITH_CHECK(memcmp(copy.begin, output.begin, output.length) == 0);
}

void test_json_write_value_keeps_numbers_and_duplicate_keys(void) {
    SmithArena arena = smith_test_arena(1 << 14);
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    SmithText input = test_json_text("{\"a\":1.50,\"a\":[],\"b\\u00e9\":[1e2,{},\"x\\ny\"],\"c\":null}");
    SMITH_CHECK(smith_json_parse(&arena, input, test_json_wide, &root, &error));
    char buffer[256];
    SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 0);
    smith_json_write_value(&writer, root);
    SmithText out = {};
    SMITH_CHECK(smith_json_writer_finish(&writer, &out));
    SMITH_CHECK(smith_text_is(out, "{\"a\":1.50,\"a\":[],\"b\xc3\xa9\":[1e2,{},\"x\\ny\"],\"c\":null}"));
    SmithJsonWriter scalar = smith_json_writer_make(buffer, sizeof buffer, 0);
    smith_json_write_value(&scalar, root->items[0]);
    SMITH_CHECK(smith_json_writer_finish(&scalar, &out));
    SMITH_CHECK(smith_text_is(out, "1.50"));
    SmithJsonWriter tiny = smith_json_writer_make(buffer, 5, 0);
    smith_json_write_value(&tiny, root);
    SMITH_CHECK(!smith_json_writer_finish(&tiny, &out));
    SMITH_CHECK(tiny.error.reason == SmithJsonReason_Full);
}

void test_json_any_double_round_trips_bit_for_bit(SmithKey key) {
    uint64_t bits = test_json_random_double_bits(key);
    if (smith_bernoulli_sample(smith_key_derive(key, 5), (SmithBernoulli){.probability = 0.5}))
        bits = smith_key_bits(smith_key_derive(key, 6), 0);
    double d = __builtin_bit_cast(double, bits);
    if (d != d || d > DBL_MAX || d < -DBL_MAX) return;
    char buffer[64];
    SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 0);
    smith_json_write_f64(&writer, d);
    SmithText output = {};
    SMITH_CHECK(smith_json_writer_finish(&writer, &output));
    SmithArena arena = smith_test_arena(1 << 12);
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    SMITH_CHECK(smith_json_parse(&arena, output, test_json_wide, &root, &error));
    double back = 0;
    SMITH_CHECK(smith_text_parse_f64(root->text, &back));
    SMITH_CHECK_SAME_BITS(back, d);
}

void test_json_any_integer_round_trips_exactly(SmithKey key) {
    uint64_t bits = smith_key_bits(smith_key_derive(key, 0), 0) >> (smith_key_bits(smith_key_derive(key, 1), 0) % 64);
    char buffer[64];
    SmithJsonWriter writer = smith_json_writer_make(buffer, sizeof buffer, 0);
    smith_json_write_array_begin(&writer);
    smith_json_write_u64(&writer, bits);
    smith_json_write_i64(&writer, (int64_t)bits);
    smith_json_write_array_end(&writer);
    SmithText output = {};
    SMITH_CHECK(smith_json_writer_finish(&writer, &output));
    SmithArena arena = smith_test_arena(1 << 12);
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    SMITH_CHECK(smith_json_parse(&arena, output, test_json_wide, &root, &error));
    uint64_t u = 0;
    int64_t i = 0;
    SMITH_CHECK(smith_text_parse_u64(root->items[0]->text, &u));
    SMITH_CHECK(smith_text_parse_i64(root->items[1]->text, &i));
    SMITH_CHECK(u, ==, bits);
    SMITH_CHECK((uint64_t)i, ==, bits);
}

// ---------------------------------------------------------------------------------------------------------------
// Fuzzing: damaged documents

// Damages the bytes in place and returns the new length; every byte comes from the key.
static size_t test_json_damage(SmithKey key, char *bytes, size_t length, size_t capacity) {
    static const char interesting[] = "[]{},:\"\\ \n-+.eE0123456789tfnu\xff\xc3\xed\xf4\x80";
    size_t edits = (size_t)test_json_uniform(smith_key_derive(key, 0), 1, 4);
    for (size_t e = 0; e < edits && length > 0; e++) {
        SmithKey edit = smith_key_derive(key, 1 + e);
        size_t at = (size_t)test_json_uniform(smith_key_derive(edit, 0), 0, (int64_t)length - 1);
        char byte = interesting[test_json_uniform(smith_key_derive(edit, 1), 0, (int64_t)sizeof interesting - 2)];
        switch (test_json_uniform(smith_key_derive(edit, 2), 0, 5)) {
        case 0:
            bytes[at] = byte;
            break;
        case 1:
            bytes[at] = (char)smith_key_bits(smith_key_derive(edit, 3), 0);
            break;
        case 2:
            length = at;
            break; // truncate
        case 3:    // delete one byte
            memmove(bytes + at, bytes + at + 1, length - at - 1);
            length -= 1;
            break;
        case 4: // insert one
            if (length < capacity) {
                memmove(bytes + at + 1, bytes + at, length - at);
                bytes[at] = byte;
                length += 1;
            }
            break;
        default:
            bytes[at] =
                (char)((unsigned char)bytes[at] ^ (1U << (unsigned)test_json_uniform(smith_key_derive(edit, 4), 0, 7)));
            break;
        }
    }
    return length;
}

// The reader and the tree agree on whether the bytes are a document, report an offset inside the input, and read
// nothing outside it (the input sits exactly in its own arena push, so AddressSanitizer sees a stray read).
static void test_json_judge(SmithArena *arena, const char *bytes, size_t length, SmithJsonLimits limits) {
    char *exact = smith_arena_push(arena, length == 0 ? 1 : length, 1);
    memcpy(exact, bytes, length);
    SmithText input = {exact, length};
    SmithJsonReader reader = smith_json_reader_make(input, limits);
    SmithJsonToken token = SmithJsonToken_Null;
    size_t steps = 0;
    while (token != SmithJsonToken_Error && token != SmithJsonToken_End) {
        token = smith_json_read(&reader);
        steps += 1;
        SMITH_CHECK(steps, <=, length + 2); // each token takes a byte: no hang
        if (token == SmithJsonToken_Key || token == SmithJsonToken_String) {
            SmithArenaScope decoding = smith_arena_scope_begin(arena);
            SmithText text = smith_json_decode(&reader, arena);
            SMITH_CHECK(text.length, ==, reader.length);
            SMITH_CHECK(text.length, <=, limits.string_length);
            smith_arena_scope_end(decoding);
        }
    }
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    SmithArenaScope scope = smith_arena_scope_begin(arena);
    bool parsed = smith_json_parse(arena, input, limits, &root, &error);
    SMITH_CHECK(parsed == (token == SmithJsonToken_End));
    if (parsed) {
        SMITH_CHECK(root->span, ==, reader.values);
    } else {
        SMITH_CHECK(error.reason != SmithJsonReason_None);
        SMITH_CHECK(error.offset, <=, length);
        SMITH_CHECK(error.offset, ==, reader.error.offset);
        SMITH_CHECK(error.reason == reader.error.reason);
    }
    smith_arena_scope_end(scope);
}

void test_json_damaged_documents_never_misbehave(SmithKey key) {
    SmithArena arena = smith_test_arena(1 << 22);
    SmithJsonWriter writer;
    static char buffer[1 << 14];
    writer = smith_json_writer_make(buffer, sizeof buffer, (size_t)test_json_uniform(smith_key_derive(key, 90), 0, 1));
    TestJsonDocument *document = smith_arena_push(&arena, sizeof(TestJsonDocument), alignof(TestJsonDocument));
    test_json_random_value(&writer, document, smith_key_derive(key, 91), 6);
    SmithText output = {};
    SMITH_CHECK(smith_json_writer_finish(&writer, &output));
    char damaged[1 << 14];
    memcpy(damaged, output.begin, output.length);
    size_t length = test_json_damage(smith_key_derive(key, 92), damaged, output.length, sizeof damaged);
    smith_test_note("damaged %.*s", (int)length > 400 ? 400 : (int)length, damaged);
    test_json_judge(&arena, damaged, length, test_json_wide);
    SmithJsonLimits tight = {.depth = 3, .string_length = 6, .count = 12};
    test_json_judge(&arena, damaged, length, tight);
    test_json_judge(&arena, output.begin, output.length, tight); // and an undamaged one under tight limits
}

void test_json_random_bytes_never_misbehave(SmithKey key) {
    SmithArena arena = smith_test_arena(1 << 20);
    size_t length = (size_t)test_json_uniform(smith_key_derive(key, 0), 0, 64);
    char bytes[64];
    for (size_t i = 0; i < length; i++) {
        bytes[i] = (char)smith_key_bits(smith_key_derive(key, 1 + i), 0);
        if (smith_bernoulli_sample(smith_key_derive(key, 100 + i), (SmithBernoulli){.probability = 0.6}))
            bytes[i] =
                "[]{}:,\" 0-1e.tfnu\\"[smith_key_bits(smith_key_derive(key, 200 + i), 0) % 19]; // mostly structure
    }
    test_json_judge(&arena, bytes, length, test_json_wide);
}

// ---------------------------------------------------------------------------------------------------------------
// JSONTestSuite

// What this library does with the suite's implementation-defined (i_) cases: true if it accepts the document. The
// reasons are in the comments; the document is the suite's file of that name. A change here is a change of behaviour.
typedef struct {
    const char *name;
    bool accepted;
} TestJsonImplementationDefined;

static const TestJsonImplementationDefined test_json_implementation_defined[] = {
    // Numbers are syntax only: the library keeps the text, and the conversion to a double refuses what is out of range.
    {"i_number_double_huge_neg_exp.json", true},
    {"i_number_huge_exp.json", true},
    {"i_number_neg_int_huge_exp.json", true},
    {"i_number_pos_double_huge_exp.json", true},
    {"i_number_real_neg_overflow.json", true},
    {"i_number_real_pos_overflow.json", true},
    {"i_number_real_underflow.json", true},
    {"i_number_too_big_neg_int.json", true},
    {"i_number_too_big_pos_int.json", true},
    {"i_number_very_big_negative_int.json", true},
    // A surrogate escape that is not half of a pair has no UTF-8 form: refused.
    {"i_object_key_lone_2nd_surrogate.json", false},
    {"i_string_1st_surrogate_but_2nd_missing.json", false},
    {"i_string_1st_valid_surrogate_2nd_invalid.json", false},
    {"i_string_incomplete_surrogate_and_escape_valid.json", false},
    {"i_string_incomplete_surrogate_pair.json", false},
    {"i_string_incomplete_surrogates_escape_valid.json", false},
    {"i_string_invalid_lonely_surrogate.json", false},
    {"i_string_invalid_surrogate.json", false},
    {"i_string_inverted_surrogates_U+1D11E.json", false},
    {"i_string_lone_second_surrogate.json", false},
    // Bytes that are not UTF-8, or not JSON's encoding at all: refused.
    {"i_string_UTF-16LE_with_BOM.json", false},
    {"i_string_UTF-8_invalid_sequence.json", false},
    {"i_string_UTF8_surrogate_U+D800.json", false},
    {"i_string_invalid_utf-8.json", false},
    {"i_string_iso_latin_1.json", false},
    {"i_string_lone_utf8_continuation_byte.json", false},
    {"i_string_not_in_unicode_range.json", false},
    {"i_string_overlong_sequence_2_bytes.json", false},
    {"i_string_overlong_sequence_6_bytes.json", false},
    {"i_string_overlong_sequence_6_bytes_null.json", false},
    {"i_string_truncated-utf-8.json", false},
    {"i_string_utf16BE_no_BOM.json", false},
    {"i_string_utf16LE_no_BOM.json", false},
    // 500 nested arrays: accepted while the caller's depth limit allows it (these tests ask for 512).
    {"i_structure_500_nested_arrays.json", true},
    // A byte order mark is refused.
    {"i_structure_UTF-8_BOM_empty_object.json", false},
};

typedef struct {
    size_t y_total;
    size_t y_accepted;
    size_t n_total;
    size_t n_refused;
    size_t i_total;
    size_t i_as_listed;
} TestJsonConformance;

static bool test_json_read_file(const char *path, char *bytes, size_t capacity, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return false;
    *length = fread(bytes, 1, capacity, file);
    bool ok = ferror(file) == 0 && feof(file);
    (void)fclose(file);
    return ok;
}

static bool test_json_suite_accepts(SmithArena *arena, const char *bytes, size_t length) {
    SmithJsonLimits limits = {.depth = smith_json_depth_max, .string_length = 1 << 20, .count = 1 << 20};
    SmithArenaScope scope = smith_arena_scope_begin(arena);
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    bool accepted = smith_json_parse(arena, (SmithText){bytes, length}, limits, &root, &error);
    SmithJsonReader reader = smith_json_reader_make((SmithText){bytes, length}, limits);
    SmithJsonToken token = SmithJsonToken_Null;
    while (token != SmithJsonToken_Error && token != SmithJsonToken_End) {
        token = smith_json_read(&reader);
    }
    SMITH_CHECK(accepted == (token == SmithJsonToken_End)); // the two layers agree
    smith_arena_scope_end(scope);
    return accepted;
}

static const TestJsonImplementationDefined *test_json_find_listed(const char *name) {
    for (size_t i = 0; i < sizeof test_json_implementation_defined / sizeof test_json_implementation_defined[0]; i++) {
        if (strcmp(test_json_implementation_defined[i].name, name) == 0) return &test_json_implementation_defined[i];
    }
    return nullptr;
}

// Runs every file of the suite's test_parsing folder (the path comes from the environment, because tests/json.sh
// fetches the package). Prints nothing when it passes; a failure names the file.
void test_json_conformance_with_jsontestsuite(void) {
    const char *folder = getenv("SMITH_JSON_SUITE");
    if (folder == nullptr || folder[0] == '\0') return; // not fetched: tests/json.sh says so
    SmithArena arena = smith_test_arena(1 << 26);
    static char bytes[1 << 20];
    TestJsonConformance totals = {};
    const char *listing_path = getenv("SMITH_JSON_LISTING"); // where tests/json.sh wants what was done with each case
    FILE *listing = listing_path != nullptr && listing_path[0] != '\0' ? fopen(listing_path, "w") : nullptr;
    DIR *directory = opendir(folder);
    SMITH_CHECK(directory != nullptr);
    for (struct dirent *entry = readdir(directory); entry != nullptr; entry = readdir(directory)) {
        const char *name = entry->d_name;
        size_t name_length = strlen(name);
        if (name_length < 7 || strcmp(name + name_length - 5, ".json") != 0 || name[1] != '_') continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", folder, name);
        size_t length = 0;
        smith_test_note("file %s", name);
        SMITH_CHECK(test_json_read_file(path, bytes, sizeof bytes, &length));
        bool accepted = test_json_suite_accepts(&arena, bytes, length);
        if (name[0] == 'y') {
            totals.y_total += 1;
            totals.y_accepted += accepted ? 1 : 0;
            SMITH_CHECK(accepted);
        } else if (name[0] == 'n') {
            totals.n_total += 1;
            totals.n_refused += accepted ? 0 : 1;
            SMITH_CHECK(!accepted);
        } else if (name[0] == 'i') {
            const TestJsonImplementationDefined *listed = test_json_find_listed(name);
            totals.i_total += 1;
            if (listing != nullptr) fprintf(listing, "%-9s %s\n", accepted ? "accepted" : "refused", name);
            SMITH_CHECK(listed != nullptr); // a new case must be listed
            totals.i_as_listed += listed != nullptr && listed->accepted == accepted ? 1 : 0;
            SMITH_CHECK(listed->accepted == accepted);
        }
    }
    (void)closedir(directory);
    if (listing != nullptr) {
        fprintf(listing, "y_: %zu of %zu accepted; n_: %zu of %zu refused; i_: %zu of %zu as listed\n",
                totals.y_accepted, totals.y_total, totals.n_refused, totals.n_total, totals.i_as_listed,
                totals.i_total);
        (void)fclose(listing);
    }
    smith_test_note("y %zu/%zu accepted, n %zu/%zu refused, i %zu/%zu as listed", totals.y_accepted, totals.y_total,
                    totals.n_refused, totals.n_total, totals.i_as_listed, totals.i_total);
    SMITH_CHECK(totals.y_total, ==, totals.y_accepted);
    SMITH_CHECK(totals.n_total, ==, totals.n_refused);
    SMITH_CHECK(totals.i_total, ==,
                sizeof test_json_implementation_defined / sizeof test_json_implementation_defined[0]);
    SMITH_CHECK(totals.y_total, >=, 95);
    SMITH_CHECK(totals.n_total, >=, 188);
}
