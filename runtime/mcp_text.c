// The text a tool result is built in, and the readers of a tool's arguments. Included by mcp.c.

// A bounded text in the arena. Once it is full the rest is dropped and `truncated` says so, so a result is never
// longer than smith_mcp_text_max however much the program wrote.
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
    bool truncated;
    const char *tag; // the last output source named in the text, so that a run of one source is named once
} SmithMcpText;

static SmithMcpText smith_mcp_text_make(SmithArena *arena, size_t capacity) {
    SmithMcpText text = {.data = smith_arena_push(arena, capacity + 1, 1), .capacity = capacity};
    SMITH_ASSERT(text.data != nullptr);
    text.data[0] = '\0';
    return text;
}

static void smith_mcp_text_bytes(SmithMcpText *text, const char *bytes, size_t size) {
    size_t room = text->capacity - text->size;
    size_t take = size < room ? size : room;
    memcpy(text->data + text->size, bytes, take);
    text->size += take;
    text->data[text->size] = '\0';
    if (take < size) text->truncated = true;
}

static void smith_mcp_text_add(SmithMcpText *text, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void smith_mcp_text_add(SmithMcpText *text, const char *format, ...) {
    char piece[2048];
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(piece, sizeof piece, format, arguments);
    va_end(arguments);
    SMITH_ASSERT(written >= 0);
    size_t size = (size_t)written < sizeof piece ? (size_t)written : sizeof piece - 1; // a longer piece is cut
    smith_mcp_text_bytes(text, piece, size);
}

static void smith_mcp_text_add_text(SmithMcpText *text, struct smith_array_const_char_n piece) {
    smith_mcp_text_bytes(text, piece.data, piece.count[0]);
}

// A copy of the text with a terminator, for the functions that take a C string.
static char *smith_mcp_cstring(SmithArena *arena, struct smith_array_const_char_n text) {
    char *copy = smith_arena_push(arena, text.count[0] + 1, 1);
    SMITH_ASSERT(copy != nullptr);
    memcpy(copy, text.data, text.count[0]);
    copy[text.count[0]] = '\0';
    return copy;
}

// Copies the text into a fixed buffer, cut to fit.
static void smith_mcp_copy(char *to, size_t capacity, struct smith_array_const_char_n from) {
    SMITH_ASSERT(capacity > 0);
    size_t take = from.count[0] < capacity - 1 ? from.count[0] : capacity - 1;
    memcpy(to, from.data, take);
    to[take] = '\0';
}

// A tool's arguments: a member of the object when it has the kind, else nothing (so a wrong type is an absent one, and
// the tool says what it needs).
static const SmithJsonValue *smith_mcp_argument(const SmithJsonValue *arguments, const char *name, SmithJsonKind kind) {
    if (arguments == nullptr || arguments->kind != SmithJsonKind_Object) return nullptr;
    const SmithJsonValue *member = smith_json_member(arguments, name);
    return member != nullptr && member->kind == kind ? member : nullptr;
}

static const char *smith_mcp_argument_string(SmithArena *arena, const SmithJsonValue *arguments, const char *name) {
    const SmithJsonValue *member = smith_mcp_argument(arguments, name, SmithJsonKind_String);
    return member != nullptr ? smith_mcp_cstring(arena, member->text) : nullptr;
}

static int64_t smith_mcp_argument_integer(const SmithJsonValue *arguments, const char *name, int64_t fallback) {
    const SmithJsonValue *member = smith_mcp_argument(arguments, name, SmithJsonKind_Number);
    int64_t value = 0;
    return member != nullptr && smith_text_parse_i64(member->text, &value) ? value : fallback;
}

static bool smith_mcp_argument_bool(const SmithJsonValue *arguments, const char *name, bool fallback) {
    if (arguments == nullptr || arguments->kind != SmithJsonKind_Object) return fallback;
    const SmithJsonValue *member = smith_json_member(arguments, name);
    if (member == nullptr) return fallback;
    if (member->kind == SmithJsonKind_True) return true;
    return member->kind == SmithJsonKind_False ? false : fallback;
}

// A timeout argument in milliseconds, clamped to something a call can wait for, as nanoseconds.
static int64_t smith_mcp_argument_timeout(const SmithJsonValue *arguments, int64_t fallback) {
    int64_t milliseconds = smith_mcp_argument_integer(arguments, "timeout_ms", -1);
    if (milliseconds < 0) return fallback;
    if (milliseconds > 600'000) milliseconds = 600'000; // ten minutes: a longer call is a hung client's
    return milliseconds * 1'000'000;
}
