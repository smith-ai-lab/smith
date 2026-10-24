// What the code scripts/protocol.c generates (runtime/protocol/dap.[ch], mcp.[ch]) stands on: the error a parse
// reports, the readers, writers, comparisons and random makers of the types a JSON Schema starts from (strings,
// integers, numbers, booleans, "any JSON"), and the checks the generated code shares. Hand-written, so the generated
// files stay small; knowledge/protocol-generation.md has the whole design.
//
// Every type, generated or here, has the same four functions, named for the type:
//   bool parse(SmithArena *arena, const SmithJsonValue *value, T *out, SmithProtocolError *error)
//   void write(SmithJsonWriter *writer, const T *value)
//   bool equal(const T *a, const T *b)
//   void random(SmithArena *arena, SmithKey key, T *out)
// A parse reads a tree that smith_json_parse made; what it keeps (arrays, strings with escapes) is pushed into the
// arena, and strings without escapes point into the input, which must outlive the value. A parse that fails leaves
// the arena holding whatever it had pushed: run it inside a scope, or throw the arena away.
#pragma once

// A project finds the headers with -I .smith/include; smith.c, built with one cc command and no -I, finds them beside.
#if __has_include(<smith/arena.h>)
#include <smith/arena.h>
#include <smith/json.h>
#include <smith/random.h>
#include <smith/text.h>
#else
#include "../../include/smith/arena.h"
#include "../../include/smith/json.h"
#include "../../include/smith/random.h"
#include "../../include/smith/text.h"
#endif

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SmithProtocolReason_None,
    SmithProtocolReason_Missing,     // a required member is absent
    SmithProtocolReason_WrongType,   // the JSON is not the kind the schema says
    SmithProtocolReason_BadValue,    // a string that is not one of the enum's, or not the constant
    SmithProtocolReason_NoMatch,     // no alternative of a union takes the value
    SmithProtocolReason_OutOfMemory, // the arena cannot hold what the value needs
} SmithProtocolReason;

// Why a parse failed, and where: the innermost object type and member being read (names the schema's, in static
// storage), and the byte of the input where the offending value began.
typedef struct {
    SmithProtocolReason reason;
    const char *type;
    const char *field;
    size_t offset;
} SmithProtocolError;

[[nodiscard]] const char *smith_protocol_reason_text(SmithProtocolReason reason);

// Failures, for the generated code. Each returns false. fail records the reason at `at` and forgets the type and
// member, so the object being read can name them with in; in keeps what is already named (the innermost).
bool smith_protocol_fail(SmithProtocolError *error, SmithProtocolReason reason, const SmithJsonValue *at);
bool smith_protocol_in(SmithProtocolError *error, const char *type, const char *field);
bool smith_protocol_missing(SmithProtocolError *error, const SmithJsonValue *object, const char *type,
                            const char *field);

// The kinds a union looks at before it tries an alternative.
constexpr unsigned smith_protocol_kind_null = 1U << 0;
constexpr unsigned smith_protocol_kind_bool = 1U << 1;
constexpr unsigned smith_protocol_kind_number = 1U << 2;
constexpr unsigned smith_protocol_kind_string = 1U << 3;
constexpr unsigned smith_protocol_kind_array = 1U << 4;
constexpr unsigned smith_protocol_kind_object = 1U << 5;
constexpr unsigned smith_protocol_kind_any = (1U << 6) - 1;

[[nodiscard]] bool smith_protocol_kind_in(const SmithJsonValue *value, unsigned mask);
// A member that is there and not null: how an optional member of a type that is not itself nullable is "present".
[[nodiscard]] bool smith_protocol_present(const SmithJsonValue *member);

// Any JSON value is kept as the tree it was read as (so nothing is lost, and a field the code does not look inside can
// be written back). nullptr writes as null.
typedef const SmithJsonValue *SmithProtocolAny;

// The types a schema starts from. "object" is any JSON object, kept as a tree; "any" is any JSON value.
[[nodiscard]] bool smith_protocol_string_parse(SmithArena *arena, const SmithJsonValue *value,
                                               struct smith_array_const_char_n *out, SmithProtocolError *error);
void smith_protocol_string_write(SmithJsonWriter *writer, const struct smith_array_const_char_n *value);
[[nodiscard]] bool smith_protocol_string_equal(const struct smith_array_const_char_n *a,
                                               const struct smith_array_const_char_n *b);
void smith_protocol_string_random(SmithArena *arena, SmithKey key, struct smith_array_const_char_n *out);

[[nodiscard]] bool smith_protocol_integer_parse(SmithArena *arena, const SmithJsonValue *value, int64_t *out,
                                                SmithProtocolError *error);
void smith_protocol_integer_write(SmithJsonWriter *writer, const int64_t *value);
[[nodiscard]] bool smith_protocol_integer_equal(const int64_t *a, const int64_t *b);
void smith_protocol_integer_random(SmithArena *arena, SmithKey key, int64_t *out);

[[nodiscard]] bool smith_protocol_number_parse(SmithArena *arena, const SmithJsonValue *value, double *out,
                                               SmithProtocolError *error);
void smith_protocol_number_write(SmithJsonWriter *writer, const double *value);
[[nodiscard]] bool smith_protocol_number_equal(const double *a, const double *b);
void smith_protocol_number_random(SmithArena *arena, SmithKey key, double *out);

[[nodiscard]] bool smith_protocol_boolean_parse(SmithArena *arena, const SmithJsonValue *value, bool *out,
                                                SmithProtocolError *error);
void smith_protocol_boolean_write(SmithJsonWriter *writer, const bool *value);
[[nodiscard]] bool smith_protocol_boolean_equal(const bool *a, const bool *b);
void smith_protocol_boolean_random(SmithArena *arena, SmithKey key, bool *out);

[[nodiscard]] bool smith_protocol_any_parse(SmithArena *arena, const SmithJsonValue *value, SmithProtocolAny *out,
                                            SmithProtocolError *error);
void smith_protocol_any_write(SmithJsonWriter *writer, const SmithProtocolAny *value);
[[nodiscard]] bool smith_protocol_any_equal(const SmithProtocolAny *a, const SmithProtocolAny *b);
void smith_protocol_any_random(SmithArena *arena, SmithKey key, SmithProtocolAny *out);

[[nodiscard]] bool smith_protocol_object_parse(SmithArena *arena, const SmithJsonValue *value, SmithProtocolAny *out,
                                               SmithProtocolError *error);
void smith_protocol_object_write(SmithJsonWriter *writer, const SmithProtocolAny *value);
[[nodiscard]] bool smith_protocol_object_equal(const SmithProtocolAny *a, const SmithProtocolAny *b);
void smith_protocol_object_random(SmithArena *arena, SmithKey key, SmithProtocolAny *out);

// The members of an object that its schema does not list, for a schema that allows more ("additionalProperties").
typedef struct {
    const struct smith_array_const_char_n *keys;
    const SmithProtocolAny *values;
    size_t count;
} SmithProtocolExtra;

[[nodiscard]] bool smith_protocol_extra_parse(SmithArena *arena, const SmithJsonValue *object, const char *const *known,
                                              size_t known_count, SmithProtocolExtra *out, SmithProtocolError *error);
void smith_protocol_extra_write(SmithJsonWriter *writer, const SmithProtocolExtra *extra);
[[nodiscard]] bool smith_protocol_extra_equal(const SmithProtocolExtra *a, const SmithProtocolExtra *b);
void smith_protocol_extra_random(SmithArena *arena, SmithKey key, SmithProtocolExtra *out);

// Shared by the generated parsers.
[[nodiscard]] bool smith_protocol_expect_object(const SmithJsonValue *value, SmithProtocolError *error);
// The array's elements: a zeroed block of count items of size bytes (nullptr for none), pushed into the arena.
[[nodiscard]] bool smith_protocol_array_parse(SmithArena *arena, const SmithJsonValue *value, size_t size, size_t align,
                                              void **items, SmithProtocolError *error);
// An object's members as a block of keys and one of items of size bytes.
[[nodiscard]] bool smith_protocol_map_parse(SmithArena *arena, const SmithJsonValue *value, size_t size, size_t align,
                                            struct smith_array_const_char_n **keys, void **items,
                                            SmithProtocolError *error);
[[nodiscard]] bool smith_protocol_const_text(const SmithJsonValue *value, const char *expected,
                                             SmithProtocolError *error);
[[nodiscard]] bool smith_protocol_const_integer(const SmithJsonValue *value, int64_t expected,
                                                SmithProtocolError *error);
// The index of the string in names, else count; false if the value is not a string.
[[nodiscard]] bool smith_protocol_enum_lookup(const SmithJsonValue *value, const char *const *names, size_t count,
                                              size_t *index, SmithProtocolError *error);
[[nodiscard]] bool smith_protocol_text_is(struct smith_array_const_char_n text, const char *string);

// The writer's side.
void smith_protocol_key(SmithJsonWriter *writer, const char *name);
// The name "~k<i>" in the arena: the key of the i-th member of a random map.
[[nodiscard]] struct smith_array_const_char_n smith_protocol_name(SmithArena *arena, size_t i);

// The random makers' choices. All come from the key, so a key replays a value. word 0 of each is the simplest choice.
[[nodiscard]] SmithKey smith_protocol_take(SmithKey *stream);         // the next key of a stream
[[nodiscard]] bool smith_protocol_coin(SmithKey key);                 // true half the time
[[nodiscard]] size_t smith_protocol_count(SmithKey key);              // 0 to 2 elements
[[nodiscard]] size_t smith_protocol_pick(SmithKey key, size_t count); // 0 to count - 1
[[nodiscard]] void *smith_protocol_block(SmithArena *arena, size_t size, size_t align, size_t count);
