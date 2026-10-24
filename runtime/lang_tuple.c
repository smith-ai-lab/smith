// Tuples (issue #96): `struct { i32, f32 }` is a struct whose fields have no names, a syntax error in C, so it is
// smith's. Its identity is its shape: the same fields in order are the same type in every file and module. It lowers,
// where it is written, to the struct `struct smith_tuple_i32__f32 { int32_t _0; float _1; }` (C23 accepts the same
// definition again, in any scope); `t[0]` is `t._0` and `countof(t)` the number of fields. Included by runtime/lang.c,
// before runtime/lang_stream.c.
//
// The tag is derived from the fields' types, spelled the same way in every pass (a module's own compile, its header
// and the raw lowering, which has no parser): a field's type is named by what is written, with a module's names
// resolved (`Meters` in geometry and `geometry::Meters` elsewhere are `geometry_Meters`) and the sized, stdint and
// basic C types unified (`i32`, `int32_t` and `int` are `i32`). A typedef is not looked through: `struct { Meters,
// f32 }` and `struct { f64, f32 }` are different types even when Meters is f64. The generated header hoists each
// definition out of the signatures (smith_lang_tuple_hoist).

constexpr size_t smith_lang_tuple_depth_max = 8;      // tuples inside tuples
constexpr size_t smith_lang_tuple_name_max = 512;     // the spelling of a tuple's fields
constexpr size_t smith_lang_tuple_fields_max = 128;   // fields of one tuple
constexpr size_t smith_lang_tuple_text_max = 1 << 15; // the ring of text the tokens made here point into
constexpr size_t smith_lang_tuple_long = 120;         // a spelling longer than this is cut and hashed
constexpr char smith_lang_tuple_prefix[] = "smith_tuple_";

static char smith_lang_tuple_text[smith_lang_tuple_text_max];
static size_t smith_lang_tuple_used;

// Text for a token made here. It lives until the ring wraps, long after the token has been used.
static const char *smith_lang_tuple_keep(const char *text, size_t length) {
    if (smith_lang_tuple_used + length + 1 > smith_lang_tuple_text_max) smith_lang_tuple_used = 0;
    char *at = smith_lang_tuple_text + smith_lang_tuple_used;
    memcpy(at, text, length);
    at[length] = 0;
    smith_lang_tuple_used += length + 1;
    return at;
}

// ---- The spelling of a type

// One field's type as it is read: the specifiers, the name when it is not a basic type, and the qualifiers and
// pointers around it. A tuple that is a field is a frame of its own, and the frame under it holds its fields so far.
typedef struct {
    unsigned longs, shorts, signs, unsigns;
    bool is_char, is_int, is_float, is_double, is_bool, is_void;
    bool pointer; // after a `*`: the qualifiers are the pointer's
    bool has_name;
    char name[smith_lang_tuple_name_max];
    char quals[48];
    char ptrs[96];
    char fields[smith_lang_tuple_name_max]; // the spellings of the fields of the tuple this frame is, joined by __
    size_t field_count;
} SmithLangTupleFrame;

typedef struct {
    SmithLangTupleFrame frames[smith_lang_tuple_depth_max];
    size_t depth;
    const char *problem;
    uint32_t where;
} SmithLangTupleReader;

static bool smith_lang_tuple_append(char *buffer, size_t capacity, const char *text) {
    size_t used = strlen(buffer);
    size_t length = strlen(text);
    if (used + length + 1 > capacity) return false;
    memcpy(buffer + used, text, length + 1);
    return true;
}

// The names that are one type: the sized names, the stdint and the C names for the same types (a 64-bit platform).
static const char *smith_lang_tuple_alias(const char *name) {
    static const char *const pairs[][2] = {
        {"int8_t", "i8"},       {"uint8_t", "u8"},     {"int16_t", "i16"},     {"uint16_t", "u16"},
        {"int32_t", "i32"},     {"uint32_t", "u32"},   {"int64_t", "i64"},     {"uint64_t", "u64"},
        {"ssize_t", "isize"},   {"intptr_t", "isize"}, {"ptrdiff_t", "isize"}, {"size_t", "usize"},
        {"uintptr_t", "usize"}, {"_Bool", "bool"},     {"_Float16", "f16"},
    };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) { // 15 entries
        if (strcmp(name, pairs[i][0]) == 0) return pairs[i][1];
    }
    return name;
}

// The name of a basic type the specifiers add up to.
static const char *smith_lang_tuple_basic(const SmithLangTupleFrame *frame) {
    bool sign = frame->unsigns > 0;
    if (frame->is_void) return "void";
    if (frame->is_bool) return "bool";
    if (frame->is_float) return "f32";
    if (frame->is_double) return frame->longs > 0 ? "ldouble" : "f64";
    if (frame->is_char) return frame->unsigns > 0 ? "u8" : frame->signs > 0 ? "i8" : "char";
    if (frame->shorts > 0) return sign ? "u16" : "i16";
    if (frame->longs == 1) return sign ? "usize" : "isize";
    if (frame->longs >= 2) return sign ? "u64" : "i64";
    return sign ? "u32" : "i32";
}

static bool smith_lang_tuple_has_type(const SmithLangTupleFrame *frame) {
    return frame->has_name || frame->longs > 0 || frame->shorts > 0 || frame->signs > 0 || frame->unsigns > 0 ||
           frame->is_char || frame->is_int || frame->is_float || frame->is_double || frame->is_bool || frame->is_void;
}

// A finished field: its spelling is added to the frame's fields and the frame is ready for the next.
static bool smith_lang_tuple_finish(SmithLangTupleReader *reader, uint32_t where) {
    SmithLangTupleFrame *frame = &reader->frames[reader->depth];
    if (!smith_lang_tuple_has_type(frame)) {
        reader->problem = "a tuple field is a type, and this one is empty";
        reader->where = where;
        return false;
    }
    char spelled[smith_lang_tuple_name_max] = "";
    bool good = smith_lang_tuple_append(spelled, sizeof spelled, frame->quals) &&
                smith_lang_tuple_append(spelled, sizeof spelled,
                                        frame->has_name ? frame->name : smith_lang_tuple_basic(frame)) &&
                smith_lang_tuple_append(spelled, sizeof spelled, frame->ptrs);
    if (frame->field_count > 0) good = good && smith_lang_tuple_append(frame->fields, sizeof frame->fields, "__");
    good = good && smith_lang_tuple_append(frame->fields, sizeof frame->fields, spelled);
    if (!good) {
        reader->problem = "the types of this tuple are too long to name it";
        reader->where = where;
        return false;
    }
    frame->field_count += 1;
    char keep[smith_lang_tuple_name_max];
    memcpy(keep, frame->fields, sizeof keep);
    size_t count = frame->field_count;
    *frame = (SmithLangTupleFrame){};
    memcpy(frame->fields, keep, sizeof keep);
    frame->field_count = count;
    return true;
}

// The name an identifier at span[i] is written with (`x::Name` takes three tokens), as the module resolves it. Returns
// the tokens it took.
static size_t smith_lang_tuple_resolve(SmithLang *lang, const SmithToken *span, size_t i, size_t to, char *out,
                                       size_t capacity) {
    const SmithToken *id = &span[i];
    if (i + 2 < to && smith_lang_punct(&span[i + 1], "::") && span[i + 2].kind == SmithTokenKind_Identifier &&
        lang->bindings_count > 0) {
        const SmithLangBinding *binding = smith_lang_binding_find(lang, id->text, id->length);
        if (binding != nullptr) {
            const SmithToken *member = &span[i + 2];
            char name[smith_lang_name_max];
            const char *prefix = binding->is_c ? "" : smith_lang_str(binding->prefix);
            if (strlen(prefix) + member->length + 1 <= sizeof name) {
                snprintf(name, sizeof name, "%s%.*s", prefix, (int)member->length, member->text);
                const SmithLangCName *chosen = !binding->is_c && lang->cnames_count > 0
                                                   ? smith_lang_cname_find(lang, name, strlen(name))
                                                   : nullptr;
                snprintf(out, capacity, "%s", chosen != nullptr ? smith_lang_str(chosen->c_name) : name);
                return 3;
            }
        }
    }
    if (lang->is_module && smith_lang_name_find(lang, id->text, id->length) != nullptr) {
        snprintf(out, capacity, "%s", smith_lang_prefixed(lang, id->text, id->length));
        return 1;
    }
    snprintf(out, capacity, "%.*s", (int)id->length, id->text);
    return 1;
}

static void smith_lang_tuple_qualifier(SmithLangTupleFrame *frame, const char *word) {
    const char *name = strstr(word, "const")      ? "const"
                       : strstr(word, "volatile") ? "volatile"
                       : strstr(word, "restrict") ? "restrict"
                                                  : "atomic";
    if (frame->pointer) {
        smith_lang_tuple_append(frame->ptrs, sizeof frame->ptrs, "_");
        smith_lang_tuple_append(frame->ptrs, sizeof frame->ptrs, name);
    } else if (!strstr(frame->quals, name)) {
        smith_lang_tuple_append(frame->quals, sizeof frame->quals, name);
        smith_lang_tuple_append(frame->quals, sizeof frame->quals, "_");
    }
}

// A basic type keyword: counted, so that `unsigned long long` and `long unsigned long` are one type.
static bool smith_lang_tuple_keyword(SmithLangTupleFrame *frame, const char *word) {
    if (strcmp(word, "long") == 0)
        frame->longs += 1;
    else if (strcmp(word, "short") == 0)
        frame->shorts += 1;
    else if (strcmp(word, "signed") == 0)
        frame->signs += 1;
    else if (strcmp(word, "unsigned") == 0)
        frame->unsigns += 1;
    else if (strcmp(word, "char") == 0)
        frame->is_char = true;
    else if (strcmp(word, "int") == 0)
        frame->is_int = true;
    else if (strcmp(word, "float") == 0)
        frame->is_float = true;
    else if (strcmp(word, "double") == 0)
        frame->is_double = true;
    else if (strcmp(word, "bool") == 0 || strcmp(word, "_Bool") == 0)
        frame->is_bool = true;
    else if (strcmp(word, "void") == 0)
        frame->is_void = true;
    else
        return false;
    return true;
}

static bool smith_lang_tuple_fail(SmithLangTupleReader *reader, const SmithToken *at, const char *problem) {
    reader->problem = problem;
    reader->where = at->location;
    return false;
}

// `struct {` inside a field opens a frame; its `}` closes it, and the tuple is the name of the field above.
static bool smith_lang_tuple_open(SmithLangTupleReader *reader, const SmithToken *at) {
    if (reader->depth + 1 >= smith_lang_tuple_depth_max) {
        return smith_lang_tuple_fail(reader, at, "tuples are nested too deep to name this one");
    }
    if (smith_lang_tuple_has_type(&reader->frames[reader->depth]) || reader->frames[reader->depth].pointer) {
        return smith_lang_tuple_fail(reader, at, "a tuple field is one type");
    }
    reader->depth += 1;
    reader->frames[reader->depth] = (SmithLangTupleFrame){};
    return true;
}

static bool smith_lang_tuple_close(SmithLangTupleReader *reader, const SmithToken *at) {
    if (reader->depth == 0) return smith_lang_tuple_fail(reader, at, "this `}` closes nothing");
    if (!smith_lang_tuple_finish(reader, at->location)) return false;
    SmithLangTupleFrame *inner = &reader->frames[reader->depth];
    SmithLangTupleFrame *outer = &reader->frames[reader->depth - 1];
    outer->has_name = true;
    outer->name[0] = 0;
    bool good = smith_lang_tuple_append(outer->name, sizeof outer->name, "tuple_") &&
                smith_lang_tuple_append(outer->name, sizeof outer->name, inner->fields) &&
                smith_lang_tuple_append(outer->name, sizeof outer->name, "_end");
    reader->depth -= 1;
    return good || smith_lang_tuple_fail(reader, at, "the types of this tuple are too long to name it");
}

// A tag after struct, union or enum, or any other name of a type.
static bool smith_lang_tuple_named(SmithLang *lang, SmithLangTupleReader *reader, const SmithToken *span, size_t *at,
                                   size_t to, const char *tag) {
    SmithLangTupleFrame *frame = &reader->frames[reader->depth];
    if (smith_lang_tuple_has_type(frame) || frame->pointer) {
        return smith_lang_tuple_fail(reader, &span[*at], "a tuple field is one type");
    }
    size_t from = *at;
    if (tag != nullptr) from += 1;
    if (from >= to || span[from].kind != SmithTokenKind_Identifier) {
        return smith_lang_tuple_fail(reader, &span[*at], "a tuple field is a type");
    }
    char resolved[smith_lang_name_max];
    size_t taken = smith_lang_tuple_resolve(lang, span, from, to, resolved, sizeof resolved);
    frame->name[0] = 0;
    if (tag != nullptr) {
        smith_lang_tuple_append(frame->name, sizeof frame->name, tag);
        smith_lang_tuple_append(frame->name, sizeof frame->name, "_");
    }
    smith_lang_tuple_append(frame->name, sizeof frame->name,
                            tag != nullptr ? resolved : smith_lang_tuple_alias(resolved));
    frame->has_name = true;
    *at = from + taken - 1;
    return true;
}

static bool smith_lang_tuple_word(SmithLang *lang, SmithLangTupleReader *reader, const SmithToken *span, size_t *at,
                                  size_t to) {
    const SmithToken *token = &span[*at];
    SmithLangTupleFrame *frame = &reader->frames[reader->depth];
    SmithLangWord word = smith_lang_word(token->text, token->length);
    char text[48];
    snprintf(text, sizeof text, "%.*s", (int)(token->length < sizeof text ? token->length : sizeof text - 1),
             token->text);
    if (word == SmithLangWord_Qualifier) {
        smith_lang_tuple_qualifier(frame, text);
        return true;
    }
    if (word == SmithLangWord_Tag) {
        if (strcmp(text, "struct") == 0 && *at + 1 < to && smith_lang_punct(&span[*at + 1], "{")) {
            *at += 1;
            return smith_lang_tuple_open(reader, token);
        }
        return smith_lang_tuple_named(lang, reader, span, at, to, text);
    }
    if (word == SmithLangWord_TypeKeyword) {
        if (frame->pointer || frame->has_name || !smith_lang_tuple_keyword(frame, text)) {
            return smith_lang_tuple_fail(reader, token,
                                         "a tuple field is a type name; name other types with a typedef");
        }
        return true;
    }
    if (word != SmithLangWord_Other) return smith_lang_tuple_fail(reader, token, "a tuple field is a type");
    return smith_lang_tuple_named(lang, reader, span, at, to, nullptr);
}

// The fields of the tuple span[from, to) as the spelling that names it, or false with the reason.
static bool smith_lang_tuple_spell(SmithLang *lang, const SmithToken *span, size_t from, size_t to, char *out,
                                   size_t capacity, const char **problem, uint32_t *where) {
    static SmithLangTupleReader reader;
    reader = (SmithLangTupleReader){};
    for (size_t at = from; at < to; at++) { // each token of the fields
        const SmithToken *token = &span[at];
        bool good = false;
        if (token->kind == SmithTokenKind_Identifier) {
            good = smith_lang_tuple_word(lang, &reader, span, &at, to);
        } else if (smith_lang_punct(token, "*") && smith_lang_tuple_has_type(&reader.frames[reader.depth])) {
            SmithLangTupleFrame *frame = &reader.frames[reader.depth];
            frame->pointer = true;
            good = smith_lang_tuple_append(frame->ptrs, sizeof frame->ptrs, "_ptr");
        } else if (smith_lang_punct(token, ",")) {
            good = smith_lang_tuple_finish(&reader, token->location);
        } else if (smith_lang_punct(token, "}")) {
            good = smith_lang_tuple_close(&reader, token);
        } else {
            good = smith_lang_tuple_fail(&reader, token,
                                         "a tuple field is a type name, with stars and qualifiers; name an array, a "
                                         "function pointer or a typeof with a typedef");
        }
        if (!good) {
            *problem = reader.problem != nullptr ? reader.problem : "this tuple cannot be named";
            *where = reader.where;
            return false;
        }
    }
    if (reader.depth != 0) {
        *problem = "this tuple is not closed";
        *where = span[to - 1].location;
        return false;
    }
    if (!smith_lang_tuple_finish(&reader, span[to - 1].location)) {
        *problem = reader.problem;
        *where = reader.where;
        return false;
    }
    snprintf(out, capacity, "%s", reader.frames[0].fields);
    return true;
}

// `smith_tuple_` and the spelling; a long one is cut and ends in a hash of the whole.
static void smith_lang_tuple_tag(const char *spelled, char *tag, size_t capacity) {
    size_t length = strlen(spelled);
    if (length <= smith_lang_tuple_long) {
        snprintf(tag, capacity, "%s%s", smith_lang_tuple_prefix, spelled);
        return;
    }
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < length; i++) { // FNV-1a
        hash = (hash ^ (uint8_t)spelled[i]) * 1099511628211ull;
    }
    snprintf(tag, capacity, "%s%.80s_h%016llx", smith_lang_tuple_prefix, spelled, (unsigned long long)hash);
}

// ---- The queue

// The first `count` tokens of the queue are replaced by `tokens`.
static bool smith_lang_tuple_replace(SmithLang *lang, size_t count, const SmithToken *tokens, size_t made) {
    size_t rest = lang->queue_count - count;
    if (rest + made > smith_lang_queue_max) return false;
    lang->queue_head = (lang->queue_head + count) % smith_lang_queue_max;
    lang->queue_count = rest;
    lang->batch_in += count;
    lang->queue_head = (lang->queue_head + smith_lang_queue_max - made) % smith_lang_queue_max;
    for (size_t i = 0; i < made; i++) // the new tokens
        lang->queue[(lang->queue_head + i) % smith_lang_queue_max] = tokens[i];
    lang->queue_count += made;
    return true;
}

static SmithToken smith_lang_tuple_made(SmithTokenKind kind, const char *text, size_t length, const SmithToken *like,
                                        bool space) {
    SmithToken made = smith_lang_synth(kind, smith_lang_tuple_keep(text, length), length, like);
    made.line_start = false;
    made.space_before = space;
    made.synthetic = true; // it stands for no token of the source
    return made;
}

// ---- A tuple type where it is written: `struct {` and a list of types up to the `}`

// Whether the tokens after `struct` are the fields of a tuple (a `{` and, before its `}`, no `;`): the index of the
// `}` in the queue, or 0.
static size_t smith_lang_tuple_extent(SmithLang *lang, const SmithHost *host) {
    const SmithToken *open = smith_lang_peek(lang, host, 0, false);
    if (!smith_lang_punct(open, "{") || open->foreign) return 0;
    int depth = 1;
    for (size_t at = 1; at < smith_lang_queue_max - 2; at++) { // a tuple is a few tokens
        const SmithToken *token = smith_lang_peek(lang, host, at, false);
        if (token->kind == SmithTokenKind_Eof || token->foreign || smith_lang_punct(token, ";")) return 0;
        if (smith_lang_punct(token, "{")) depth += 1;
        if (smith_lang_punct(token, "}")) {
            depth -= 1;
            if (depth == 0) return at;
        }
    }
    return 0;
}

// The fields' [start, end) in the span, split at the commas that no braces enclose.
static size_t smith_lang_tuple_split(const SmithToken *span, size_t close, size_t *starts, size_t *ends) {
    size_t count = 0;
    size_t start = 1;
    int depth = 0;
    for (size_t at = 1; at <= close; at++) { // each token of the fields, and the closing brace
        bool last = at == close;
        if (!last && smith_lang_punct(&span[at], "{")) depth += 1;
        if (!last && smith_lang_punct(&span[at], "}")) depth -= 1;
        if ((last || (depth == 0 && smith_lang_punct(&span[at], ","))) && count < smith_lang_tuple_fields_max) {
            starts[count] = start;
            ends[count] = at;
            count += 1;
            start = at + 1;
        }
    }
    return count;
}

// After an error in a tuple: it becomes a struct of one int, so that the compiler finds nothing more to say of it.
static void smith_lang_tuple_recover(SmithLang *lang, const SmithToken *token, const SmithToken *span, size_t close) {
    SmithToken made[6];
    made[0] = smith_lang_tuple_made(SmithTokenKind_Identifier, "smith_tuple_invalid", 19, token, true);
    made[1] = span[0];
    made[1].tuple_brace = true;
    made[2] = smith_lang_tuple_made(SmithTokenKind_Identifier, "int", 3, token, true);
    made[3] = smith_lang_tuple_made(SmithTokenKind_Identifier, "_0", 2, token, true);
    made[4] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ";", 1, token, false);
    made[5] = span[close];
    made[5].tuple_brace = true;
    smith_lang_tuple_replace(lang, close + 1, made, 6);
}

// `struct` is being processed and a `{` follows: if it is a tuple, the tokens up to its `}` become the tag, the
// braces and the fields with names (`_0`, `_1`) that the struct is defined with, and the `struct` goes on to them.
static void smith_lang_tuple_begin(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    size_t close = smith_lang_tuple_extent(lang, host);
    if (close == 0) return;
    static SmithToken span[smith_lang_queue_max];
    for (size_t at = 0; at <= close; at++) // `{` to `}`
        span[at] = *smith_lang_peek(lang, host, at, false);
    if (close == 1) {
        smith_lang_error(lang, host, span[0].location,
                         "a tuple has at least one field: `struct { i32, f32 }`; C has no empty struct");
        smith_lang_tuple_recover(lang, token, span, close);
        return;
    }
    size_t starts[smith_lang_tuple_fields_max];
    size_t ends[smith_lang_tuple_fields_max];
    size_t count = smith_lang_tuple_split(span, close, starts, ends);
    char spelled[smith_lang_tuple_name_max];
    const char *problem = nullptr;
    uint32_t where = 0;
    if (!smith_lang_tuple_spell(lang, span, 1, close, spelled, sizeof spelled, &problem, &where)) {
        smith_lang_error(lang, host, where, "%s", problem);
        smith_lang_tuple_recover(lang, token, span, close);
        return;
    }
    static SmithToken made[smith_lang_queue_max];
    size_t used = 0;
    char tag[smith_lang_tuple_name_max];
    smith_lang_tuple_tag(spelled, tag, sizeof tag);
    made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, tag, strlen(tag), token, true);
    made[used++] = span[0];
    made[used - 1].tuple_brace = true;
    for (size_t field = 0; field < count; field++) { // each field: its type, a name and a semicolon
        if (used + (ends[field] - starts[field]) + 3 >= smith_lang_queue_max - 2) {
            smith_lang_error(lang, host, span[0].location, "this tuple is too long to lower");
            smith_lang_tuple_recover(lang, token, span, close);
            return;
        }
        for (size_t at = starts[field]; at < ends[field]; at++)
            made[used++] = span[at];
        made[used - (ends[field] - starts[field])].space_before = true;
        char name[24];
        int length = snprintf(name, sizeof name, "_%zu", field);
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, name, (size_t)length, token, true);
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ";", 1, token, false);
    }
    made[used++] = span[close];
    made[used - 1].tuple_brace = true;
    if (!smith_lang_tuple_replace(lang, close + 1, made, used)) {
        smith_lang_error(lang, host, span[0].location, "this tuple is too long to lower");
        smith_lang_tuple_recover(lang, token, span, close);
        return;
    }
    lang->tuples_seen = true;
}

// A generic's body is kept as written and replayed for each instance, and a tuple type in it would reach the compiler
// as written. Until the tuple is a type a generic can take (issue #95, ticket 6), it is named outside: a typedef.
static void smith_lang_tuple_in_generic(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (smith_lang_tuple_extent(lang, host) == 0) return;
    smith_lang_error(lang, host, token->location,
                     "a tuple type cannot be written in a generic function yet: name it outside the generic with a "
                     "typedef (`typedef struct { i32, f32 } Pair;`)");
}

static bool smith_lang_slice_countof(SmithLang *lang, const SmithHost *host, const SmithToken *token, const char *tokens,
                                     const SmithToken *span, size_t close);

// ---- An expression that is a tuple: `t[0]` and `countof(t)`

// The token `back` places before the one being lowered: in the arguments of a call to a generic, which are kept and not
// yet written, the kept ones; else the host's record.
static bool smith_lang_tuple_previous(SmithLang *lang, const SmithHost *host, unsigned back, SmithToken *token) {
    if (lang->cap_frames == 0) return smith_lang_previous(lang, host, back, token);
    if (back > smith_lang_gt.count) return false;
    const SmithLangGenToken *kept = &smith_lang_gt.tokens[smith_lang_gt.count - back];
    *token = smith_lang_gt_token(&smith_lang_gt, kept);
    return true;
}

// The tokens of the postfix expression that ends just before the token being lowered, from the host's record of the
// last tokens, separated by \x1f; false when they are not one, or reach back farther than the record does.
static bool smith_lang_tuple_base(SmithLang *lang, const SmithHost *host, char *out, size_t capacity) {
    SmithToken before[8];
    size_t count = 0;
    for (unsigned back = 1; back <= 8; back++) { // the host remembers eight
        if (!smith_lang_tuple_previous(lang, host, back, &before[back - 1])) break;
        count = back;
    }
    size_t at = 0;
    while (at < count) { // an operand, then `.` or `->` and the operand it belongs to
        const SmithToken *token = &before[at];
        bool paren = smith_lang_punct(token, ")");
        bool closes = paren || smith_lang_punct(token, "]");
        if (token->kind == SmithTokenKind_Identifier &&
            smith_lang_word(token->text, token->length) == SmithLangWord_Other) {
            at += 1;
        } else if (closes) {
            const char *open = paren ? "(" : "[";
            const char *close = paren ? ")" : "]";
            int depth = 0;
            size_t match = at;
            for (; match < count; match++) { // back to the opening bracket
                if (smith_lang_punct(&before[match], close)) depth += 1;
                if (smith_lang_punct(&before[match], open) && --depth == 0) break;
            }
            if (match >= count) return false;
            at = match + 1;
            if (!paren) continue; // what is subscripted is the operand to its left
            if (at < count && before[at].kind == SmithTokenKind_Identifier &&
                smith_lang_word(before[at].text, before[at].length) == SmithLangWord_Other) {
                at += 1; // a call: the callee
            }
        } else {
            return false;
        }
        if (at < count && (smith_lang_punct(&before[at], ".") || smith_lang_punct(&before[at], "->"))) {
            at += 1;
            continue;
        }
        break;
    }
    if (at == 0 || (at >= count && count == 8)) return false;
    size_t used = 0;
    for (size_t i = at; i > 0; i--) { // oldest first
        const SmithToken *token = &before[i - 1];
        // A literal has no text in the host's record: only the type of the whole is asked, and a zero has one.
        const char *text = token->length == 0 ? "0" : token->text;
        size_t length = token->length == 0 ? 1 : token->length;
        if (used + length + 2 > capacity) return false;
        memcpy(out + used, text, length);
        used += length;
        if (i > 1) out[used++] = '\x1f';
    }
    out[used] = 0;
    return true;
}

// Whether the tokens (separated by \x1f) are an expression of tuple type, and its type and number of fields.
static bool smith_lang_tuple_typed(const SmithHost *host, const char *tokens, char *type, size_t capacity,
                                   uint32_t *fields) {
    SmithExprType found = {};
    if (host->expr_type == nullptr || !host->expr_type(host->context, tokens, (uint32_t)strlen(tokens), &found)) {
        return false;
    }
    const char *tag = strstr(found.canonical, "struct smith_tuple_");
    if (tag == nullptr || strchr(found.canonical, '*') != nullptr || strchr(found.canonical, '[') != nullptr) {
        return false;
    }
    tag += 7;
    size_t length = strcspn(tag, " ");
    SmithTypeInfo info = {};
    if (host->type_info == nullptr || !host->type_info(host->context, tag, (uint32_t)length, SmithLookup_Tag, &info) ||
        info.fields == 0) {
        return false;
    }
    snprintf(type, capacity, "%.*s", (int)length, tag);
    *fields = info.fields;
    return true;
}

// A tuple type as the program wrote it: `struct { i32, f32 }` for `smith_tuple_i32__f32`.
static void smith_lang_tuple_shown(const char *tag, char *out, size_t capacity) {
    const char *spelled = tag + strlen(smith_lang_tuple_prefix);
    size_t used = (size_t)snprintf(out, capacity, "struct { ");
    for (const char *at = spelled; *at != 0 && used + 4 < capacity; at++) { // `__` between fields
        if (at[0] == '_' && at[1] == '_') {
            used += (size_t)snprintf(out + used, capacity - used, ", ");
            at += 1;
        } else {
            out[used++] = *at;
        }
    }
    snprintf(out + used, capacity - used, " }");
}

// `[` after an expression of tuple type: `t[1]` is `t._1`; the index is a number or a constant. Returns true when the
// subscript was written as the member.
static bool smith_lang_tuple_subscript(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (host->previous == nullptr || host->expr_type == nullptr || lang->recording) return false;
    const SmithToken *index = smith_lang_peek(lang, host, 0, false);
    if (index->foreign || lang->in_attribute || smith_lang_punct(index, "[")) return false;
    char base[512];
    char type[256];
    uint32_t fields = 0;
    bool based = smith_lang_tuple_base(lang, host, base, sizeof base);
    if (!based || !smith_lang_tuple_typed(host, base, type, sizeof type, &fields)) {
        return false;
    }
    char shown[512];
    smith_lang_tuple_shown(type, shown, sizeof shown);
    int64_t value = -1;
    bool constant = false;
    if (index->kind == SmithTokenKind_Number && smith_lang_punct(smith_lang_peek(lang, host, 1, false), "]")) {
        char *end = nullptr;
        char digits[32];
        snprintf(digits, sizeof digits, "%.*s", (int)(index->length < sizeof digits ? index->length : 31), index->text);
        value = (int64_t)strtoull(digits, &end, 0);
        constant = *end == 0;
    } else if (index->kind == SmithTokenKind_Identifier &&
               smith_lang_punct(smith_lang_peek(lang, host, 1, false), "]") && host->constant != nullptr) {
        constant = host->constant(host->context, index->text, index->length, &value);
    }
    uint32_t index_location = index->location;
    bool in_range = constant && value >= 0 && (uint64_t)value < fields;
    if (!constant) {
        smith_lang_error(lang, host, index->location, "the index of a tuple is a constant: `%s` has %u fields, 0 to %u",
                         shown, fields, fields - 1);
    } else if (!in_range) {
        smith_lang_error(lang, host, index->location, "the index %lld is out of range: `%s` has %u fields",
                         (long long)value, shown, fields);
    }
    if (!in_range) value = 0; // the first field stands for it, so that the compiler has nothing more to say
    int depth = 1;
    size_t length_taken = 0;
    while (depth > 0 && length_taken < 64) { // the index, up to the matching `]`
        const SmithToken *inside = smith_lang_peek(lang, host, 0, false);
        if (inside->kind == SmithTokenKind_Eof) break;
        depth += smith_lang_punct(inside, "[") ? 1 : 0;
        depth -= smith_lang_punct(inside, "]") ? 1 : 0;
        smith_lang_take(lang, host);
        length_taken += 1;
    }
    char name[24];
    int length = snprintf(name, sizeof name, "_%lld", (long long)value);
    SmithToken dot = smith_lang_tuple_made(SmithTokenKind_Punctuator, ".", 1, token, false);
    SmithToken member = smith_lang_tuple_made(SmithTokenKind_Identifier, name, (size_t)length, token, false);
    dot.synthetic = false; // they stand for the `[` and the number
    member.synthetic = false;
    member.location = index_location;
    lang->advance_skip = true; // the `[` and `]` of a call's arguments cancel: the call's depth does not see either
    smith_lang_emit(lang, host, dot);
    smith_lang_emit(lang, host, member);
    smith_lang_track(lang, &dot, false);
    smith_lang_track(lang, &member, false);
    return true;
}

// `countof(t)` where t is a tuple: the number of its fields, as the number.
static bool smith_lang_tuple_countof(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (host->previous == nullptr || host->expr_type == nullptr || lang->recording ||
        !smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(")) {
        return false;
    }
    char tokens[512];
    size_t used = 0;
    size_t close = 0;
    int depth = 1;
    SmithToken span[24];
    for (size_t at = 1; at < 24 && close == 0; at++) { // an operand of a few tokens, as the source has them
        span[at] = *smith_lang_peek(lang, host, at, false);
        const SmithToken *next = &span[at];
        if (next->kind == SmithTokenKind_Eof || next->foreign || next->length == 0) return false;
        if (smith_lang_punct(next, "(")) depth += 1;
        if (smith_lang_punct(next, ")") && --depth == 0) close = at;
    }
    for (size_t at = 1; at < close; at++) { // its text for the compiler: a module's names as the lowering writes them
        const SmithToken *next = &span[at];
        char text[smith_lang_name_max];
        size_t taken = 1;
        if (used + sizeof text + 8 > sizeof tokens) return false;
        if (smith_lang_punct(next, "[") && at + 2 < close && span[at + 1].kind == SmithTokenKind_Number &&
            smith_lang_punct(&span[at + 2], "]")) { // `[1]` is `._1` in the operand
            snprintf(text, sizeof text, "%s_%.*s", ".\x1f", (int)span[at + 1].length, span[at + 1].text);
            taken = 3;
        } else if (next->kind == SmithTokenKind_Identifier &&
                   smith_lang_word(next->text, next->length) == SmithLangWord_Other &&
                   !smith_lang_punct(&span[at - 1], ".") && !smith_lang_punct(&span[at - 1], "->") &&
                   !(lang->in_function && smith_lang_is_param(lang, next->text, next->length))) {
            taken = smith_lang_tuple_resolve(lang, span, at, close, text, sizeof text);
        } else {
            snprintf(text, sizeof text, "%.*s", (int)(next->length < sizeof text ? next->length : sizeof text - 1),
                     next->text);
        }
        used += (size_t)snprintf(tokens + used, sizeof tokens - used, "%s%s", used > 0 ? "\x1f" : "", text);
        at += taken - 1;
    }
    if (close < 2) return false;
    tokens[used] = 0;
    if (smith_lang_slice_countof(lang, host, token, tokens, span, close)) return true;
    char type[256];
    uint32_t fields = 0;
    if (!smith_lang_tuple_typed(host, tokens, type, sizeof type, &fields)) return false;
    for (size_t at = 0; at <= close; at++)
        smith_lang_take(lang, host);
    char digits[24];
    int length = snprintf(digits, sizeof digits, "%u", fields);
    SmithToken number =
        smith_lang_tuple_made(SmithTokenKind_Number, digits, (size_t)length, token, token->space_before);
    number.space_before = token->space_before;
    number.line_start = token->line_start;
    number.synthetic = false; // it stands for `countof(t)`
    smith_lang_emit(lang, host, number);
    smith_lang_track(lang, &number, false);
    return true;
}

// `return {1, 2.0f};` in a function that returns a tuple: the braces are a compound literal of the return type, which
// the compiler says (`(struct smith_tuple_i32__f32){1, 2.0f}`).
static void smith_lang_tuple_return(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (host->return_type == nullptr || lang->recording ||
        !smith_lang_punct(smith_lang_peek(lang, host, 0, false), "{")) {
        return;
    }
    char type[256];
    if (!host->return_type(host->context, type, sizeof type) || (strncmp(type, "struct smith_tuple_", 19) != 0 && strncmp(type, "struct smith_array_", 19) != 0) ||
        strchr(type, ' ') != type + 6 || strchr(type + 7, ' ') != nullptr) {
        return;
    }
    SmithToken made[4];
    made[0] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "(", 1, token, true);
    made[1] = smith_lang_tuple_made(SmithTokenKind_Identifier, "struct", 6, token, false);
    made[2] = smith_lang_tuple_made(SmithTokenKind_Identifier, type + 7, strlen(type + 7), token, true);
    made[3] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ")", 1, token, false);
    smith_lang_tuple_replace(lang, 0, made, 4);
}

// ---- The generated header

// The header's declarations hold each tuple's definition where it is used, in a parameter or a return type, which is
// valid C23 and not before; a C file that includes the header may be older. Each definition is cut out, leaving
// `struct smith_tuple_X`, and written before the declaration that used it, once and guarded, so that two headers that
// define the shape do not collide. The innermost definition goes first: a tuple of tuples is defined after the tuples
// it holds.

// The start of the top-level declaration that holds the byte at `before`: after the last `;`, the last `}` that ends
// a function's body, and the last preprocessor line before it. Comments and strings are skipped.
static size_t smith_lang_tuple_decl_start(const char *body, size_t before) {
    size_t start = 0;
    int depth = 0;
    bool function = false;
    bool line_start = true;
    for (size_t at = 0; at < before; at++) { // the text up to it
        char c = body[at];
        if (c == '\n') {
            line_start = true;
            continue;
        }
        if (c == ' ' || c == '\t') continue;
        bool first = line_start;
        line_start = false;
        if ((c == '#' && first) || (c == '/' && at + 1 < before && body[at + 1] == '/')) {
            while (at < before && body[at] != '\n')
                at += 1;
            line_start = true;
            if (c == '#' && depth == 0) start = at + 1;
        } else if (c == '/' && at + 1 < before && body[at + 1] == '*') {
            at += 2;
            while (at + 1 < before && !(body[at] == '*' && body[at + 1] == '/'))
                at += 1;
            at += 1;
        } else if (c == '"') {
            for (at += 1; at < before && body[at] != '"'; at++) {
                at += body[at] == '\\' ? 1 : 0;
            }
        } else if (c == '{') {
            if (depth == 0) {
                size_t back = at;
                while (back > 0 && (body[back - 1] == ' ' || body[back - 1] == '\n' || body[back - 1] == '\t'))
                    back -= 1;
                function = back > 0 && body[back - 1] == ')';
            }
            depth += 1;
        } else if (c == '}') {
            depth -= 1;
            if (depth == 0 && function) start = at + 1;
        } else if (c == ';' && depth == 0) {
            start = at + 1;
        }
    }
    return start;
}

// The first definition of a tuple in the text that holds no other: the index of its `struct` and of the byte after its
// `}`; `name` is where the tag ends. False when there is none.
static bool smith_lang_tuple_find(const char *body, size_t used, const char *tag_prefix, const char *guard,
                                  size_t *found, size_t *name, size_t *end) {
    char marker[48];
    snprintf(marker, sizeof marker, "struct %s", tag_prefix);
    size_t marker_size = strlen(marker) + 1;
    char define_marker[48];
    snprintf(define_marker, sizeof define_marker, "#define %s", guard);
    bool any = false;
    for (size_t at = 0; at + marker_size < used; at++) { // each use of a tuple's tag
        if (memcmp(body + at, marker, marker_size - 1) != 0) continue;
        size_t line = at;
        while (line > 0 && body[line - 1] != '\n')
            line -= 1;
        if (line == at) { // a definition that starts a line, below its `#define`, is hoisted already
            size_t above = line >= 2 ? line - 2 : 0;
            while (above > 0 && body[above - 1] != '\n')
                above -= 1;
            if (memcmp(body + above, define_marker, strlen(define_marker)) == 0) continue;
        }
        size_t after = at + marker_size - 1;
        while (after < used && smith_lang_word_char(body[after]))
            after += 1;
        size_t open = after;
        while (open < used && (body[open] == ' ' || body[open] == '\n'))
            open += 1;
        if (open >= used || body[open] != '{') continue;
        int depth = 0;
        size_t close = open;
        for (; close < used; close++) { // to the matching brace
            depth += body[close] == '{';
            depth -= body[close] == '}';
            if (depth == 0) break;
        }
        if (close >= used) continue;
        *found = at;
        *name = after;
        *end = close + 1;
        any = true;
        bool holds = false;
        for (size_t inner = open + 1; inner + marker_size < close; inner++) { // a tuple defined inside it
            if (memcmp(body + inner, marker, marker_size - 1) != 0) continue;
            size_t stop = inner + marker_size - 1;
            while (stop < close && smith_lang_word_char(body[stop]))
                stop += 1;
            while (stop < close && (body[stop] == ' ' || body[stop] == '\n'))
                stop += 1;
            holds = holds || body[stop] == '{';
        }
        if (!holds) return true;
    }
    return any;
}

static void smith_lang_tuple_hoist_kind(char *body, size_t *used, size_t capacity, const char *tag_prefix,
                                        const char *guard) {
    for (size_t pass = 0; pass < 1024; pass++) { // a definition is hoisted in each pass
        size_t found = 0;
        size_t name = 0;
        size_t end = 0;
        if (!smith_lang_tuple_find(body, *used, tag_prefix, guard, &found, &name, &end)) return;
        size_t tag_length = name - found - 7; // past `struct `
        char suffix[smith_lang_tuple_name_max];
        if (tag_length >= sizeof suffix) return;
        memcpy(suffix, body + found + 7 + strlen(tag_prefix), tag_length - strlen(tag_prefix));
        suffix[tag_length - strlen(tag_prefix)] = 0;
        char block[8192];
        int written = snprintf(block, sizeof block, "\n\n#ifndef %s%s\n#define %s%s\n%.*s;\n#endif",
                               guard, suffix, guard, suffix, (int)(end - found), body + found);
        if (written <= 0 || (size_t)written >= sizeof block) return;
        size_t start = smith_lang_tuple_decl_start(body, found);
        char define[smith_lang_tuple_name_max + 24];
        snprintf(define, sizeof define, "#define %s%s\n", guard, suffix);
        bool present = memmem(body, start, define, strlen(define)) != nullptr;
        size_t cut = end - found - (7 + tag_length);                     // the definition's text but for `struct tag`
        memmove(body + found + 7 + tag_length, body + end, *used - end); // the use keeps `struct tag`
        *used -= cut;
        if (start < *used && body[start] != '\n' && (size_t)written + 1 < sizeof block) block[written++] = '\n';
        if (present || *used + (size_t)written > capacity) continue;
        memmove(body + start + written, body + start, *used - start);
        memcpy(body + start, block, (size_t)written);
        *used += (size_t)written;
    }
}

static void smith_lang_tuple_hoist(char *body, size_t *used, size_t capacity) {
    smith_lang_tuple_hoist_kind(body, used, capacity, smith_lang_tuple_prefix, "SMITH_TUPLE_");
    smith_lang_tuple_hoist_kind(body, used, capacity, "smith_array_", "SMITH_ARRAY_");
}
