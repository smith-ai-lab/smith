#include "../include/smith/json.h"

#include <stdint.h>
#include "../include/smith/contract.h"
#include "../include/smith/trace.h"

// A check that stays in every build: an invariant of this file, not a contract a proof of a caller should read.
// Trusted, its body is never read, so the assert is a run-time check that writes the failure report (the call's line
// and the stack) in every build.
static void smith_json_check(bool holds) {
    smith_trusted();
    smith_assert(holds);
    if (!holds) __builtin_trap();
}

// A writer call that cannot be right is a bug of the caller, not data: it stops with the report, in every build.
[[noreturn]] static void smith_json_misuse(const char *what) {
    smith_failure_detail(SmithFailure_Check, what);
}

const char *smith_json_reason_text(SmithJsonReason reason) {
    switch (reason) {
    case SmithJsonReason_None:
        return "no error";
    case SmithJsonReason_UnexpectedEnd:
        return "unexpected end of input";
    case SmithJsonReason_UnexpectedCharacter:
        return "unexpected character";
    case SmithJsonReason_TrailingContent:
        return "content after the document";
    case SmithJsonReason_BadNumber:
        return "malformed number";
    case SmithJsonReason_BadEscape:
        return "invalid escape in string";
    case SmithJsonReason_BadSurrogate:
        return "surrogate escape without its pair";
    case SmithJsonReason_BadUtf8:
        return "invalid UTF-8";
    case SmithJsonReason_ControlCharacter:
        return "control character in string";
    case SmithJsonReason_TooDeep:
        return "nesting deeper than the limit";
    case SmithJsonReason_StringTooLong:
        return "string longer than the limit";
    case SmithJsonReason_TooMany:
        return "more values than the limit";
    case SmithJsonReason_Full:
        return "the output buffer is full";
    }
    return "unknown reason";
}

// ---------------------------------------------------------------------------------------------------------------
// Bits shared by reading and writing

static bool smith_json_bit(const uint64_t *bits, size_t index) {
    return ((bits[index / 64] >> (index % 64)) & 1) != 0;
}

static void smith_json_set_bit(uint64_t *bits, size_t index, bool value) {
    uint64_t mask = (uint64_t)1 << (index % 64);
    if (value)
        bits[index / 64] |= mask;
    else
        bits[index / 64] &= ~mask;
}

static bool smith_json_is_continuation(char c) {
    return ((unsigned char)c & 0xc0) == 0x80;
}

// The length of the UTF-8 sequence that starts at text[0], or 0 if it is not well formed (overlong, a surrogate, past
// U+10FFFF, truncated).
static size_t smith_json_utf8_length(const char *text, size_t available) {
    unsigned char lead = (unsigned char)text[0];
    if (lead < 0x80) return 1;
    size_t length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xbf; // the range of the second byte
    if (lead >= 0xc2 && lead <= 0xdf) {
        length = 2;
    } else if (lead >= 0xe0 && lead <= 0xef) {
        length = 3;
        if (lead == 0xe0) low = 0xa0;
        if (lead == 0xed) high = 0x9f;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        length = 4;
        if (lead == 0xf0) low = 0x90;
        if (lead == 0xf4) high = 0x8f;
    } else {
        return 0;
    }
    if (available < length) return 0;
    unsigned char second = (unsigned char)text[1];
    if (second < low || second > high) return 0;
    for (size_t i = 2; i < length; i++) {
        if (!smith_json_is_continuation(text[i])) return 0;
    }
    return length;
}

static size_t smith_json_encode(uint32_t code, char *out) {
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

// Four hex digits at text[0..4), or -1.
static int32_t smith_json_hex4(const char *text, size_t available) {
    if (available < 4) return -1;
    int32_t value = 0;
    for (size_t i = 0; i < 4; i++) {
        char c = text[i];
        int32_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            return -1;
        value = value * 16 + digit;
    }
    return value;
}

// ---------------------------------------------------------------------------------------------------------------
// The reader

enum {
    SmithJsonState_Top,         // a value is due, the first of the document
    SmithJsonState_Done,        // the document is complete
    SmithJsonState_ArrayFirst,  // a value or ]
    SmithJsonState_ArrayNext,   // , or ]
    SmithJsonState_ArrayValue,  // a value, after a comma
    SmithJsonState_ObjectFirst, // a key or }
    SmithJsonState_ObjectNext,  // , or }
    SmithJsonState_ObjectKey,   // a key, after a comma
    SmithJsonState_ObjectValue, // a value, after a key
};

SmithJsonReader smith_json_reader_make(struct smith_array_const_char_n input, SmithJsonLimits limits) {
    smith_requires(limits.depth <= smith_json_depth_max);
    smith_requires(input.data != nullptr || input.count[0] == 0);
    SmithJsonReader reader = {.input = input, .limits = limits, .state = SmithJsonState_Top};
    reader.token = SmithJsonToken_Null; // nothing read yet; not Error and not End
    return reader;
}

static SmithJsonToken smith_json_fail(SmithJsonReader *reader, size_t offset, SmithJsonReason reason) {
    reader->error = (SmithJsonError){offset, reason};
    reader->token = SmithJsonToken_Error;
    return SmithJsonToken_Error;
}

static SmithJsonToken smith_json_emit(SmithJsonReader *reader, SmithJsonToken token, size_t offset) {
    reader->token = token;
    reader->offset = offset;
    return token;
}

static bool smith_json_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void smith_json_skip_space(SmithJsonReader *reader) {
    while (reader->position < reader->input.count[0] && smith_json_is_space(reader->input.data[reader->position])) {
        reader->position += 1;
    }
}

// What follows a complete value.
static void smith_json_after_value(SmithJsonReader *reader) {
    if (reader->depth == 0)
        reader->state = SmithJsonState_Done;
    else
        reader->state =
            smith_json_bit(reader->objects, reader->depth - 1) ? SmithJsonState_ObjectNext : SmithJsonState_ArrayNext;
}

// One escape at input[at], which is the backslash. Returns the code point written to *code and the escape's length, or
// 0 with *reason set.
static size_t smith_json_escape(const struct smith_array_const_char_n input, size_t at, uint32_t *code,
                                SmithJsonReason *reason) {
    size_t available = input.count[0] - at;
    if (available < 2) {
        *reason = SmithJsonReason_UnexpectedEnd;
        return 0;
    }
    switch (input.data[at + 1]) {
    case '"':
        *code = '"';
        return 2;
    case '\\':
        *code = '\\';
        return 2;
    case '/':
        *code = '/';
        return 2;
    case 'b':
        *code = '\b';
        return 2;
    case 'f':
        *code = '\f';
        return 2;
    case 'n':
        *code = '\n';
        return 2;
    case 'r':
        *code = '\r';
        return 2;
    case 't':
        *code = '\t';
        return 2;
    case 'u':
        break;
    default:
        *reason = SmithJsonReason_BadEscape;
        return 0;
    }
    int32_t first = smith_json_hex4(input.data + at + 2, available - 2);
    if (first < 0) {
        *reason = SmithJsonReason_BadEscape;
        return 0;
    }
    if (first >= 0xdc00 && first <= 0xdfff) {
        *reason = SmithJsonReason_BadSurrogate;
        return 0;
    }
    if (first < 0xd800 || first > 0xdbff) {
        *code = (uint32_t)first;
        return 6;
    }
    if (available < 12 || input.data[at + 6] != '\\' || input.data[at + 7] != 'u') {
        *reason = SmithJsonReason_BadSurrogate;
        return 0;
    }
    int32_t second = smith_json_hex4(input.data + at + 8, available - 8);
    if (second < 0xdc00 || second > 0xdfff) {
        *reason = SmithJsonReason_BadSurrogate;
        return 0;
    }
    *code = 0x10000 + (((uint32_t)first - 0xd800) << 10) + ((uint32_t)second - 0xdc00);
    return 12;
}

// Reads the string whose opening quote is at the position; sets text, length and escaped. Returns false after failing.
static bool smith_json_lex_string(SmithJsonReader *reader) {
    struct smith_array_const_char_n input = reader->input;
    size_t at = reader->position + 1;
    size_t start = at;
    size_t length = 0;
    bool escaped = false;
    while (at < input.count[0]) {
        unsigned char c = (unsigned char)input.data[at];
        size_t step = 1;
        size_t decoded = 1;
        if (c == '"') break;
        if (c < 0x20) {
            smith_json_fail(reader, at, SmithJsonReason_ControlCharacter);
            return false;
        }
        if (c == '\\') {
            uint32_t code = 0;
            SmithJsonReason reason = SmithJsonReason_None;
            step = smith_json_escape(input, at, &code, &reason);
            if (step == 0) {
                smith_json_fail(reader, at, reason);
                return false;
            }
            char scratch[4];
            decoded = smith_json_encode(code, scratch);
            escaped = true;
        } else if (c >= 0x80) {
            step = smith_json_utf8_length(input.data + at, input.count[0] - at);
            if (step == 0) {
                smith_json_fail(reader, at, SmithJsonReason_BadUtf8);
                return false;
            }
            decoded = step;
        }
        if (decoded > reader->limits.string_length - length) {
            smith_json_fail(reader, at, SmithJsonReason_StringTooLong);
            return false;
        }
        length += decoded;
        at += step;
    }
    if (at >= input.count[0]) {
        smith_json_fail(reader, at, SmithJsonReason_UnexpectedEnd);
        return false;
    }
    reader->text = (struct smith_array_const_char_n){.data = input.data + start, .count = {at - start}};
    reader->length = length;
    reader->escaped = escaped;
    reader->position = at + 1;
    return true;
}

static bool smith_json_is_digit(char c) {
    return c >= '0' && c <= '9';
}

// Digits from at; returns where they end (at, if there are none).
static size_t smith_json_digits(struct smith_array_const_char_n input, size_t at) {
    while (at < input.count[0] && smith_json_is_digit(input.data[at])) {
        at += 1;
    }
    return at;
}

// -? (0 | [1-9][0-9]*) (. [0-9]+)? ([eE] [+-]? [0-9]+)?
static bool smith_json_lex_number(SmithJsonReader *reader) {
    struct smith_array_const_char_n input = reader->input;
    size_t start = reader->position;
    size_t at = start;
    if (input.data[at] == '-') at += 1;
    if (at >= input.count[0] || !smith_json_is_digit(input.data[at])) {
        smith_json_fail(reader, at, SmithJsonReason_BadNumber);
        return false;
    }
    size_t digits = at + 1; // a leading zero is the whole integer part: "01" is the number 0 and then a stray 1
    if (input.data[at] != '0') digits = smith_json_digits(input, at);
    at = digits;
    if (at < input.count[0] && input.data[at] == '.') {
        digits = smith_json_digits(input, at + 1);
        if (digits == at + 1) {
            smith_json_fail(reader, digits, SmithJsonReason_BadNumber);
            return false;
        }
        at = digits;
    }
    if (at < input.count[0] && (input.data[at] == 'e' || input.data[at] == 'E')) {
        size_t sign = at + 1;
        if (sign < input.count[0] && (input.data[sign] == '+' || input.data[sign] == '-')) sign += 1;
        digits = smith_json_digits(input, sign);
        if (digits == sign) {
            smith_json_fail(reader, digits, SmithJsonReason_BadNumber);
            return false;
        }
        at = digits;
    }
    reader->text = (struct smith_array_const_char_n){.data = input.data + start, .count = {at - start}};
    reader->position = at;
    return true;
}

static bool smith_json_literal(SmithJsonReader *reader, const char *word, size_t length) {
    struct smith_array_const_char_n input = reader->input;
    for (size_t i = 0; i < length; i++) {
        size_t at = reader->position + i;
        if (at >= input.count[0]) {
            smith_json_fail(reader, at, SmithJsonReason_UnexpectedEnd);
            return false;
        }
        if (input.data[at] != word[i]) {
            smith_json_fail(reader, at, SmithJsonReason_UnexpectedCharacter);
            return false;
        }
    }
    reader->position += length;
    return true;
}

static SmithJsonToken smith_json_begin_container(SmithJsonReader *reader, bool object) {
    size_t offset = reader->position;
    if (reader->depth >= reader->limits.depth) return smith_json_fail(reader, offset, SmithJsonReason_TooDeep);
    smith_json_set_bit(reader->objects, reader->depth, object);
    reader->depth += 1;
    reader->position += 1;
    reader->state = object ? SmithJsonState_ObjectFirst : SmithJsonState_ArrayFirst;
    return smith_json_emit(reader, object ? SmithJsonToken_ObjectBegin : SmithJsonToken_ArrayBegin, offset);
}

static SmithJsonToken smith_json_end_container(SmithJsonReader *reader, SmithJsonToken token) {
    size_t offset = reader->position;
    reader->position += 1;
    reader->depth -= 1;
    smith_json_after_value(reader);
    return smith_json_emit(reader, token, offset);
}

// A value starts at the position, c being its first byte.
static SmithJsonToken smith_json_read_value(SmithJsonReader *reader, char c) {
    size_t offset = reader->position;
    if (reader->values >= reader->limits.count) return smith_json_fail(reader, offset, SmithJsonReason_TooMany);
    SmithJsonToken token = SmithJsonToken_Null;
    if (c == '{' || c == '[') {
        SmithJsonToken begun = smith_json_begin_container(reader, c == '{');
        if (begun != SmithJsonToken_Error) reader->values += 1;
        return begun;
    } else if (c == '"') {
        if (!smith_json_lex_string(reader)) return SmithJsonToken_Error;
        token = SmithJsonToken_String;
    } else if (c == '-' || smith_json_is_digit(c)) {
        if (!smith_json_lex_number(reader)) return SmithJsonToken_Error;
        token = SmithJsonToken_Number;
    } else if (c == 't') {
        if (!smith_json_literal(reader, "true", 4)) return SmithJsonToken_Error;
        token = SmithJsonToken_True;
    } else if (c == 'f') {
        if (!smith_json_literal(reader, "false", 5)) return SmithJsonToken_Error;
        token = SmithJsonToken_False;
    } else if (c == 'n') {
        if (!smith_json_literal(reader, "null", 4)) return SmithJsonToken_Error;
        token = SmithJsonToken_Null;
    } else {
        return smith_json_fail(reader, offset, SmithJsonReason_UnexpectedCharacter);
    }
    reader->values += 1;
    smith_json_after_value(reader);
    return smith_json_emit(reader, token, offset);
}

// A key starts at the position (c is the quote): the string, then the colon.
static SmithJsonToken smith_json_read_key(SmithJsonReader *reader) {
    size_t offset = reader->position;
    if (!smith_json_lex_string(reader)) return SmithJsonToken_Error;
    smith_json_skip_space(reader);
    if (reader->position >= reader->input.count[0])
        return smith_json_fail(reader, reader->position, SmithJsonReason_UnexpectedEnd);
    if (reader->input.data[reader->position] != ':')
        return smith_json_fail(reader, reader->position, SmithJsonReason_UnexpectedCharacter);
    reader->position += 1;
    reader->state = SmithJsonState_ObjectValue;
    return smith_json_emit(reader, SmithJsonToken_Key, offset);
}

// After a comma, the next thing is due: skips space and reads it.
static SmithJsonToken smith_json_read_after_comma(SmithJsonReader *reader, bool object) {
    reader->position += 1;
    reader->state = object ? SmithJsonState_ObjectKey : SmithJsonState_ArrayValue;
    smith_json_skip_space(reader);
    if (reader->position >= reader->input.count[0])
        return smith_json_fail(reader, reader->position, SmithJsonReason_UnexpectedEnd);
    char c = reader->input.data[reader->position];
    if (!object) return smith_json_read_value(reader, c);
    if (c != '"') return smith_json_fail(reader, reader->position, SmithJsonReason_UnexpectedCharacter);
    return smith_json_read_key(reader);
}

SmithJsonToken smith_json_read(SmithJsonReader *reader) {
    if (reader->token == SmithJsonToken_Error || reader->token == SmithJsonToken_End) return reader->token;
    smith_json_skip_space(reader);
    if (reader->position >= reader->input.count[0]) {
        if (reader->state != SmithJsonState_Done)
            return smith_json_fail(reader, reader->position, SmithJsonReason_UnexpectedEnd);
        return smith_json_emit(reader, SmithJsonToken_End, reader->position);
    }
    char c = reader->input.data[reader->position];
    switch (reader->state) {
    case SmithJsonState_Done:
        return smith_json_fail(reader, reader->position, SmithJsonReason_TrailingContent);
    case SmithJsonState_ArrayFirst:
        if (c == ']') return smith_json_end_container(reader, SmithJsonToken_ArrayEnd);
        return smith_json_read_value(reader, c);
    case SmithJsonState_ArrayNext:
        if (c == ',') return smith_json_read_after_comma(reader, false);
        if (c == ']') return smith_json_end_container(reader, SmithJsonToken_ArrayEnd);
        break;
    case SmithJsonState_ObjectFirst:
        if (c == '}') return smith_json_end_container(reader, SmithJsonToken_ObjectEnd);
        if (c == '"') return smith_json_read_key(reader);
        break;
    case SmithJsonState_ObjectNext:
        if (c == ',') return smith_json_read_after_comma(reader, true);
        if (c == '}') return smith_json_end_container(reader, SmithJsonToken_ObjectEnd);
        break;
    case SmithJsonState_ObjectKey:
        if (c == '"') return smith_json_read_key(reader);
        break;
    default:
        return smith_json_read_value(reader, c); // Top, ArrayValue, ObjectValue
    }
    return smith_json_fail(reader, reader->position, SmithJsonReason_UnexpectedCharacter);
}

bool smith_json_skip(SmithJsonReader *reader) {
    if (reader->token == SmithJsonToken_Key && smith_json_read(reader) == SmithJsonToken_Error) return false;
    if (reader->token == SmithJsonToken_Error) return false;
    if (reader->token != SmithJsonToken_ObjectBegin && reader->token != SmithJsonToken_ArrayBegin) return true;
    size_t outside = reader->depth - 1;
    for (size_t step = 0; step <= reader->input.count[0]; step++) { // each token uses at least one byte
        SmithJsonToken token = smith_json_read(reader);
        if (token == SmithJsonToken_Error) return false;
        if (reader->depth == outside) return true;
        smith_json_check(token != SmithJsonToken_End);
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
// Decoding strings

// The next decoded character of raw, which is a validated string: writes its bytes to out and returns how many.
static size_t smith_json_decode_step(struct smith_array_const_char_n raw, size_t *at, char *out) {
    if (raw.data[*at] != '\\') {
        out[0] = raw.data[*at];
        *at += 1;
        return 1;
    }
    uint32_t code = 0;
    SmithJsonReason reason = SmithJsonReason_None;
    size_t step = smith_json_escape(raw, *at, &code, &reason);
    smith_json_check(step != 0);
    *at += step;
    return smith_json_encode(code, out);
}

static struct smith_array_const_char_n smith_json_decode_into(const SmithJsonReader *reader, char *destination) {
    size_t at = 0;
    size_t used = 0;
    while (at < reader->text.count[0]) {
        used += smith_json_decode_step(reader->text, &at, destination + used);
    }
    smith_json_check(used == reader->length);
    destination[used] = '\0';
    return (struct smith_array_const_char_n){.data = destination, .count = {used}};
}

struct smith_array_const_char_n smith_json_decode(const SmithJsonReader *reader, SmithArena *arena) {
    smith_trusted(); // its loops run over the input, which has no bound a proof can use
    smith_requires(reader->token == SmithJsonToken_String || reader->token == SmithJsonToken_Key);
    if (!reader->escaped) return reader->text;
    char *destination = smith_arena_push(arena, reader->length + 1, 1);
    return smith_json_decode_into(reader, destination);
}

bool smith_json_is(const SmithJsonReader *reader, const char *string) {
    smith_trusted(); // its loops run over the input, which has no bound a proof can use
    smith_requires(reader->token == SmithJsonToken_String || reader->token == SmithJsonToken_Key);
    size_t at = 0;
    size_t matched = 0;
    while (at < reader->text.count[0]) {
        char bytes[4];
        size_t count = smith_json_decode_step(reader->text, &at, bytes);
        for (size_t i = 0; i < count; i++) {
            if (matched >= reader->length || string[matched] != bytes[i]) return false;
            matched += 1;
        }
    }
    return string[matched] == '\0';
}

// ---------------------------------------------------------------------------------------------------------------
// The tree

// What the first pass found: how many values, and how many bytes the decoded strings with escapes need.
typedef struct {
    size_t values;
    size_t bytes;
} SmithJsonSize;

static bool smith_json_measure(struct smith_array_const_char_n input, SmithJsonLimits limits, SmithJsonSize *size,
                               SmithJsonError *error) {
    SmithJsonReader reader = smith_json_reader_make(input, limits);
    size_t bytes = 0;
    for (size_t reads = 0; reads <= input.count[0]; reads++) { // every token but the last takes a byte of the input
        SmithJsonToken token = smith_json_read(&reader);
        if (token == SmithJsonToken_Error) {
            *error = reader.error;
            return false;
        }
        if (token == SmithJsonToken_End) break;
        if ((token == SmithJsonToken_String || token == SmithJsonToken_Key) && reader.escaped)
            bytes += reader.length + 1;
    }
    *size = (SmithJsonSize){reader.values, bytes};
    return true;
}

// The tree being made in the second pass.
typedef struct {
    SmithJsonValue *nodes;
    size_t next;                 // the next node to fill
    const SmithJsonValue **pool; // the arrays of children, taken from the front
    size_t pool_used;
    char *bytes; // room for the decoded strings
    size_t bytes_used;
    size_t open_node[smith_json_depth_max]; // the container at each depth, as an index in nodes
    size_t open_count[smith_json_depth_max];
    struct smith_array_const_char_n key; // the key read for the next value
} SmithJsonBuild;

static struct smith_array_const_char_n smith_json_build_text(SmithJsonBuild *build, const SmithJsonReader *reader) {
    if (!reader->escaped) return reader->text;
    struct smith_array_const_char_n text = smith_json_decode_into(reader, build->bytes + build->bytes_used);
    build->bytes_used += reader->length + 1;
    return text;
}

// A new node for the token just read; `depth` is how many containers are open around it.
static SmithJsonValue *smith_json_build_node(SmithJsonBuild *build, const SmithJsonReader *reader, SmithJsonKind kind,
                                             size_t depth) {
    SmithJsonValue *node = &build->nodes[build->next];
    build->next += 1;
    node->kind = kind;
    node->offset = reader->offset;
    node->key = build->key;
    node->span = 1;
    build->key = (struct smith_array_const_char_n){};
    if (depth > 0) build->open_count[depth - 1] += 1;
    return node;
}

// A container closes: its children are the nodes that follow it, each followed by its own subtree.
static void smith_json_build_close(SmithJsonBuild *build, size_t depth) {
    size_t index = build->open_node[depth];
    SmithJsonValue *container = &build->nodes[index];
    size_t count = build->open_count[depth];
    const SmithJsonValue **items = build->pool + build->pool_used;
    build->pool_used += count;
    container->count = count;
    container->items = items;
    container->span = build->next - index;
    size_t child = index + 1;
    for (size_t i = 0; i < count; i++) {
        items[i] = &build->nodes[child];
        child += build->nodes[child].span;
    }
    smith_json_check(child == build->next);
}

static void smith_json_build_token(SmithJsonBuild *build, const SmithJsonReader *reader) {
    size_t depth = reader->depth; // after the token
    switch (reader->token) {
    case SmithJsonToken_Key:
        build->key = smith_json_build_text(build, reader);
        break;
    case SmithJsonToken_ObjectBegin:
    case SmithJsonToken_ArrayBegin: {
        bool object = reader->token == SmithJsonToken_ObjectBegin;
        SmithJsonValue *node =
            smith_json_build_node(build, reader, object ? SmithJsonKind_Object : SmithJsonKind_Array, depth - 1);
        build->open_node[depth - 1] = (size_t)(node - build->nodes);
        build->open_count[depth - 1] = 0;
        break;
    }
    case SmithJsonToken_ObjectEnd:
    case SmithJsonToken_ArrayEnd:
        smith_json_build_close(build, depth);
        break;
    case SmithJsonToken_String: {
        SmithJsonValue *node = smith_json_build_node(build, reader, SmithJsonKind_String, depth);
        node->text = smith_json_build_text(build, reader);
        break;
    }
    case SmithJsonToken_Number:
        smith_json_build_node(build, reader, SmithJsonKind_Number, depth)->text = reader->text;
        break;
    case SmithJsonToken_True:
        (void)smith_json_build_node(build, reader, SmithJsonKind_True, depth);
        break;
    case SmithJsonToken_False:
        (void)smith_json_build_node(build, reader, SmithJsonKind_False, depth);
        break;
    case SmithJsonToken_Null:
        (void)smith_json_build_node(build, reader, SmithJsonKind_Null, depth);
        break;
    case SmithJsonToken_Error:
    case SmithJsonToken_End:
        smith_json_check(false); // the first pass read the same bytes, so neither is a value
        break;
    }
}

// Pushes the one block the tree needs (the nodes, the arrays of children, the decoded strings). An arena that cannot
// hold it stops the program with the arena's own report, as any push does.
static void smith_json_build_make(SmithJsonBuild *build, SmithArena *arena, SmithJsonSize size) {
    size_t pool_count = size.values > 0 ? size.values - 1 : 0;
    size_t node_bytes = size.values * sizeof(SmithJsonValue);
    size_t pool_bytes = pool_count * sizeof(SmithJsonValue *);
    if (size.values > SIZE_MAX / sizeof(SmithJsonValue) || pool_count > SIZE_MAX / sizeof(SmithJsonValue *) ||
        node_bytes > SIZE_MAX - pool_bytes || node_bytes + pool_bytes > SIZE_MAX - size.bytes)
        smith_failure_format(SmithFailure_Capacity, "json tree of %zu values and %zu bytes of text", size.values,
                             size.bytes);
    static_assert(sizeof(SmithJsonValue) % alignof(SmithJsonValue) == 0 &&
                  sizeof(SmithJsonValue *) % alignof(SmithJsonValue) == 0);
    size_t total = node_bytes + pool_bytes + size.bytes;
    unsigned char *block = smith_arena_push(arena, total, alignof(SmithJsonValue));
    build->nodes = (SmithJsonValue *)(void *)block;
    build->pool = (const SmithJsonValue **)(void *)(block + node_bytes);
    build->bytes = (char *)(block + node_bytes + pool_bytes);
}

bool smith_json_parse(SmithArena *arena, struct smith_array_const_char_n input, SmithJsonLimits limits,
                      const SmithJsonValue **root, SmithJsonError *error) {
    SmithJsonSize size = {};
    if (!smith_json_measure(input, limits, &size, error)) return false;
    SmithJsonBuild build = {};
    smith_json_build_make(&build, arena, size);
    SmithJsonReader reader = smith_json_reader_make(input, limits);
    for (SmithJsonToken token = smith_json_read(&reader); token != SmithJsonToken_End;
         token = smith_json_read(&reader)) {
        smith_json_check(token != SmithJsonToken_Error); // the first pass read the same bytes
        smith_json_build_token(&build, &reader);
    }
    smith_json_check(build.next == size.values && build.bytes_used == size.bytes);
    *root = build.nodes;
    return true;
}

const SmithJsonValue *smith_json_member(const SmithJsonValue *object, const char *key) {
    if (object->kind != SmithJsonKind_Object) return nullptr;
    for (size_t i = object->count; i-- > 0;) {
        if (smith_text_is(object->items[i]->key, key)) return object->items[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// The writer

enum {
    SmithJsonWriterState_Start, // a value is due, the first of the document
    SmithJsonWriterState_Done,  // the document is complete
    SmithJsonWriterState_Item,  // in an array: a value or the end
    SmithJsonWriterState_Key,   // in an object: a key or the end
    SmithJsonWriterState_Value, // in an object: the value of the key just written
};

SmithJsonWriter smith_json_writer_make(char *buffer, size_t capacity, size_t indent) {
    smith_requires(buffer != nullptr || capacity == 0);
    return (SmithJsonWriter){.buffer = buffer, .capacity = capacity, .indent = indent};
}

static bool smith_json_writer_fail(SmithJsonWriter *writer, SmithJsonReason reason) {
    if (writer->error.reason == SmithJsonReason_None) writer->error = (SmithJsonError){writer->length, reason};
    return false;
}

static bool smith_json_put(SmithJsonWriter *writer, const char *text, size_t length) {
    if (writer->error.reason != SmithJsonReason_None) return false;
    if (length > writer->capacity - writer->length) return smith_json_writer_fail(writer, SmithJsonReason_Full);
    for (size_t i = 0; i < length; i++) {
        writer->buffer[writer->length + i] = text[i];
    }
    writer->length += length;
    return true;
}

static void smith_json_put_char(SmithJsonWriter *writer, char c) {
    (void)smith_json_put(writer, &c, 1);
}

// A newline and the indent of `levels` levels, when there is an indent.
static void smith_json_put_break(SmithJsonWriter *writer, size_t levels) {
    if (writer->indent == 0) return;
    smith_json_put_char(writer, '\n');
    for (size_t i = 0; i < levels * writer->indent; i++) {
        smith_json_put_char(writer, ' ');
    }
}

// What comes before an element or a key of the open container: a comma if it has one already, and the break.
static void smith_json_put_separator(SmithJsonWriter *writer) {
    size_t level = writer->depth - 1;
    if (smith_json_bit(writer->filled, level)) smith_json_put_char(writer, ',');
    smith_json_set_bit(writer->filled, level, true);
    smith_json_put_break(writer, writer->depth);
}

// Checks that a value may come here and writes what goes before it.
static bool smith_json_begin_value(SmithJsonWriter *writer) {
    if (writer->error.reason != SmithJsonReason_None) return false;
    if (writer->depth == 0) {
        if (writer->state != SmithJsonWriterState_Start)
            smith_json_misuse("json writer: a second document, or a value where the document is complete");
        return true;
    }
    if (writer->state == SmithJsonWriterState_Item) {
        smith_json_put_separator(writer);
        return true;
    }
    if (writer->state == SmithJsonWriterState_Value) return true;
    smith_json_misuse("json writer: a value where a key, or the end of the object, is due");
}

// A value is complete.
static void smith_json_end_value(SmithJsonWriter *writer) {
    if (writer->depth == 0)
        writer->state = SmithJsonWriterState_Done;
    else
        writer->state =
            smith_json_bit(writer->objects, writer->depth - 1) ? SmithJsonWriterState_Key : SmithJsonWriterState_Item;
}

static void smith_json_write_begin(SmithJsonWriter *writer, bool object) {
    if (!smith_json_begin_value(writer)) return;
    if (writer->depth >= smith_json_depth_max) {
        (void)smith_json_writer_fail(writer, SmithJsonReason_TooDeep);
        return;
    }
    smith_json_put_char(writer, object ? '{' : '[');
    smith_json_set_bit(writer->objects, writer->depth, object);
    smith_json_set_bit(writer->filled, writer->depth, false);
    writer->depth += 1;
    writer->state = object ? SmithJsonWriterState_Key : SmithJsonWriterState_Item;
}

static void smith_json_write_end(SmithJsonWriter *writer, bool object) {
    if (writer->error.reason != SmithJsonReason_None) return;
    bool open = writer->depth > 0 && smith_json_bit(writer->objects, writer->depth - 1) == object;
    bool ready = writer->state == (object ? SmithJsonWriterState_Key : SmithJsonWriterState_Item);
    if (!open || !ready) {
        smith_json_misuse("json writer: an end that closes nothing, the wrong container, or a key without its value");
    }
    writer->depth -= 1;
    if (smith_json_bit(writer->filled, writer->depth)) smith_json_put_break(writer, writer->depth);
    smith_json_put_char(writer, object ? '}' : ']');
    smith_json_end_value(writer);
}

void smith_json_write_object_begin(SmithJsonWriter *writer) {
    smith_json_write_begin(writer, true);
}

void smith_json_write_object_end(SmithJsonWriter *writer) {
    smith_json_write_end(writer, true);
}

void smith_json_write_array_begin(SmithJsonWriter *writer) {
    smith_json_write_begin(writer, false);
}

void smith_json_write_array_end(SmithJsonWriter *writer) {
    smith_json_write_end(writer, false);
}

// Whether the text is well formed UTF-8 (the writer will not make invalid JSON out of anything else).
static bool smith_json_valid_utf8(struct smith_array_const_char_n text) {
    size_t at = 0;
    while (at < text.count[0]) {
        size_t step = smith_json_utf8_length(text.data + at, text.count[0] - at);
        if (step == 0) return false;
        at += step;
    }
    return true;
}

static void smith_json_put_escaped(SmithJsonWriter *writer, char c) {
    static const char hex[] = "0123456789abcdef";
    const char *short_escape = nullptr;
    switch (c) {
    case '"':
        short_escape = "\\\"";
        break;
    case '\\':
        short_escape = "\\\\";
        break;
    case '\b':
        short_escape = "\\b";
        break;
    case '\f':
        short_escape = "\\f";
        break;
    case '\n':
        short_escape = "\\n";
        break;
    case '\r':
        short_escape = "\\r";
        break;
    case '\t':
        short_escape = "\\t";
        break;
    default:
        break;
    }
    if (short_escape != nullptr) {
        (void)smith_json_put(writer, short_escape, 2);
        return;
    }
    uint32_t byte = (unsigned char)c;
    char escape[6] = {'\\', 'u', '0', '0', hex[(byte >> 4) & 0xfU], hex[byte & 0xfU]};
    (void)smith_json_put(writer, escape, sizeof escape);
}

// A quoted string. The text is written in runs that need no escape, each run in one copy.
static void smith_json_put_string(SmithJsonWriter *writer, struct smith_array_const_char_n text) {
    if (writer->error.reason != SmithJsonReason_None) return;
    if (!smith_json_valid_utf8(text)) {
        (void)smith_json_writer_fail(writer, SmithJsonReason_BadUtf8);
        return;
    }
    smith_json_put_char(writer, '"');
    size_t run = 0;
    for (size_t at = 0; at < text.count[0]; at++) {
        char c = text.data[at];
        if (c != '"' && c != '\\' && (unsigned char)c >= 0x20) continue;
        (void)smith_json_put(writer, text.data + run, at - run);
        smith_json_put_escaped(writer, c);
        run = at + 1;
    }
    (void)smith_json_put(writer, text.data + run, text.count[0] - run);
    smith_json_put_char(writer, '"');
}

void smith_json_write_key(SmithJsonWriter *writer, struct smith_array_const_char_n key) {
    if (writer->error.reason != SmithJsonReason_None) return;
    if (writer->depth == 0 || writer->state != SmithJsonWriterState_Key) {
        smith_json_misuse("json writer: a key outside an object, or two keys in a row");
    }
    smith_json_put_separator(writer);
    smith_json_put_string(writer, key);
    smith_json_put_char(writer, ':');
    if (writer->indent > 0) smith_json_put_char(writer, ' ');
    writer->state = SmithJsonWriterState_Value;
}

void smith_json_write_string(SmithJsonWriter *writer, struct smith_array_const_char_n string) {
    if (!smith_json_begin_value(writer)) return;
    smith_json_put_string(writer, string);
    smith_json_end_value(writer);
}

static void smith_json_write_number(SmithJsonWriter *writer, const char *text, size_t length) {
    if (!smith_json_begin_value(writer)) return;
    (void)smith_json_put(writer, text, length);
    smith_json_end_value(writer);
}

void smith_json_write_u64(SmithJsonWriter *writer, uint64_t value) {
    char text[smith_text_number_capacity];
    smith_json_write_number(
        writer, text, smith_text_write_u64((struct smith_array_char_n){.data = text, .count = {sizeof text}}, value));
}

void smith_json_write_i64(SmithJsonWriter *writer, int64_t value) {
    char text[smith_text_number_capacity];
    smith_json_write_number(
        writer, text, smith_text_write_i64((struct smith_array_char_n){.data = text, .count = {sizeof text}}, value));
}

void smith_json_write_f64(SmithJsonWriter *writer, double value) {
    uint64_t bits = __builtin_bit_cast(uint64_t, value);
    if (((bits >> 52) & 0x7ff) == 0x7ff) {
        smith_json_misuse("json writer: infinity and nan have no JSON text");
    }
    char text[smith_text_f64_capacity];
    smith_json_write_number(
        writer, text, smith_text_write_f64((struct smith_array_char_n){.data = text, .count = {sizeof text}}, value));
}

void smith_json_write_bool(SmithJsonWriter *writer, bool value) {
    if (!smith_json_begin_value(writer)) return;
    (void)(value ? smith_json_put(writer, "true", 4) : smith_json_put(writer, "false", 5));
    smith_json_end_value(writer);
}

void smith_json_write_null(SmithJsonWriter *writer) {
    if (!smith_json_begin_value(writer)) return;
    (void)smith_json_put(writer, "null", 4);
    smith_json_end_value(writer);
}

typedef struct {
    const SmithJsonValue *container;
    size_t next; // the member or element to write next
} SmithJsonValueFrame;

// Writes a scalar, or the start of a container (pushing it), as the value `current`.
static size_t smith_json_write_node(SmithJsonWriter *writer, const SmithJsonValue *current, SmithJsonValueFrame *stack,
                                    size_t depth) {
    smith_trusted(); // the writes loop over the writer's indent, which no requires here bounds
    switch (current->kind) {
    case SmithJsonKind_Null:
        smith_json_write_null(writer);
        return depth;
    case SmithJsonKind_False:
    case SmithJsonKind_True:
        smith_json_write_bool(writer, current->kind == SmithJsonKind_True);
        return depth;
    case SmithJsonKind_Number:
        smith_json_write_number(writer, current->text.data, current->text.count[0]);
        return depth;
    case SmithJsonKind_String:
        smith_json_write_string(writer, current->text);
        return depth;
    case SmithJsonKind_Array:
    case SmithJsonKind_Object:
        break;
    }
    smith_requires(depth < smith_json_depth_max);
    if (current->kind == SmithJsonKind_Array)
        smith_json_write_array_begin(writer);
    else
        smith_json_write_object_begin(writer);
    stack[depth] = (SmithJsonValueFrame){current, 0};
    return depth + 1;
}

void smith_json_write_value(SmithJsonWriter *writer, const SmithJsonValue *value) {
    SmithJsonValueFrame stack[smith_json_depth_max];
    size_t depth = 0;
    const SmithJsonValue *current = value;
    // Each value is visited once as `current`, once as a child taken from its container, and a container is ended
    // once: at most three steps for each value the tree holds, which value->span counts.
    for (size_t step = 0; step < 3 * value->span + 3; step++) {
        if (current != nullptr) {
            depth = smith_json_write_node(writer, current, stack, depth);
            current = nullptr;
            if (depth == 0) return;
            continue;
        }
        SmithJsonValueFrame *top = &stack[depth - 1];
        bool object = top->container->kind == SmithJsonKind_Object;
        if (top->next == top->container->count) {
            if (object)
                smith_json_write_object_end(writer);
            else
                smith_json_write_array_end(writer);
            depth--;
            if (depth == 0) return;
            continue;
        }
        current = top->container->items[top->next++];
        if (object) smith_json_write_key(writer, current->key);
    }
    smith_json_check(false); // span does not count the tree
}

bool smith_json_writer_finish(SmithJsonWriter *writer, struct smith_array_const_char_n *output) {
    if (writer->error.reason == SmithJsonReason_None && writer->state != SmithJsonWriterState_Done)
        smith_json_misuse("json writer: finished before the document is complete");
    if (writer->error.reason != SmithJsonReason_None) return false;
    *output = (struct smith_array_const_char_n){.data = writer->buffer, .count = {writer->length}};
    return true;
}
