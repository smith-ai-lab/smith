// The hand-written half of the generated protocol code; protocol.h says what is here.
#include "protocol.h"

#include <string.h>
#if __has_include(<smith/contract.h>)
#include <smith/contract.h>
#else
#include "../../include/smith/contract.h"
#endif

const char *smith_protocol_reason_text(SmithProtocolReason reason) {
    switch (reason) {
    case SmithProtocolReason_None:
        return "no error";
    case SmithProtocolReason_Missing:
        return "a required member is missing";
    case SmithProtocolReason_WrongType:
        return "the value is not of the kind the schema says";
    case SmithProtocolReason_BadValue:
        return "the value is not one the schema allows";
    case SmithProtocolReason_NoMatch:
        return "no alternative of the union takes the value";
    case SmithProtocolReason_OutOfMemory:
        return "the arena cannot hold the value";
    }
    return "unknown reason";
}

bool smith_protocol_fail(SmithProtocolError *error, SmithProtocolReason reason, const SmithJsonValue *at) {
    smith_requires(reason != SmithProtocolReason_None);
    *error = (SmithProtocolError){.reason = reason, .offset = at != nullptr ? at->offset : 0};
    return false;
}

bool smith_protocol_in(SmithProtocolError *error, const char *type, const char *field) {
    if (error->type == nullptr) {
        error->type = type;
        error->field = field;
    }
    return false;
}

bool smith_protocol_missing(SmithProtocolError *error, const SmithJsonValue *object, const char *type,
                            const char *field) {
    (void)smith_protocol_fail(error, SmithProtocolReason_Missing, object);
    return smith_protocol_in(error, type, field);
}

bool smith_protocol_kind_in(const SmithJsonValue *value, unsigned mask) {
    switch (value->kind) {
    case SmithJsonKind_Null:
        return (mask & smith_protocol_kind_null) != 0;
    case SmithJsonKind_False:
    case SmithJsonKind_True:
        return (mask & smith_protocol_kind_bool) != 0;
    case SmithJsonKind_Number:
        return (mask & smith_protocol_kind_number) != 0;
    case SmithJsonKind_String:
        return (mask & smith_protocol_kind_string) != 0;
    case SmithJsonKind_Array:
        return (mask & smith_protocol_kind_array) != 0;
    case SmithJsonKind_Object:
        return (mask & smith_protocol_kind_object) != 0;
    }
    return false;
}

bool smith_protocol_present(const SmithJsonValue *member) {
    return member != nullptr && member->kind != SmithJsonKind_Null;
}

bool smith_protocol_text_is(struct smith_array_const_char_n text, const char *string) {
    return smith_format_is(text, string);
}

// ---------------------------------------------------------------------------------------------------------------
// The random makers' choices

// The next key of a stream: `stream` moves on and the key returned is for one use (the reassignment pattern).
SmithKey smith_protocol_take(SmithKey *stream) {
    SmithKey next[2];
    smith_key_split_n(*stream, next, 2);
    *stream = next[0];
    return next[1];
}

bool smith_protocol_coin(SmithKey key) {
    return (smith_key_bits(key) & 1) != 0;
}

size_t smith_protocol_pick(SmithKey key, size_t count) {
    smith_requires(count > 0 && count <= INT64_MAX); // the draw is an int64_t
    return (size_t)smith_uniform_int64_sample(key, (SmithUniformInt64){.low = 0, .high = (int64_t)count - 1});
}

size_t smith_protocol_count(SmithKey key) {
    return smith_protocol_pick(key, 3);
}

void *smith_protocol_block(SmithArena *arena, size_t size, size_t align, size_t count) {
    if (count == 0) return nullptr;
    return smith_arena_push_array(arena, size, count, align);
}

// ---------------------------------------------------------------------------------------------------------------
// Strings

bool smith_protocol_string_parse([[maybe_unused]] SmithArena *arena, const SmithJsonValue *value,
                                 struct smith_array_const_char_n *out, SmithProtocolError *error) {
    if (value->kind != SmithJsonKind_String) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    *out = value->text;
    return true;
}

void smith_protocol_string_write(SmithJsonWriter *writer, const struct smith_array_const_char_n *value) {
    smith_json_write_string(writer, *value);
}

bool smith_protocol_string_equal(const struct smith_array_const_char_n *a, const struct smith_array_const_char_n *b) {
    return a->count[0] == b->count[0] && (a->count[0] == 0 || memcmp(a->data, b->data, a->count[0]) == 0);
}

// Pieces of UTF-8 a random string is made of: plain letters, the characters the writer escapes, and two, three and
// four byte sequences.
static const char *const smith_protocol_pieces[] = {"a",  "Z",  "0", " ",        "\"",           "\\",
                                                    "\n", "\t", "/", "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80"};
constexpr size_t smith_protocol_piece_count = sizeof smith_protocol_pieces / sizeof smith_protocol_pieces[0];
constexpr size_t smith_protocol_string_pieces_max = 5;

void smith_protocol_string_random(SmithArena *arena, SmithKey key, struct smith_array_const_char_n *out) {
    SmithKey *stream = &key;
    size_t pieces = smith_protocol_pick(smith_protocol_take(stream), smith_protocol_string_pieces_max);
    char *bytes = smith_arena_push_array(arena, 1, pieces * 4 + 1, 1);
    size_t length = 0;
    for (size_t i = 0; i < pieces; i++) { // bounded by smith_protocol_string_pieces_max
        const char *piece =
            smith_protocol_pieces[smith_protocol_pick(smith_protocol_take(stream), smith_protocol_piece_count)];
        size_t piece_length = strlen(piece);
        memcpy(bytes + length, piece, piece_length + 1); // with its terminator: the buffer has room for the last
        length += piece_length;
    }
    *out = (struct smith_array_const_char_n){.data = bytes, .count = {length}};
}

// ---------------------------------------------------------------------------------------------------------------
// Numbers and booleans

bool smith_protocol_integer_parse([[maybe_unused]] SmithArena *arena, const SmithJsonValue *value, int64_t *out,
                                  SmithProtocolError *error) {
    if (value->kind != SmithJsonKind_Number) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    smith_format_I64Result parsed = smith_format_parse_i64(value->text);
    if (parsed.tag != smith_enum_result_i64__SmithFormatError_tag_ok) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    *out = parsed.ok;
    return true;
}

void smith_protocol_integer_write(SmithJsonWriter *writer, const int64_t *value) {
    smith_json_write_i64(writer, *value);
}

bool smith_protocol_integer_equal(const int64_t *a, const int64_t *b) {
    return *a == *b;
}

void smith_protocol_integer_random([[maybe_unused]] SmithArena *arena, SmithKey key, int64_t *out) {
    SmithKey *stream = &key;
    if (smith_protocol_coin(smith_protocol_take(stream)))
        *out = smith_uniform_int64_sample(smith_protocol_take(stream), (SmithUniformInt64){.low = -5, .high = 1000});
    else
        *out = (int64_t)smith_key_bits(smith_protocol_take(stream));
}

bool smith_protocol_number_parse([[maybe_unused]] SmithArena *arena, const SmithJsonValue *value, double *out,
                                 SmithProtocolError *error) {
    if (value->kind != SmithJsonKind_Number) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    smith_format_F64Result parsed = smith_format_parse_f64(value->text);
    if (parsed.tag != smith_enum_result_f64__SmithFormatError_tag_ok) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    *out = parsed.ok;
    return true;
}

void smith_protocol_number_write(SmithJsonWriter *writer, const double *value) {
    smith_json_write_f64(writer, *value);
}

bool smith_protocol_number_equal(const double *a, const double *b) {
    return *a == *b;
}

void smith_protocol_number_random([[maybe_unused]] SmithArena *arena, SmithKey key, double *out) {
    SmithKey *stream = &key;
    double d = __builtin_bit_cast(double, smith_key_bits(smith_protocol_take(stream)));
    bool finite = ((__builtin_bit_cast(uint64_t, d) >> 52) & 0x7ff) != 0x7ff;
    if (smith_protocol_coin(smith_protocol_take(stream)) || !finite)
        d = smith_uniform_double_sample(smith_protocol_take(stream), (SmithUniformDouble){.low = -100, .high = 100});
    *out = d;
}

bool smith_protocol_boolean_parse([[maybe_unused]] SmithArena *arena, const SmithJsonValue *value, bool *out,
                                  SmithProtocolError *error) {
    if (value->kind != SmithJsonKind_True && value->kind != SmithJsonKind_False)
        return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    *out = value->kind == SmithJsonKind_True;
    return true;
}

void smith_protocol_boolean_write(SmithJsonWriter *writer, const bool *value) {
    smith_json_write_bool(writer, *value);
}

bool smith_protocol_boolean_equal(const bool *a, const bool *b) {
    return *a == *b;
}

void smith_protocol_boolean_random([[maybe_unused]] SmithArena *arena, SmithKey key, bool *out) {
    *out = smith_protocol_coin(key);
}

// ---------------------------------------------------------------------------------------------------------------
// Any JSON, kept as the tree it was read as

static bool smith_protocol_container(const SmithJsonValue *value) {
    return value->kind == SmithJsonKind_Array || value->kind == SmithJsonKind_Object;
}

// Whether two values are alike without looking inside: the kind, the text of a string or a number, the size of a
// container.
static bool smith_protocol_shallow_equal(const SmithJsonValue *a, const SmithJsonValue *b) {
    if (a->kind != b->kind) return false;
    if (smith_protocol_container(a)) return a->count == b->count;
    return smith_protocol_string_equal(&a->text, &b->text);
}

typedef struct {
    const SmithJsonValue *a;
    const SmithJsonValue *b;
    size_t next;
} SmithProtocolPair;

static bool smith_protocol_tree_equal(const SmithJsonValue *a, const SmithJsonValue *b) {
    if (!smith_protocol_shallow_equal(a, b)) return false;
    if (!smith_protocol_container(a)) return true;
    SmithProtocolPair stack[smith_json_depth_max];
    size_t depth = 0;
    stack[depth++] = (SmithProtocolPair){a, b, 0};
    // One step takes one element or ends one container, so a tree of span values takes at most 2 * span of them.
    for (size_t step = 0; step < 2 * a->span + 2; step++) {
        SmithProtocolPair *top = &stack[depth - 1];
        if (top->next == top->a->count) {
            depth--;
            if (depth == 0) return true;
            continue;
        }
        const SmithJsonValue *child_a = top->a->items[top->next];
        const SmithJsonValue *child_b = top->b->items[top->next];
        top->next++;
        if (!smith_protocol_string_equal(&child_a->key, &child_b->key)) return false;
        if (!smith_protocol_shallow_equal(child_a, child_b)) return false;
        if (!smith_protocol_container(child_a) || child_a->count == 0) continue;
        if (depth >= smith_json_depth_max) __builtin_trap(); // the tree is no deeper than its limit
        stack[depth++] = (SmithProtocolPair){child_a, child_b, 0};
    }
    return false;
}

bool smith_protocol_any_parse([[maybe_unused]] SmithArena *arena, const SmithJsonValue *value, SmithProtocolAny *out,
                              [[maybe_unused]] SmithProtocolError *error) {
    *out = value;
    return true;
}

void smith_protocol_any_write(SmithJsonWriter *writer, const SmithProtocolAny *value) {
    if (*value == nullptr)
        smith_json_write_null(writer);
    else
        smith_json_write_value(writer, *value);
}

bool smith_protocol_any_equal(const SmithProtocolAny *a, const SmithProtocolAny *b) {
    if (*a == nullptr || *b == nullptr) return *a == *b;
    return smith_protocol_tree_equal(*a, *b);
}

bool smith_protocol_object_parse([[maybe_unused]] SmithArena *arena, const SmithJsonValue *value, SmithProtocolAny *out,
                                 SmithProtocolError *error) {
    if (value->kind != SmithJsonKind_Object) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    *out = value;
    return true;
}

void smith_protocol_object_write(SmithJsonWriter *writer, const SmithProtocolAny *value) {
    smith_protocol_any_write(writer, value);
}

bool smith_protocol_object_equal(const SmithProtocolAny *a, const SmithProtocolAny *b) {
    return smith_protocol_any_equal(a, b);
}

constexpr size_t smith_protocol_random_depth = 3;   // containers inside each other
constexpr size_t smith_protocol_random_values = 24; // values in one random document

typedef struct {
    size_t remaining;
    bool object;
} SmithProtocolOpen;

// One random scalar, or the start of a container (pushed on the stack of open ones).
static void smith_protocol_random_item(SmithArena *arena, SmithJsonWriter *writer, SmithKey key, bool force_object,
                                       SmithProtocolOpen *open, size_t *depth) {
    SmithKey *stream = &key;
    size_t kind = force_object ? 7 : smith_protocol_pick(smith_protocol_take(stream), 8);
    if (kind >= 6 && *depth >= smith_protocol_random_depth) kind = 0;
    SmithKey rest = smith_protocol_take(stream);
    if (kind == 6 || kind == 7) {
        if (kind == 6)
            smith_json_write_array_begin(writer);
        else
            smith_json_write_object_begin(writer);
        open[*depth] = (SmithProtocolOpen){.remaining = smith_protocol_count(rest), .object = kind == 7};
        (*depth)++;
        return;
    }
    switch (kind) {
    case 0:
        smith_json_write_null(writer);
        break;
    case 1:
    case 2:
        smith_json_write_bool(writer, kind == 2);
        break;
    case 3: {
        int64_t integer;
        smith_protocol_integer_random(nullptr, rest, &integer);
        smith_json_write_i64(writer, integer);
        break;
    }
    case 4: {
        double number;
        smith_protocol_number_random(nullptr, rest, &number);
        smith_json_write_f64(writer, number);
        break;
    }
    default: {
        struct smith_array_const_char_n text;
        smith_protocol_string_random(arena, rest, &text);
        smith_json_write_string(writer, text);
        break;
    }
    }
}

// A random JSON document, written then read back, so the tree is exactly what a parse of its text gives.
static const SmithJsonValue *smith_protocol_random_tree(SmithArena *arena, SmithKey key, bool force_object) {
    constexpr size_t capacity = 1 << 13;
    char *buffer = smith_arena_push_array(arena, 1, capacity, 1);
    SmithJsonWriter writer = smith_json_writer_make(buffer, capacity, 0);
    SmithProtocolOpen open[smith_protocol_random_depth];
    size_t depth = 0;
    size_t values = 0;
    bool due = true; // a value is due, rather than the next element or an end
    SmithKey *stream = &key;
    for (size_t step = 0; step < 4 * smith_protocol_random_values; step++) { // each step makes a value or an end
        if (due) {
            SmithKey item = smith_protocol_take(stream);
            bool force = force_object && values == 0;
            if (values < smith_protocol_random_values || force) {
                smith_protocol_random_item(arena, &writer, item, force, open, &depth);
            } else {
                smith_json_write_null(&writer);
            }
            values++;
            due = false;
            continue;
        }
        if (depth == 0) break;
        SmithProtocolOpen *top = &open[depth - 1];
        if (top->remaining == 0 || values >= smith_protocol_random_values) {
            if (top->object)
                smith_json_write_object_end(&writer);
            else
                smith_json_write_array_end(&writer);
            depth--;
            continue;
        }
        top->remaining--;
        if (top->object) {
            char name[smith_format_number_capacity + 1];
            size_t at = (size_t)(smith_format_write_u64(
                (struct smith_array_char_n){.data = name + 1, .count = {smith_format_number_capacity}}, step));
            name[0] = 'k';
            smith_json_write_key(&writer, (struct smith_array_const_char_n){.data = name, .count = {at + 1}});
        }
        due = true;
    }
    struct smith_array_const_char_n text = {};
    bool finished = smith_json_writer_finish(&writer, &text);
    if (!finished) __builtin_trap(); // the document fits its buffer
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    bool parsed = smith_json_parse(
        arena, text,
        (SmithJsonLimits){.depth = smith_protocol_random_depth + 1, .string_length = capacity, .count = capacity},
        &root, &error);
    if (!parsed) __builtin_trap(); // it is the text the writer just wrote
    (void)finished;
    (void)parsed;
    return root;
}

void smith_protocol_any_random(SmithArena *arena, SmithKey key, SmithProtocolAny *out) {
    *out = smith_protocol_random_tree(arena, key, false);
}

void smith_protocol_object_random(SmithArena *arena, SmithKey key, SmithProtocolAny *out) {
    *out = smith_protocol_random_tree(arena, key, true);
}

// ---------------------------------------------------------------------------------------------------------------
// The members a schema does not list

bool smith_protocol_expect_object(const SmithJsonValue *value, SmithProtocolError *error) {
    if (value->kind != SmithJsonKind_Object) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    return true;
}

static bool smith_protocol_known(struct smith_array_const_char_n key, const char *const *known, size_t known_count) {
    for (size_t i = 0; i < known_count; i++) { // bounded by the schema's list of members
        if (smith_format_is(key, known[i])) return true;
    }
    return false;
}

bool smith_protocol_extra_parse(SmithArena *arena, const SmithJsonValue *object, const char *const *known,
                                size_t known_count, SmithProtocolExtra *out, SmithProtocolError *error) {
    size_t count = 0;
    for (size_t i = 0; i < object->count; i++) { // bounded by the object's members
        if (!smith_protocol_known(object->items[i]->key, known, known_count)) count++;
    }
    *out = (SmithProtocolExtra){};
    if (count == 0) return true;
    struct smith_array_const_char_n *keys = nullptr;
    void *block = nullptr;
    if (!smith_protocol_map_parse(arena, object, sizeof(SmithProtocolAny), alignof(SmithProtocolAny), &keys, &block,
                                  error))
        return false;
    SmithProtocolAny *values = (SmithProtocolAny *)block;
    // map_parse took every member's key; keep the members the schema does not list.
    size_t kept = 0;
    for (size_t i = 0; i < object->count; i++) { // bounded by the object's members
        if (smith_protocol_known(keys[i], known, known_count)) continue;
        keys[kept] = keys[i];
        values[kept] = object->items[i];
        kept++;
    }
    if (kept != count) __builtin_trap(); // the first pass counted the same members
    out->keys = keys;
    out->values = values;
    out->count = count;
    return true;
}

void smith_protocol_extra_write(SmithJsonWriter *writer, const SmithProtocolExtra *extra) {
    for (size_t i = 0; i < extra->count; i++) { // bounded by the count
        smith_json_write_key(writer, extra->keys[i]);
        smith_protocol_any_write(writer, &extra->values[i]);
    }
}

bool smith_protocol_extra_equal(const SmithProtocolExtra *a, const SmithProtocolExtra *b) {
    if (a->count != b->count) return false;
    for (size_t i = 0; i < a->count; i++) { // bounded by the count
        if (!smith_protocol_string_equal(&a->keys[i], &b->keys[i])) return false;
        if (!smith_protocol_any_equal(&a->values[i], &b->values[i])) return false;
    }
    return true;
}

void smith_protocol_extra_random(SmithArena *arena, SmithKey key, SmithProtocolExtra *out) {
    SmithKey *stream = &key;
    size_t count = smith_protocol_count(smith_protocol_take(stream));
    *out = (SmithProtocolExtra){};
    if (count == 0) return;
    struct smith_array_const_char_n *keys = smith_protocol_block(arena, sizeof(struct smith_array_const_char_n),
                                                                 alignof(struct smith_array_const_char_n), count);
    SmithProtocolAny *values =
        (SmithProtocolAny *)smith_protocol_block(arena, sizeof(SmithProtocolAny), alignof(SmithProtocolAny), count);
    for (size_t i = 0; i < count; i++) { // bounded by the count
        keys[i] = smith_protocol_name(arena, i);
        smith_protocol_any_random(arena, smith_protocol_take(stream), &values[i]);
    }
    out->keys = keys;
    out->values = values;
    out->count = count;
}

// ---------------------------------------------------------------------------------------------------------------
// Arrays, maps, constants and enums

static bool smith_protocol_block_fits(const SmithArena *arena, size_t size, size_t align, size_t count) {
    return count <= SIZE_MAX / size / 2 && smith_arena_fits(arena, size * count, align);
}

bool smith_protocol_array_parse(SmithArena *arena, const SmithJsonValue *value, size_t size, size_t align, void **items,
                                SmithProtocolError *error) {
    if (value->kind != SmithJsonKind_Array) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    *items = nullptr;
    if (value->count == 0) return true;
    if (!smith_protocol_block_fits(arena, size, align, value->count))
        return smith_protocol_fail(error, SmithProtocolReason_OutOfMemory, value);
    *items = smith_protocol_block(arena, size, align, value->count);
    return true;
}

bool smith_protocol_map_parse(SmithArena *arena, const SmithJsonValue *value, size_t size, size_t align,
                              struct smith_array_const_char_n **keys, void **items, SmithProtocolError *error) {
    if (value->kind != SmithJsonKind_Object) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    *keys = nullptr;
    *items = nullptr;
    if (value->count == 0) return true;
    if (!smith_protocol_block_fits(arena, size, align, value->count) ||
        !smith_protocol_block_fits(arena, sizeof(struct smith_array_const_char_n),
                                   alignof(struct smith_array_const_char_n), value->count))
        return smith_protocol_fail(error, SmithProtocolReason_OutOfMemory, value);
    *items = smith_protocol_block(arena, size, align, value->count);
    struct smith_array_const_char_n *names = smith_protocol_block(
        arena, sizeof(struct smith_array_const_char_n), alignof(struct smith_array_const_char_n), value->count);
    for (size_t i = 0; i < value->count; i++) { // bounded by the object's members
        names[i] = value->items[i]->key;
    }
    *keys = names;
    return true;
}

bool smith_protocol_const_text(const SmithJsonValue *value, const char *expected, SmithProtocolError *error) {
    if (value->kind != SmithJsonKind_String) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    if (!smith_format_is(value->text, expected)) return smith_protocol_fail(error, SmithProtocolReason_BadValue, value);
    return true;
}

bool smith_protocol_const_integer(const SmithJsonValue *value, int64_t expected, SmithProtocolError *error) {
    if (value->kind != SmithJsonKind_Number) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    smith_format_I64Result parsed = smith_format_parse_i64(value->text);
    if (parsed.tag != smith_enum_result_i64__SmithFormatError_tag_ok) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    int64_t integer = parsed.ok;
    if (integer != expected) return smith_protocol_fail(error, SmithProtocolReason_BadValue, value);
    return true;
}

bool smith_protocol_enum_lookup(const SmithJsonValue *value, const char *const *names, size_t count, size_t *index,
                                SmithProtocolError *error) {
    if (value->kind != SmithJsonKind_String) return smith_protocol_fail(error, SmithProtocolReason_WrongType, value);
    *index = count;
    for (size_t i = 0; i < count; i++) { // bounded by the enum's values
        if (smith_format_is(value->text, names[i])) {
            *index = i;
            break;
        }
    }
    return true;
}

void smith_protocol_key(SmithJsonWriter *writer, const char *name) {
    smith_json_write_key(writer, smith_format_string(name));
}

struct smith_array_const_char_n smith_protocol_name(SmithArena *arena, size_t i) {
    char *name = smith_arena_push_array(arena, 1, 24, 1);
    name[0] = '~';
    name[1] = 'k';
    size_t length = smith_format_write_u64((struct smith_array_char_n){.data = name + 2, .count = {22}}, i);
    return (struct smith_array_const_char_n){.data = name, .count = {length + 2}};
}
