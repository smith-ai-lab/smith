// The checks the generated protocol tests share; protocol_support.h says what they are.
#include "protocol_support.h"

#include <string.h>

constexpr size_t protocol_test_capacity = 1 << 20;
constexpr SmithJsonLimits protocol_test_limits = {.depth = 64, .string_length = 1 << 20, .count = 1 << 20};

SmithJsonWriter protocol_test_writer(SmithArena *arena) {
    char *buffer = smith_arena_push_array(arena, 1, protocol_test_capacity, 1);
    return smith_json_writer_make(buffer, protocol_test_capacity, 0);
}

SmithText protocol_test_text(SmithJsonWriter *writer) {
    SmithText text = {};
    bool finished = smith_json_writer_finish(writer, &text);
    if (!finished) {
        smith_test_note("the writer failed at byte %zu: %s", writer->error.offset,
                        smith_json_reason_text(writer->error.reason));
    }
    smith_check(finished);
    return text;
}

const SmithJsonValue *protocol_test_parse(SmithArena *arena, SmithText text) {
    const SmithJsonValue *root = nullptr;
    SmithJsonError error = {};
    bool parsed = smith_json_parse(arena, text, protocol_test_limits, &root, &error);
    if (!parsed) {
        smith_test_note("the document is not JSON at byte %zu: %s", error.offset, smith_json_reason_text(error.reason));
    }
    smith_check(parsed);
    return root;
}

void protocol_test_accepted(bool accepted, const SmithProtocolError *error) {
    if (!accepted) {
        smith_test_note("refused (%s) in %s.%s at byte %zu", smith_protocol_reason_text(error->reason),
                        error->type != nullptr ? error->type : "?", error->field != nullptr ? error->field : "?",
                        error->offset);
    }
    smith_check(accepted);
}

void protocol_test_equal(bool equal, SmithText text) {
    if (!equal) smith_test_note("written as %.*s", (int)(text.length < 3000 ? text.length : 3000), text.begin);
    smith_check(equal);
}

void protocol_test_same_text(SmithText a, SmithText b) {
    smith_check_equal_u64(a.length, b.length);
    smith_check(memcmp(a.begin, b.begin, a.length) == 0);
}

const SmithJsonValue *protocol_test_without(SmithArena *arena, const SmithJsonValue *tree, const char *name) {
    SmithJsonWriter writer = protocol_test_writer(arena);
    smith_json_write_object_begin(&writer);
    for (size_t i = 0; i < tree->count; i++) { // bounded by the members
        if (smith_text_is(tree->items[i]->key, name)) continue;
        smith_json_write_key(&writer, tree->items[i]->key);
        smith_json_write_value(&writer, tree->items[i]);
    }
    smith_json_write_object_end(&writer);
    return protocol_test_parse(arena, protocol_test_text(&writer));
}

void protocol_test_refused(bool accepted, const SmithProtocolError *error, const char *field) {
    smith_test_note("without \"%s\"", field);
    smith_check(!accepted);
    smith_check_equal_u64((uint64_t)error->reason, (uint64_t)SmithProtocolReason_Missing);
    smith_check(error->field != nullptr && strcmp(error->field, field) == 0);
}
