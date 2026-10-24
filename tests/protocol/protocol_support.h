// What the generated protocol tests (tests/protocol/test_protocol_dap.c and _mcp.c) share: a writer with room for any
// random value, the checks on a parse that failed or should have, and a tree with one member cut out.
#pragma once

#include "protocol.h"

#include <smith/test.h>

// A writer into 1 MiB of the arena, compact.
[[nodiscard]] SmithJsonWriter protocol_test_writer(SmithArena *arena);
// What the writer wrote; the test fails if the writer did.
[[nodiscard]] struct smith_array_const_char_n protocol_test_text(SmithJsonWriter *writer);
// The tree of a document the test wrote; fails if it is not JSON.
[[nodiscard]] const SmithJsonValue *protocol_test_parse(SmithArena *arena, struct smith_array_const_char_n text);
// Fails the test, saying why and where, when a parse of a valid value was refused.
void protocol_test_accepted(bool accepted, const SmithProtocolError *error);
// Fails the test, showing the document, when the value read back is not the value written.
void protocol_test_equal(bool equal, struct smith_array_const_char_n text);
void protocol_test_same_text(struct smith_array_const_char_n a, struct smith_array_const_char_n b);
// The object with every member named `name` left out.
[[nodiscard]] const SmithJsonValue *protocol_test_without(SmithArena *arena, const SmithJsonValue *tree,
                                                          const char *name);
// Fails the test unless a parse was refused for the missing member `field`.
void protocol_test_refused(bool accepted, const SmithProtocolError *error, const char *field);
