// <smith/json.h>: reading and writing JSON (RFC 8259) in memory, with every loop bounded and no malloc. Text is a
// pointer and a length (SmithText), so a file read into memory is read where it lies. Numbers go through
// <smith/text.h>, which calls no C library function, so a double written here reads back as the same bits on any
// machine.
//
// Reading has two layers. The reader is a pull parser that walks values without building anything:
//
//   SmithJsonReader reader = smith_json_reader_make(input, limits);
//   while (smith_json_read(&reader) != SmithJsonToken_End) { ... }
//
// and smith_json_parse builds a tree in an arena on top of it, for callers that want random access:
//
//   const SmithJsonValue *root;
//   SmithJsonError error;
//   if (!smith_json_parse(&arena, input, limits, &root, &error)) { ...error.offset, smith_json_reason_text(...) }
//
// Both refuse what RFC 8259 refuses, and a few things it permits that no UTF-8 text can hold: a lone surrogate escape,
// and a byte order mark. Failures are values: a byte offset and a reason, never an exit. Strings are decoded
// (escapes, surrogate pairs) and their UTF-8 validated; a string needing no decoding is a slice of the input. A
// number is kept as its text, and read with smith_text_parse_i64, _u64 or _f64 on it, which refuse what does not fit.
//
// The writer puts values into a buffer, handling commas, escaping, nesting and an optional indent, and fails (once,
// stickily) rather than write invalid JSON.
#pragma once

#include <smith/arena.h>
#include <smith/text.h>

#include <stddef.h>
#include <stdint.h>

// The deepest nesting a limit may ask for.
constexpr size_t smith_json_depth_max = 512;

// What the caller allows. A document over a limit is an error, not a truncation.
typedef struct {
    size_t depth;         // arrays and objects inside each other: 0 allows only scalars
    size_t string_length; // the bytes of one string or key, decoded
    size_t count;         // values in the document (keys not counted)
} SmithJsonLimits;

typedef enum {
    SmithJsonReason_None,
    SmithJsonReason_UnexpectedEnd,       // the input ended inside a value or before one
    SmithJsonReason_UnexpectedCharacter, // a byte that cannot come here
    SmithJsonReason_TrailingContent,     // something after the document
    SmithJsonReason_BadNumber,
    SmithJsonReason_BadEscape,
    SmithJsonReason_BadSurrogate,     // a surrogate escape not part of a pair
    SmithJsonReason_BadUtf8,          // in a string, or written
    SmithJsonReason_ControlCharacter, // a byte below 0x20 in a string
    SmithJsonReason_TooDeep,
    SmithJsonReason_StringTooLong,
    SmithJsonReason_TooMany,
    SmithJsonReason_OutOfMemory, // the arena cannot hold the tree
    SmithJsonReason_NotFinite,   // the writer was given infinity or nan
    SmithJsonReason_Misplaced,   // the writer was given a value, key or end where it cannot go
    SmithJsonReason_Full,        // the writer's buffer is full
} SmithJsonReason;

// Where and why. offset is a byte of the input (of the output, for the writer).
typedef struct {
    size_t offset;
    SmithJsonReason reason;
} SmithJsonError;

[[nodiscard]] const char *smith_json_reason_text(SmithJsonReason reason);

// ---------------------------------------------------------------------------------------------------------------
// The pull parser

typedef enum {
    SmithJsonToken_Error, // reader.error says where and why; the reader stays at it
    SmithJsonToken_End,   // the document is complete and the input is used up
    SmithJsonToken_ObjectBegin,
    SmithJsonToken_ObjectEnd,
    SmithJsonToken_ArrayBegin,
    SmithJsonToken_ArrayEnd,
    SmithJsonToken_Key, // a member's name; the value follows
    SmithJsonToken_String,
    SmithJsonToken_Number, // reader.text is the number as written
    SmithJsonToken_True,
    SmithJsonToken_False,
    SmithJsonToken_Null,
} SmithJsonToken;

// A cursor. Make one with smith_json_reader_make, advance it with smith_json_read, and read the fields; the rest is the
// parser's. It is a value (a few hundred bytes of stack), so a copy is a bookmark.
typedef struct {
    SmithJsonToken token; // the last one read
    size_t offset;        // its first byte in the input
    SmithText text;       // Key and String: the text between the quotes, as written (escapes not decoded); Number: it
    size_t length;        // Key and String: the bytes decoded, at most limits.string_length
    bool escaped;         // Key and String: text holds escapes
    SmithJsonError error;
    SmithText input;
    SmithJsonLimits limits;
    size_t position;
    size_t depth;
    size_t values;
    uint32_t state;
    uint64_t objects[smith_json_depth_max / 64]; // bit d set: container d is an object
} SmithJsonReader;

// asserts limits.depth <= smith_json_depth_max.
[[nodiscard]] SmithJsonReader smith_json_reader_make(SmithText input, SmithJsonLimits limits);

// The next token. After an Error it returns Error again; after End, End.
SmithJsonToken smith_json_read(SmithJsonReader *reader);

// Skips the value that began with the last token: nothing for a scalar, up to the matching end for an object or an
// array (a Key's value is not part of it: read the value, then skip it). Returns false at an error.
[[nodiscard]] bool smith_json_skip(SmithJsonReader *reader);

// The decoded text of the last Key or String. When it holds no escape it is the input itself; otherwise it is pushed
// into the arena (length bytes, and a terminating zero that length does not count).
[[nodiscard]] SmithText smith_json_decode(const SmithJsonReader *reader, SmithArena *arena);

// Whether the last Key or String, decoded, is exactly the terminated string. Needs no arena.
[[nodiscard]] bool smith_json_is(const SmithJsonReader *reader, const char *string);

// ---------------------------------------------------------------------------------------------------------------
// The tree

typedef enum {
    SmithJsonKind_Null,
    SmithJsonKind_False,
    SmithJsonKind_True,
    SmithJsonKind_Number,
    SmithJsonKind_String,
    SmithJsonKind_Array,
    SmithJsonKind_Object,
} SmithJsonKind;

typedef struct SmithJsonValue SmithJsonValue;
struct SmithJsonValue {
    SmithJsonKind kind;
    size_t offset;                      // where it began in the input
    SmithText key;                      // an object member's name, decoded; empty elsewhere
    SmithText text;                     // String: decoded. Number: as written. Else empty.
    size_t count;                       // Array: elements. Object: members.
    const SmithJsonValue *const *items; // the elements, or the members' values, in order
    size_t span;                        // values in it, itself and everything inside included
};

// Parses a whole document into the arena. The input must outlive the tree (strings without escapes and numbers point
// into it); the tree lives as long as the arena. Returns false, and writes only *error, for a document that is invalid,
// over a limit, or too big for the arena (the arena is then unchanged). Duplicate keys are kept.
[[nodiscard]] bool smith_json_parse(SmithArena *arena, SmithText input, SmithJsonLimits limits,
                                    const SmithJsonValue **root, SmithJsonError *error);

// The value of the named member of an object (the last, if the key repeats), or nullptr.
[[nodiscard]] const SmithJsonValue *smith_json_member(const SmithJsonValue *object, const char *key);

// ---------------------------------------------------------------------------------------------------------------
// The writer

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    size_t indent; // spaces per level, 0 for none
    SmithJsonError error;
    size_t depth;
    uint32_t state;
    uint64_t objects[smith_json_depth_max / 64];
    uint64_t filled[smith_json_depth_max / 64]; // bit d set: container d has an element
} SmithJsonWriter;

// A writer into buffer, which has room for capacity characters. indent is the spaces per nesting level: with 0 the
// output has no spaces at all.
[[nodiscard]] SmithJsonWriter smith_json_writer_make(char *buffer, size_t capacity, size_t indent);

// Each call adds one value (or a key, or an end). The first mistake (a value where a key is due, an end that closes
// nothing, a string that is not UTF-8, a full buffer) is kept in writer.error and every later call does nothing.
void smith_json_write_object_begin(SmithJsonWriter *writer);
void smith_json_write_object_end(SmithJsonWriter *writer);
void smith_json_write_array_begin(SmithJsonWriter *writer);
void smith_json_write_array_end(SmithJsonWriter *writer);
void smith_json_write_key(SmithJsonWriter *writer, SmithText key);
void smith_json_write_string(SmithJsonWriter *writer, SmithText string); // UTF-8, escaped as needed
void smith_json_write_u64(SmithJsonWriter *writer, uint64_t value);
void smith_json_write_i64(SmithJsonWriter *writer, int64_t value);
void smith_json_write_f64(SmithJsonWriter *writer, double value); // the fewest digits that read back as the same bits
void smith_json_write_bool(SmithJsonWriter *writer, bool value);
void smith_json_write_null(SmithJsonWriter *writer);

// Writes a whole tree that smith_json_parse made (or one built the same way) as one value: strings escaped again,
// numbers as they were written, object members in order, duplicate keys kept. Nothing recurses: it keeps its place in
// a stack of smith_json_depth_max entries, so a tree deeper than that asserts. Used to put a value the caller
// does not look inside (a protocol's "any" field) back into a document.
void smith_json_write_value(SmithJsonWriter *writer, const SmithJsonValue *value);

// The document written, once exactly one complete value has been. Returns false, with writer.error set, if a call
// failed or the value is not finished. The text has no terminator.
[[nodiscard]] bool smith_json_writer_finish(SmithJsonWriter *writer, SmithText *output);
