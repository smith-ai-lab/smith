// Run-time arrays of one dimension (issue #82): `T xs[:]` is a slice, a pointer and the number of elements that follow
// it. It is a struct the lowering writes where it is used, `struct smith_array_i32_n { int32_t *data; size_t count[1];
// }` (C23 accepts the same definition again, in any scope, so two files and two headers agree), and everything else is
// plain C over it:
//
//   xs[i]       *({ auto smith_s = (xs); size_t smith_i = (i); check(smith_i < smith_s.count[0]); &smith_s.data[smith_i]; })
//   xs[a:b]     ({ auto smith_s = (xs); ...; check(a <= b && b <= smith_s.count[0]); (struct smith_array_i32_n){...}; })
//   p[a:b]      the same over a pointer, whose extent is the program's word (the slice's `smith prove` obligation)
//   countof(xs) (xs).count[0]
//
// Because a subscript is read after the expression it subscripts has been written, the lowering reads a whole postfix
// chain (`a.items[i].rows[b:c]`) at its first token, from the queue, types each prefix with the compiler (`expr_type`)
// and writes the chain again. Included by runtime/lang.c, after runtime/lang_tuple.c.

constexpr size_t smith_lang_slice_chain_max = 200;
constexpr size_t smith_lang_slice_cur_max = 240;
constexpr size_t smith_lang_slice_elements_max = 24;
constexpr size_t smith_lang_slice_scan_max = 48;
constexpr size_t smith_lang_slice_text_max = 4096;
constexpr size_t smith_lang_slice_args_max = 16;
constexpr char smith_lang_slice_prefix[] = "smith_array_";


// Whether the text has a `[ ]` with a `:` that is neither `::` nor the `:` of a `? :` (the hook's hasSlice).
static bool smith_lang_text_has_slice(const char *text, size_t length) {
    for (size_t at = 0; at < length; at++) { // each `[`
        if (text[at] != '[') continue;
        int depth = 0;
        int conditions = 0;
        for (size_t scan = at; scan < length && text[scan] != ';'; scan++) { // to its `]`
            char c = text[scan];
            depth += c == '[' || c == '(' || c == '{';
            depth -= c == ']' || c == ')' || c == '}';
            if (depth <= 0) break;
            conditions += depth == 1 && c == '?';
            if (depth != 1 || c != ':') continue;
            if (scan + 1 < length && text[scan + 1] == ':') {
                scan += 1;
            } else if (conditions > 0) {
                conditions -= 1;
            } else {
                return true;
            }
        }
    }
    return false;
}

bool smith_lang_slices_on(const SmithLang *lang) {
    return lang->slices_seen;
}

static bool smith_lang_slice_active(const SmithLang *lang, const SmithHost *host) {
    return host->maybe_slice || lang->slices_seen;
}

// ---- Tokens from a pattern

typedef struct {
    const SmithToken *tokens;
    size_t count;
} SmithLangSliceSpan;

static SmithTokenKind smith_lang_slice_kind(const char *word) {
    unsigned char c = (unsigned char)word[0];
    if (c == '"') return SmithTokenKind_String;
    if (c >= '0' && c <= '9') return SmithTokenKind_Number;
    if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return SmithTokenKind_Identifier;
    return SmithTokenKind_Punctuator;
}

// The tokens of a pattern: words separated by one space, `@0` to `@9` for the tokens of a span. A made token is
// synthetic; the span's are as they were.
static size_t smith_lang_slice_build(SmithToken *out, size_t used, size_t capacity, const char *pattern,
                                     const SmithLangSliceSpan *spans, const SmithToken *like) {
    const char *at = pattern;
    bool open = true; // the word before was an opening bracket or `.`: no space after it
    while (*at != 0 && used + 1 < capacity) {
        while (*at == ' ')
            at += 1;
        const char *end = at;
        while (*end != 0 && *end != ' ')
            end += 1;
        size_t length = (size_t)(end - at);
        if (length == 0) break;
        if (at[0] == '@' && length == 2) {
            const SmithLangSliceSpan *span = &spans[at[1] - '0'];
            for (size_t i = 0; i < span->count && used + 1 < capacity; i++) // the span's tokens
                out[used++] = span->tokens[i];
            open = false;
        } else {
            bool closing = length == 1 && strchr(")],;.", at[0]) != nullptr;
            out[used++] = smith_lang_tuple_made(smith_lang_slice_kind(at), at, length, like, !open && !closing);
            open = length == 1 && strchr("([.", at[0]) != nullptr;
        }
        at = end;
    }
    return used;
}

// The tokens of a pattern (as smith_lang_slice_build), the unsigned type of a size spelled as the compiler does.
static size_t smith_lang_slice_pattern(SmithToken *out, size_t used, size_t capacity, const char *pattern,
                                       const SmithLangSliceSpan *spans, const SmithToken *like) {
    static unsigned counter;
    counter += 1;
    size_t from = used;
    used = smith_lang_slice_build(out, used, capacity, pattern, spans, like);
    for (size_t i = from; i < used; i++) { // each token made
        static const char *const temporaries[] = {"smith_s", "smith_i", "smith_a", "smith_b", "smith_p"};
        for (size_t t = 0; t < sizeof temporaries / sizeof temporaries[0]; t++) { // the names of the temporaries
            if (!out[i].synthetic || !smith_lang_equals(out[i].text, out[i].length, temporaries[t])) continue;
            char numbered[24];
            int length = snprintf(numbered, sizeof numbered, "%s%u", temporaries[t], counter);
            out[i] = smith_lang_tuple_made(SmithTokenKind_Identifier, numbered, (size_t)length, &out[i], out[i].space_before);
        }
        if (!out[i].synthetic || out[i].length != 13 || memcmp(out[i].text, "__SIZE_TYPE__", 13) != 0) continue;
        out[i].expand = true;
        out[i].header_text = "size_t";
        out[i].header_length = 6;
    }
    return used;
}

// ---- The name of a slice's struct

static void smith_lang_slice_tag(const char *spelled, char *tag, size_t capacity) {
    size_t length = strlen(spelled);
    if (length <= smith_lang_tuple_long) {
        snprintf(tag, capacity, "%s%s_n", smith_lang_slice_prefix, spelled);
        return;
    }
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < length; i++) { // FNV-1a
        hash = (hash ^ (uint8_t)spelled[i]) * 1099511628211ull;
    }
    snprintf(tag, capacity, "%s%.80s_h%016llx_n", smith_lang_slice_prefix, spelled, (unsigned long long)hash);
}

// The struct a slice of the element written as `elem` is, or false with the reason at `where`.
static bool smith_lang_slice_tag_of(SmithLang *lang, const SmithToken *elem, size_t count, char *tag, size_t capacity,
                                    const char **problem, uint32_t *where) {
    char spelled[smith_lang_tuple_name_max];
    if (count == 0) {
        *problem = "a slice needs its element type: `T xs[:]`";
        return false;
    }
    if (!smith_lang_tuple_spell(lang, elem, 0, count, spelled, sizeof spelled, problem, where)) return false;
    smith_lang_slice_tag(spelled, tag, capacity);
    return true;
}

// Whether the struct of this tag is defined at file scope by an earlier declaration: a later use names the tag only.
// (A second definition inside a function's head, after one at file scope, is read by the compiler but the token after
// the head is lost from the hook's stream; and a bare tag is shorter.)
static bool smith_lang_slice_defined(const SmithLang *lang, const char *tag) {
    for (uint32_t i = 0; i < lang->slice.tag_count; i++) { // at most 32
        if (strcmp(lang->slice.tags[i], tag) == 0) return true;
    }
    return false;
}

// The definition as a C header spells it, for a bare tag: the header of an export that names the struct holds it.
static const char *smith_lang_slice_header_text(SmithLang *lang, const char *tag, const SmithToken *elem, size_t count) {
    char text[smith_lang_tuple_name_max];
    size_t used = (size_t)snprintf(text, sizeof text, "%s {", tag);
    for (size_t at = 0; at < count && used + smith_lang_name_max + 8 < sizeof text; at++) { // the element's tokens
        const SmithToken *token = &elem[at];
        char word[smith_lang_name_max];
        size_t taken = 1;
        const SmithLangSized *sized = token->kind == SmithTokenKind_Identifier ? smith_lang_find_sized(token->text, token->length) : nullptr;
        if (sized != nullptr) {
            snprintf(word, sizeof word, "%s", sized->header);
        } else if (token->kind == SmithTokenKind_Identifier &&
                   smith_lang_word(token->text, token->length) == SmithLangWord_Other) {
            taken = smith_lang_tuple_resolve(lang, elem, at, count, word, sizeof word);
        } else {
            snprintf(word, sizeof word, "%.*s", (int)(token->length < sizeof word ? token->length : sizeof word - 1), token->text);
        }
        used += (size_t)snprintf(text + used, sizeof text - used, " %s", word);
        at += taken - 1;
    }
    snprintf(text + used, sizeof text - used, " * data; size_t count [ 1 ]; }");
    return smith_lang_tuple_keep(text, strlen(text));
}

// The struct a slice is, ready for the queue: `struct tag { T *data; usize count[1]; }`, or only `struct tag` when an
// earlier declaration defined it at file scope. The first use at file scope defines it.
static size_t smith_lang_slice_define(SmithLang *lang, SmithToken *out, size_t used, size_t capacity, const char *tag,
                                      const SmithToken *elem, size_t count, const SmithToken *like) {
    bool bare = smith_lang_slice_defined(lang, tag);
    char pattern[smith_lang_tuple_name_max + 80];
    snprintf(pattern, sizeof pattern, bare ? "struct %s" : "struct %s { @0 * data ; __SIZE_TYPE__ count [ 1 ] ; }", tag);
    SmithLangSliceSpan spans[1] = {{elem, count}};
    size_t first = used;
    used = smith_lang_slice_pattern(out, used, capacity, pattern, spans, like);
    if (first > 0) out[first].space_before = true; // after `typedef`, `static` and the like
    if (bare) {
        const char *text = smith_lang_slice_header_text(lang, tag, elem, count);
        out[used - 1].header_text = text;
        out[used - 1].header_length = (uint32_t)strlen(text);
        return used;
    }
    for (size_t i = first; i < used; i++) { // the braces of the definition, not those the element might hold
        if (out[i].synthetic && (smith_lang_punct(&out[i], "{") || smith_lang_punct(&out[i], "}")))
            out[i].tuple_brace = true;
    }
    if (!lang->in_function && lang->paren_depth == 0 && lang->slice.tag_count < 32) {
        snprintf(lang->slice.tags[lang->slice.tag_count++], sizeof lang->slice.tags[0], "%s", tag);
    }
    return used;
}

// ---- A declaration: `T xs[:]`

static bool smith_lang_slice_boundary(const SmithToken *token);

static bool smith_lang_slice_statement_word(const SmithToken *token) {
    static const char *const words[] = {"return", "sizeof", "case", "goto", "else", "do", "if", "while", "for",
                                        "switch", "default", "typeof", "alignof", "__typeof__", "defer"};
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) { // 15 words
        if (smith_lang_ident(token, words[i])) return true;
    }
    return false;
}

// The token `at` places into a declaration that begins with the one being lowered (0), then the queue.
static const SmithToken *smith_lang_slice_at(SmithLang *lang, const SmithHost *host, const SmithToken *first, size_t at) {
    return at == 0 ? first : smith_lang_peek(lang, host, at - 1, false);
}

// Whether the declaration that begins with `first` declares a slice: the types and stars of the element, the name and
// `[ : ]`. Gives the index of the name and of the `]`, counting `first` as 0.
static bool smith_lang_slice_declaration(SmithLang *lang, const SmithHost *host, const SmithToken *first, size_t *name,
                                         size_t *close) {
    if (first->kind != SmithTokenKind_Identifier || smith_lang_slice_statement_word(first)) return false;
    for (size_t at = 1; at < smith_lang_slice_scan_max; at++) { // a declaration's type is a few tokens
        const SmithToken *token = smith_lang_slice_at(lang, host, first, at);
        if (token->kind == SmithTokenKind_Eof || token->foreign) return false;
        if (at == 1 && smith_lang_punct(token, "::")) {
            const SmithLangBinding *named = lang->bindings_count > 0
                                                ? smith_lang_binding_find(lang, first->text, first->length)
                                                : nullptr;
            if (named != nullptr && named->is_c) return false; // the name after `c::` must reach the stream as written
        }
        if (token->kind == SmithTokenKind_Identifier || smith_lang_punct(token, "*") || smith_lang_punct(token, "::")) {
            continue;
        }
        if (!smith_lang_punct(token, "[")) return false;
        if (smith_lang_punct(smith_lang_slice_at(lang, host, first, at + 1), ":") &&
            smith_lang_punct(smith_lang_slice_at(lang, host, first, at + 2), "]") &&
            smith_lang_slice_at(lang, host, first, at - 1)->kind == SmithTokenKind_Identifier) {
            *name = at - 1;
            *close = at + 2;
            return true;
        }
        return false;
    }
    return false;
}

// How many leading tokens of the declaration are not part of the element: storage classes, `typedef` and attributes.
static size_t smith_lang_slice_leading(const SmithToken *span, size_t name) {
    size_t at = 0;
    while (at < name) { // each leading word
        if (span[at].kind == SmithTokenKind_Identifier) {
            SmithLangWord word = smith_lang_word(span[at].text, span[at].length);
            bool exported = smith_lang_equals(span[at].text, span[at].length, "export");
            if (word != SmithLangWord_Storage && word != SmithLangWord_Typedef && !exported) break;
            at += 1;
        } else if (smith_lang_punct(&span[at], "[")) {
            while (at < name && !smith_lang_punct(&span[at], "]"))
                at += 1;
            at += 2; // the two closing brackets
        } else {
            break;
        }
    }
    return at < name ? at : name;
}

static void smith_lang_slice_flag(SmithLang *lang) {
    lang->slices_seen = true;
    lang->tuples_seen = true;
}

// `[ : ]` was read after the name and more dimensions follow: they are not built yet. Returns the tokens of them, to be
// dropped so that the compiler has nothing more to say.
static size_t smith_lang_slice_more(SmithLang *lang, const SmithHost *host, size_t close) {
    size_t extra = 0;
    for (size_t groups = 0; groups < 4; groups++) { // each `[ ... ]` after the name
        const SmithToken *next = smith_lang_peek(lang, host, close + extra, false);
        if (!smith_lang_punct(next, "[")) break;
        if (groups == 0) {
            smith_lang_error(lang, host, next->location,
                             "a run-time array of more than one dimension is not built yet; a `[:]` of `[:]` has rows "
                             "of different lengths, so hold the rows in a struct");
        }
        for (size_t guard = 0; guard < 32; guard++) { // to its `]`
            extra += 1;
            if (smith_lang_punct(smith_lang_peek(lang, host, close + extra - 1, false), "]")) break;
        }
    }
    return extra;
}

// At the first token of a declaration (after `;`, `{`, `}`, `(`, `,` or `:`): if it declares a slice, its tokens up to
// `]` become the struct, defined where it is written, and the name. True when the token was replaced.
static bool smith_lang_slice_begin(SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    size_t name = 0;
    size_t close = 0;
    if (!smith_lang_slice_declaration(lang, host, first, &name, &close)) return false;
    static SmithToken span[smith_lang_slice_scan_max + 4];
    for (size_t at = 0; at <= close; at++) // up to the `]`
        span[at] = *smith_lang_slice_at(lang, host, first, at);
    size_t leading = smith_lang_slice_leading(span, name);
    char tag[smith_lang_tuple_name_max];
    const char *problem = nullptr;
    uint32_t where = span[name].location;
    static SmithToken made[smith_lang_slice_scan_max + 40];
    size_t used = 0;
    for (size_t at = 0; at < leading; at++)
        made[used++] = span[at];
    bool good = smith_lang_slice_tag_of(lang, span + leading, name - leading, tag, sizeof tag, &problem, &where);
    if (good) {
        used = smith_lang_slice_define(lang, made, used, sizeof made / sizeof made[0], tag, span + leading,
                                       name - leading, &span[leading]);
    } else {
        smith_lang_error(lang, host, where, "%s", problem);
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, "int", 3, &span[leading], true);
    }
    made[used++] = span[name];
    size_t extra = smith_lang_slice_more(lang, host, close);
    if (!smith_lang_tuple_replace(lang, close + extra, made, used)) return false;
    lang->slice.skip = span[name].location; // the name just declared starts no chain
    smith_lang_slice_flag(lang);
    snprintf(lang->slice.armed, sizeof lang->slice.armed, "%.*s", (int)span[name].length, span[name].text);
    snprintf(lang->slice.armed_tag, sizeof lang->slice.armed_tag, "%s", good ? tag : "");
    return true;
}

// `typeof(T[:])`: a slice's struct as a type, the result of a function or the type of a local.
static bool smith_lang_slice_typeof(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(")) return false;
    size_t close = 0;
    for (size_t at = 1; at < smith_lang_slice_scan_max && close == 0; at++) { // the type is a few tokens
        const SmithToken *next = smith_lang_peek(lang, host, at, false);
        if (next->kind == SmithTokenKind_Eof || next->foreign) return false;
        if (smith_lang_punct(next, "[") && smith_lang_punct(smith_lang_peek(lang, host, at + 1, false), ":") &&
            smith_lang_punct(smith_lang_peek(lang, host, at + 2, false), "]") &&
            smith_lang_punct(smith_lang_peek(lang, host, at + 3, false), ")")) {
            close = at;
        } else if (!(next->kind == SmithTokenKind_Identifier || smith_lang_punct(next, "*") ||
                     smith_lang_punct(next, "::"))) {
            return false;
        }
    }
    if (close < 2) return false;
    static SmithToken span[smith_lang_slice_scan_max + 4];
    for (size_t at = 0; at <= close + 3; at++) // `(` to `)`
        span[at] = *smith_lang_peek(lang, host, at, false);
    char tag[smith_lang_tuple_name_max];
    const char *problem = nullptr;
    uint32_t where = span[1].location;
    static SmithToken made[smith_lang_slice_scan_max + 40];
    if (!smith_lang_slice_tag_of(lang, span + 1, close - 1, tag, sizeof tag, &problem, &where)) {
        smith_lang_error(lang, host, where, "%s", problem);
        return false;
    }
    size_t used = smith_lang_slice_define(lang, made, 0, sizeof made / sizeof made[0], tag, span + 1, close - 1, token);
    size_t extra = smith_lang_slice_more(lang, host, close + 4);
    made[0].space_before = token->space_before;
    made[0].line_start = token->line_start;
    if (!smith_lang_tuple_replace(lang, close + 4 + extra, made, used)) return false;
    smith_lang_slice_flag(lang);
    return true; // the `typeof` itself is dropped
}

// Whether a token begins a declaration or ends one: where a slice's type may start.
static bool smith_lang_slice_boundary(const SmithToken *token) {
    return token->kind == SmithTokenKind_Punctuator && token->length == 1 && strchr(";{}(,:", token->text[0]) != nullptr;
}

// ---- What the compiler says of an expression

typedef enum { SmithLangSlice_None, SmithLangSlice_Slice, SmithLangSlice_Pointer, SmithLangSlice_Array } SmithLangSliceKind;

typedef struct {
    SmithLangSliceKind kind;
    char tag[smith_lang_tuple_name_max]; // a slice: its struct
    char element[256];                   // a pointer or an array: the element as the parser writes it
    uint64_t count;                      // an array: how many
} SmithLangSliceType;

// The element of `int32_t *` or `float[4]`: what stands before the last `*` or `[`.
static bool smith_lang_slice_element(const char *written, const char *canonical, char *out, size_t capacity) {
    const char *source = written[0] != 0 ? written : canonical;
    snprintf(out, capacity, "%s", source);
    char *cut = strrchr(out, '*');
    char *bracket = strrchr(out, '[');
    if (strchr(out, '(') != nullptr) return false; // a function pointer
    if (cut != nullptr && (bracket == nullptr || cut > bracket)) {
        *cut = 0;
    } else if (bracket != nullptr) {
        *bracket = 0;
    }
    size_t length = strlen(out);
    while (length > 0 && out[length - 1] == ' ')
        out[--length] = 0;
    return length > 0;
}

static bool smith_lang_slice_type(const SmithHost *host, const char *text, SmithLangSliceType *out) {
    *out = (SmithLangSliceType){};
    SmithExprType found = {};
    if (host->expr_type == nullptr || !host->expr_type(host->context, text, (uint32_t)strlen(text), &found)) {
        return false;
    }
    const char *canonical = found.canonical;
    const char *tag = strstr(canonical, "struct smith_array_");
    size_t length = strlen(canonical);
    while (length > 0 && canonical[length - 1] == ' ')
        length -= 1;
    if (tag != nullptr && strchr(canonical, '*') == nullptr && strchr(canonical, '[') == nullptr &&
        strchr(canonical, '(') == nullptr) {
        tag += 7;
        snprintf(out->tag, sizeof out->tag, "%.*s", (int)strcspn(tag, " "), tag);
        out->kind = SmithLangSlice_Slice;
        return true;
    }
    if (length > 0 && canonical[length - 1] == '*' && strchr(canonical, '(') == nullptr) {
        out->kind = found.is_array ? SmithLangSlice_Array : SmithLangSlice_Pointer;
        out->count = found.array_size;
    } else {
        return true;
    }
    if (!smith_lang_slice_element(found.written, canonical, out->element, sizeof out->element)) {
        out->kind = SmithLangSlice_None;
    }
    return true;
}

// The tokens of a C type written as the parser does (`const char`, `struct Foo`): words and stars.
static size_t smith_lang_slice_words(SmithToken *out, size_t capacity, const char *type, const SmithToken *like) {
    size_t used = 0;
    const char *at = type;
    while (*at != 0 && used < capacity) {
        while (*at == ' ')
            at += 1;
        const char *end = at;
        if (*at == '*') {
            end += 1;
        } else {
            while (*end != 0 && *end != ' ' && *end != '*')
                end += 1;
        }
        if (end == at) break;
        out[used++] = smith_lang_tuple_made(smith_lang_slice_kind(at), at, (size_t)(end - at), like, true);
        at = end;
    }
    if (used > 0) out[0].space_before = false;
    return used;
}

// ---- The text of tokens for the compiler

// The tokens as one expression for `expr_type`: a module's names as the lowering writes them, the tokens it made as
// they are.
static bool smith_lang_slice_text(SmithLang *lang, const SmithToken *tokens, size_t count, char *out, size_t capacity) {
    size_t used = 0;
    for (size_t at = 0; at < count; at++) { // each token
        const SmithToken *next = &tokens[at];
        char text[smith_lang_name_max];
        size_t taken = 1;
        if (used + sizeof text + 8 > capacity) return false;
        bool member = at > 0 && (smith_lang_punct(&tokens[at - 1], ".") || smith_lang_punct(&tokens[at - 1], "->"));
        if (next->kind == SmithTokenKind_Identifier && !next->synthetic && !member &&
            smith_lang_word(next->text, next->length) == SmithLangWord_Other &&
            !(lang->in_function && smith_lang_is_param(lang, next->text, next->length))) {
            taken = smith_lang_tuple_resolve(lang, tokens, at, count, text, sizeof text);
        } else {
            snprintf(text, sizeof text, "%.*s", (int)(next->length < sizeof text ? next->length : sizeof text - 1),
                     next->text);
        }
        used += (size_t)snprintf(out + used, capacity - used, "%s%s", used > 0 ? "\x1f" : "", text);
        at += taken - 1;
    }
    return count > 0;
}

// ---- The check an index or a slice makes

static bool smith_lang_slice_assertions(const SmithHost *host) {
    int64_t value = 1;
    if (host->target_fact != nullptr && host->target_fact(host->context, SmithTarget_Assertions, &value)) {
        return value != 0;
    }
    return true;
}

// The statement that fails the program unless `condition` holds (nothing where assertions are off): smith_failure
// (<smith/trace.h>) with the kind of an assert, 4, as `contract::assert` fails, so the report is the same and its line is
// the line of the index. The function is declared here, so that the file needs no import; the build links it with the
// trace library.
static void smith_lang_slice_check(const SmithHost *host, const char *condition, char *out, size_t capacity) {
    if (!smith_lang_slice_assertions(host)) {
        out[0] = 0;
        return;
    }
    snprintf(out, capacity, "extern void smith_failure ( unsigned char ) ; if ( ! ( %s ) ) smith_failure ( 4 ) ;", condition);
}


// ---- A postfix chain

typedef enum {
    SmithLangSliceElement_Member,
    SmithLangSliceElement_Call,
    SmithLangSliceElement_Subscript
} SmithLangSliceElementKind;

typedef struct {
    SmithLangSliceElementKind kind;
    size_t from; // [from, to) in the chain: `.name`, `( ... )` or `[ ... ]`
    size_t to;
} SmithLangSliceElement;

typedef struct {
    SmithToken tokens[smith_lang_slice_chain_max];
    size_t count;
    SmithLangSliceElement elements[smith_lang_slice_elements_max];
    size_t element_count;
    size_t head_end; // the tokens of the name (or the parenthesized head)
    bool assigned;   // an `=` follows the chain
    bool c_head;     // the chain is a name written `c::name`
    bool subscripts;
    bool calls;
} SmithLangSliceChain;

// What the lowering made of a chain, being written.
typedef struct {
    SmithToken tokens[smith_lang_slice_cur_max];
    size_t count;
    bool changed;
    bool overflow;
} SmithLangSliceCur;

static SmithLangSliceChain smith_lang_slice_chain_state;
static SmithLangSliceCur smith_lang_slice_cur;

static void smith_lang_slice_add(SmithLangSliceCur *cur, const SmithToken *tokens, size_t count) {
    if (cur->count + count > smith_lang_slice_cur_max) {
        cur->overflow = true;
        return;
    }
    for (size_t i = 0; i < count; i++) // the tokens
        cur->tokens[cur->count++] = tokens[i];
}

// The tokens from the opening bracket at `*at` of the queue to its match, into the chain.
static bool smith_lang_slice_group(SmithLang *lang, const SmithHost *host, SmithLangSliceChain *chain, size_t *at,
                                   int depth) {
    for (size_t guard = 0; guard < smith_lang_slice_chain_max; guard++) { // a chain is a few tokens
        const SmithToken *next = smith_lang_peek(lang, host, *at, false);
        if (next->kind == SmithTokenKind_Eof || next->foreign || chain->count >= smith_lang_slice_chain_max) return false;
        if (smith_lang_punct(next, "(") || smith_lang_punct(next, "[") || smith_lang_punct(next, "{")) depth += 1;
        if (smith_lang_punct(next, ")") || smith_lang_punct(next, "]") || smith_lang_punct(next, "}")) depth -= 1;
        chain->tokens[chain->count++] = *next;
        *at += 1;
        if (depth == 0) return true;
    }
    return false;
}

static bool smith_lang_slice_more_element(SmithLang *lang, const SmithHost *host, SmithLangSliceChain *chain,
                                          size_t *at) {
    const SmithToken *next = smith_lang_peek(lang, host, *at, false);
    if (chain->element_count >= smith_lang_slice_elements_max) return false;
    SmithLangSliceElement *element = &chain->elements[chain->element_count];
    element->from = chain->count;
    if (smith_lang_punct(next, ".") || smith_lang_punct(next, "->")) {
        const SmithToken *name = smith_lang_peek(lang, host, *at + 1, false);
        if (name->kind != SmithTokenKind_Identifier || name->foreign || chain->count + 2 > smith_lang_slice_chain_max) {
            return false;
        }
        chain->tokens[chain->count++] = *next;
        chain->tokens[chain->count++] = *name;
        *at += 2;
        element->kind = SmithLangSliceElement_Member;
    } else if (smith_lang_punct(next, "[") || smith_lang_punct(next, "(")) {
        bool subscript = smith_lang_punct(next, "[");
        if (subscript && smith_lang_punct(smith_lang_peek(lang, host, *at + 1, false), "[")) return false; // [[
        if (!smith_lang_slice_group(lang, host, chain, at, 0)) return false;
        element->kind = subscript ? SmithLangSliceElement_Subscript : SmithLangSliceElement_Call;
        chain->subscripts = chain->subscripts || subscript;
        chain->calls = chain->calls || (!subscript && chain->count - element->from > 2);
    } else {
        return false;
    }
    element->to = chain->count;
    chain->element_count += 1;
    return true;
}

// The chain that begins with `token`: a name (`a::b`) or a parenthesized expression, then members, calls and
// subscripts. False when it is not one, or too long.
static bool smith_lang_slice_read(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                  SmithLangSliceChain *chain) {
    chain->tokens[0] = *token;
    chain->count = 1;
    chain->element_count = 0;
    chain->subscripts = chain->calls = chain->c_head = false;
    size_t at = 0;
    if (smith_lang_punct(token, "(")) {
        if (!smith_lang_slice_group(lang, host, chain, &at, 1)) return false;
        const SmithToken *after = smith_lang_peek(lang, host, at, false);
        if (!smith_lang_punct(after, ".") && !smith_lang_punct(after, "->") && !smith_lang_punct(after, "[")) {
            return false;
        }
    } else {
        const SmithLangBinding *named = lang->bindings_count > 0 ? smith_lang_binding_find(lang, token->text, token->length)
                                                                 : nullptr;
        if (named != nullptr && named->is_c && smith_lang_punct(smith_lang_peek(lang, host, 0, false), "::")) {
            smith_lang_peek(lang, host, 1, true); // a macro after `c::` reaches the stream as written
        }
        while (smith_lang_punct(smith_lang_peek(lang, host, at, false), "::") &&
               smith_lang_peek(lang, host, at + 1, false)->kind == SmithTokenKind_Identifier &&
               chain->count + 2 <= smith_lang_slice_chain_max) { // a qualified name
            chain->tokens[chain->count++] = *smith_lang_peek(lang, host, at, false);
            chain->tokens[chain->count++] = *smith_lang_peek(lang, host, at + 1, false);
            at += 2;
        }
        const SmithLangBinding *binding = chain->count == 3 && lang->bindings_count > 0
                                              ? smith_lang_binding_find(lang, token->text, token->length)
                                              : nullptr;
        chain->c_head = binding != nullptr && binding->is_c;
    }
    chain->head_end = chain->count;
    while (smith_lang_slice_more_element(lang, host, chain, &at)) { // each member, call and subscript
    }
    chain->assigned = smith_lang_punct(smith_lang_peek(lang, host, at, false), "=");
    return true;
}

// Whether the token before the one being lowered can end an operand: then this token continues an expression.
static bool smith_lang_slice_continues(SmithLang *lang, const SmithHost *host) {
    SmithToken before;
    if (!smith_lang_tuple_previous(lang, host, 1, &before)) return false;
    if (before.kind == SmithTokenKind_Punctuator) {
        return before.length == 0 || smith_lang_punct(&before, ")") || smith_lang_punct(&before, "]") ||
               smith_lang_punct(&before, ".") || smith_lang_punct(&before, "->") || smith_lang_punct(&before, "::");
    }
    if (before.kind != SmithTokenKind_Identifier) return true; // a literal
    return smith_lang_word(before.text, before.length) != SmithLangWord_Keyword;
}

// ---- A subscript of a slice

static size_t smith_lang_slice_index(SmithLangSliceCur *cur, const SmithHost *host, const SmithLangSliceSpan *spans,
                                     const SmithToken *like) {
    char check[512];
    char pattern[1024];
    smith_lang_slice_check(host, "smith_i < smith_s . count [ 0 ]", check, sizeof check);
    snprintf(pattern, sizeof pattern,
             "( * __extension__ ( { auto smith_s = ( @0 ) ; __SIZE_TYPE__ smith_i = ( @1 ) ; %s & smith_s . data [ smith_i ] ; } ) )",
             check);
    cur->count = smith_lang_slice_pattern(cur->tokens, 0, smith_lang_slice_cur_max, pattern, spans, like);
    return cur->count;
}

// A slice of a slice: a view that shares the elements.
static size_t smith_lang_slice_view(SmithLangSliceCur *cur, const SmithHost *host, const SmithLangSliceSpan *spans,
                                    const char *tag, const SmithToken *like) {
    char check[512];
    char pattern[1536];
    smith_lang_slice_check(host, "smith_a <= smith_b && smith_b <= smith_s . count [ 0 ]", check, sizeof check);
    snprintf(pattern, sizeof pattern,
             "__extension__ ( { auto smith_s = ( @0 ) ; __SIZE_TYPE__ smith_a = ( @1 ) ; __SIZE_TYPE__ smith_b = ( @2 ) ; %s "
             "( struct %s ) { . data = smith_a == 0 ? smith_s . data : smith_s . data + smith_a , . count = { smith_b - "
             "smith_a } } ; } )",
             check, tag);
    cur->count = smith_lang_slice_pattern(cur->tokens, 0, smith_lang_slice_cur_max, pattern, spans, like);
    return cur->count;
}

// A slice of a pointer or of a fixed array: the extent is the program's word for a pointer, the array's size for an
// array.
static bool smith_lang_slice_from_pointer(SmithLang *lang, SmithLangSliceCur *cur, const SmithHost *host,
                                          const SmithLangSliceSpan *spans, const SmithLangSliceType *type,
                                          const SmithToken *like, const char **problem, uint32_t *where) {
    static SmithToken element[48];
    size_t words = smith_lang_slice_words(element, sizeof element / sizeof element[0], type->element, like);
    char tag[smith_lang_tuple_name_max];
    if (!smith_lang_slice_tag_of(lang, element, words, tag, sizeof tag, problem, where)) return false;
    SmithLangSliceSpan all[4] = {spans[0], spans[1], spans[2], {element, words}};
    char condition[160] = "smith_a <= smith_b";
    if (type->kind == SmithLangSlice_Array) {
        snprintf(condition, sizeof condition, "smith_a <= smith_b && smith_b <= %llu", (unsigned long long)type->count);
    }
    char check[512];
    char pattern[1536];
    smith_lang_slice_check(host, condition, check, sizeof check);
    snprintf(pattern, sizeof pattern,
             "__extension__ ( { auto smith_p = ( @0 ) ; __SIZE_TYPE__ smith_a = ( @1 ) ; __SIZE_TYPE__ smith_b = ( @2 ) ; %s struct %s { "
             "@3 * data ; __SIZE_TYPE__ count [ 1 ] ; } ; ( struct %s ) { . data = smith_a == 0 ? smith_p : smith_p + smith_a , . "
             "count = { smith_b - smith_a } } ; } )",
             check, tag, tag);
    cur->count = smith_lang_slice_pattern(cur->tokens, 0, smith_lang_slice_cur_max, pattern, all, like);
    return true;
}

// ---- Reading a subscript: `[i]`, `[a:b]`, `[:b]`, `[a:]`

typedef struct {
    size_t colon;  // index of the slice's `:` in the inner tokens, or SIZE_MAX for an index
    size_t second; // a second `:` (a step), or SIZE_MAX
} SmithLangSliceShape;

static SmithLangSliceShape smith_lang_slice_shape(const SmithToken *inner, size_t count) {
    SmithLangSliceShape shape = {.colon = SIZE_MAX, .second = SIZE_MAX};
    int depth = 0;
    int conditions = 0;
    for (size_t at = 0; at < count; at++) { // each inner token
        const SmithToken *token = &inner[at];
        if (smith_lang_punct(token, "(") || smith_lang_punct(token, "[") || smith_lang_punct(token, "{")) depth += 1;
        if (smith_lang_punct(token, ")") || smith_lang_punct(token, "]") || smith_lang_punct(token, "}")) depth -= 1;
        if (depth != 0) continue;
        if (smith_lang_punct(token, "?")) conditions += 1;
        if (!smith_lang_punct(token, ":")) continue;
        if (conditions > 0) {
            conditions -= 1;
        } else if (shape.colon == SIZE_MAX) {
            shape.colon = at;
        } else if (shape.second == SIZE_MAX) {
            shape.second = at;
        }
    }
    return shape;
}

static SmithToken smith_lang_slice_number(const char *text, const SmithToken *like) {
    return smith_lang_tuple_made(SmithTokenKind_Number, text, strlen(text), like, false);
}

// A constant index below zero is an error: the last element is `xs[countof(xs) - 1]`.
static void smith_lang_slice_negative(SmithLang *lang, const SmithHost *host, const SmithToken *inner, size_t count) {
    char text[smith_lang_slice_text_max];
    SmithExprType found = {};
    if (count == 0 || host->expr_type == nullptr || !smith_lang_slice_text(lang, inner, count, text, sizeof text) ||
        !host->expr_type(host->context, text, (uint32_t)strlen(text), &found)) {
        return;
    }
    if (found.constant && !found.is_float && found.negative && found.magnitude > 0) {
        smith_lang_error(lang, host, inner[0].location,
                         "an index is never negative: the last element is `xs[countof(xs) - 1]`");
    }
}

// One subscript of the chain: `cur` holds what is before it, and holds the whole afterwards.
static void smith_lang_slice_subscript_element(SmithLang *lang, const SmithHost *host, const SmithLangSliceChain *chain,
                                               const SmithLangSliceElement *element, SmithLangSliceCur *cur) {
    const SmithToken *inner = &chain->tokens[element->from + 1];
    size_t count = element->to - element->from - 2;
    SmithLangSliceShape shape = smith_lang_slice_shape(inner, count);
    char text[smith_lang_slice_text_max];
    SmithLangSliceType type = {};
    bool typed = smith_lang_slice_text(lang, cur->tokens, cur->count, text, sizeof text) &&
                 smith_lang_slice_type(host, text, &type);
    const SmithToken *like = &chain->tokens[element->from];
    bool known = typed && (type.kind == SmithLangSlice_Slice ||
                           (shape.colon != SIZE_MAX && (type.kind == SmithLangSlice_Pointer || type.kind == SmithLangSlice_Array)));
    if (!known) {
        if (shape.colon != SIZE_MAX) {
            smith_lang_error(lang, host, like->location,
                             "cannot slice this expression: it is not a slice, a pointer or an array");
            SmithToken zero_index[4];
            SmithLangSliceSpan none[1] = {{nullptr, 0}};
            size_t made = smith_lang_slice_build(zero_index, 0, 4, "[ 0 ]", none, like);
            smith_lang_slice_add(cur, zero_index, made); // so that the compiler has nothing more to say of it
            cur->changed = true;
            return;
        }
        smith_lang_slice_add(cur, &chain->tokens[element->from], element->to - element->from);
        return;
    }
    if (shape.second != SIZE_MAX) {
        smith_lang_error(lang, host, like->location, "a slice has no step: `xs[a:b]` takes the elements a to b - 1");
    }
    static SmithLangSliceCur out;
    out = (SmithLangSliceCur){};
    SmithToken zero = smith_lang_slice_number("0", like);
    SmithToken whole[8];
    SmithLangSliceSpan whole_span = {whole, smith_lang_slice_build(whole, 0, 8, "smith_s . count [ 0 ]", nullptr, like)};
    SmithLangSliceSpan prefix = {cur->tokens, cur->count};
    if (shape.colon == SIZE_MAX) {
        smith_lang_slice_negative(lang, host, inner, count);
        SmithLangSliceSpan spans[2] = {prefix, {inner, count}};
        smith_lang_slice_index(&out, host, spans, like);
    } else {
        SmithLangSliceSpan low = shape.colon == 0 ? (SmithLangSliceSpan){&zero, 1}
                                                  : (SmithLangSliceSpan){inner, shape.colon};
        size_t stop = shape.second == SIZE_MAX ? count : shape.second;
        SmithLangSliceSpan high = shape.colon + 1 == stop ? whole_span
                                                          : (SmithLangSliceSpan){inner + shape.colon + 1, stop - shape.colon - 1};
        SmithLangSliceSpan spans[3] = {prefix, low, high};
        if (type.kind == SmithLangSlice_Slice) {
            smith_lang_slice_view(&out, host, spans, type.tag, like);
        } else {
            const char *problem = nullptr;
            uint32_t where = like->location;
            if (shape.colon + 1 == stop) {
                smith_lang_error(lang, host, like->location, "slicing a pointer or an array needs both bounds: `p[a:b]`");
                spans[2] = (SmithLangSliceSpan){&zero, 1};
            }
            if (!smith_lang_slice_from_pointer(lang, &out, host, spans, &type, like, &problem, &where)) {
                smith_lang_error(lang, host, where, "%s", problem);
                return;
            }
        }
    }
    *cur = out;
    cur->changed = true;
}

// ---- Arrays become slices, and slices become C pointers

// The parameters of a function type as the compiler prints it (`int (struct smith_array_i32_n, unsigned long)`): the
// tag of each that is a slice, else an empty string.
static size_t smith_lang_slice_params(const char *canonical, char tags[][smith_lang_tuple_name_max], size_t capacity) {
    size_t length = strlen(canonical);
    if (length == 0 || canonical[length - 1] != ')') return 0;
    size_t open = length - 1;
    int depth = 0;
    for (; open > 0; open--) { // back to the matching parenthesis
        depth += canonical[open] == ')';
        depth -= canonical[open] == '(';
        if (depth == 0) break;
    }
    size_t count = 0;
    size_t start = open + 1;
    depth = 0;
    for (size_t at = start; at < length && count < capacity; at++) { // each parameter
        char c = canonical[at];
        depth += c == '(' || c == '[';
        depth -= c == ')' || c == ']';
        if ((c == ',' && depth == 0) || at == length - 1) {
            const char *param = canonical + start;
            while (*param == ' ')
                param += 1;
            tags[count][0] = 0;
            if (strncmp(param, "struct smith_array_", 19) == 0) {
                snprintf(tags[count], smith_lang_tuple_name_max, "%.*s", (int)strcspn(param + 7, ",) "), param + 7);
            }
            count += 1;
            start = at + 1;
        }
    }
    return count;
}

// `( struct tag ) { . data = ( expr ) , . count = { n } }`
static void smith_lang_slice_wrap(SmithLangSliceCur *cur, const char *tag, const SmithToken *expr, size_t count,
                                  unsigned long long n, const SmithToken *like) {
    char pattern[smith_lang_tuple_name_max + 100];
    snprintf(pattern, sizeof pattern, "( struct %s ) { . data = ( @0 ) , . count = { %llu } }", tag, n);
    SmithLangSliceSpan spans[1] = {{expr, count}};
    cur->count = smith_lang_slice_pattern(cur->tokens, cur->count, smith_lang_slice_cur_max, pattern, spans, like);
}

static bool smith_lang_slice_is_string(const SmithToken *expr, size_t count) {
    for (size_t i = 0; i < count; i++) { // each token
        if (expr[i].kind != SmithTokenKind_String) return false;
    }
    return count > 0;
}

// An expression where a slice of `tag` is wanted: an array (or a string) is its elements, a slice is as it is. Returns
// false for a pointer, which is an error.
static bool smith_lang_slice_convert(SmithLang *lang, const SmithHost *host, const char *tag, const SmithToken *expr,
                                     size_t count, SmithLangSliceCur *cur) {
    char text[smith_lang_slice_text_max];
    SmithLangSliceType type = {};
    if (count == 0 || !smith_lang_slice_text(lang, expr, count, text, sizeof text) ||
        !smith_lang_slice_type(host, text, &type)) {
        smith_lang_slice_add(cur, expr, count);
        return true;
    }
    if (type.kind == SmithLangSlice_Array) {
        unsigned long long n = type.count - (smith_lang_slice_is_string(expr, count) && type.count > 0 ? 1 : 0);
        smith_lang_slice_wrap(cur, tag, expr, count, n, expr);
        cur->changed = true;
        return true;
    }
    if (type.kind == SmithLangSlice_Pointer) {
        smith_lang_error(lang, host, expr[0].location,
                         "a pointer is no slice: say how many elements it points at, `p[0:n]`");
    }
    smith_lang_slice_add(cur, expr, count);
    return true;
}

// The arguments of a call, converted: an array where the function wants a slice, and, for a C function, a slice
// where it wants a pointer (`xs.data`; its length is `countof(xs)`).
static void smith_lang_slice_call(SmithLang *lang, const SmithHost *host, const SmithLangSliceChain *chain,
                                  const SmithLangSliceElement *element, SmithLangSliceCur *cur) {
    static char tags[smith_lang_slice_args_max][smith_lang_tuple_name_max];
    char text[smith_lang_slice_text_max];
    SmithExprType found = {};
    size_t params = 0;
    if (smith_lang_slice_text(lang, cur->tokens, cur->count, text, sizeof text) && host->expr_type != nullptr &&
        host->expr_type(host->context, text, (uint32_t)strlen(text), &found)) {
        params = smith_lang_slice_params(found.canonical, tags, smith_lang_slice_args_max);
    }
    smith_lang_slice_add(cur, &chain->tokens[element->from], 1); // `(`
    size_t start = element->from + 1;
    size_t end = element->to - 1;
    int depth = 0;
    size_t argument = 0;
    for (size_t at = start; at <= end; at++) { // each argument, split at the commas outside brackets
        const SmithToken *token = &chain->tokens[at];
        if (at < end) {
            depth += smith_lang_punct(token, "(") || smith_lang_punct(token, "[") || smith_lang_punct(token, "{");
            depth -= smith_lang_punct(token, ")") || smith_lang_punct(token, "]") || smith_lang_punct(token, "}");
            if (!(depth == 0 && smith_lang_punct(token, ","))) continue;
        }
        size_t from = start;
        if (argument < params && tags[argument][0] != 0) {
            smith_lang_slice_convert(lang, host, tags[argument], &chain->tokens[from], at - from, cur);
        } else if (chain->c_head && at > from) {
            char arg[smith_lang_slice_text_max];
            SmithLangSliceType type = {};
            if (smith_lang_slice_text(lang, &chain->tokens[from], at - from, arg, sizeof arg) &&
                smith_lang_slice_type(host, arg, &type) && type.kind == SmithLangSlice_Slice) {
                SmithLangSliceSpan spans[1] = {{&chain->tokens[from], at - from}};
                cur->count = smith_lang_slice_pattern(cur->tokens, cur->count, smith_lang_slice_cur_max, "( @0 ) . data", spans, token);
                cur->changed = true;
            } else {
                smith_lang_slice_add(cur, &chain->tokens[from], at - from);
            }
        } else {
            smith_lang_slice_add(cur, &chain->tokens[from], at - from);
        }
        if (at < end) smith_lang_slice_add(cur, token, 1); // the comma
        start = at + 1;
        argument += 1;
    }
    smith_lang_slice_add(cur, &chain->tokens[element->to - 1], 1); // `)`
}

// ---- The chain, written again

static bool smith_lang_slice_eligible(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (token->foreign || token->synthetic || lang->recording || lang->in_attribute || host->expr_type == nullptr ||
        token->location == lang->slice.skip) {
        return false;
    }
    if (smith_lang_punct(token, "(")) return !smith_lang_slice_continues(lang, host);
    if (token->kind != SmithTokenKind_Identifier || smith_lang_word(token->text, token->length) != SmithLangWord_Other) {
        return false;
    }
    SmithToken before = {};
    if (smith_lang_tuple_previous(lang, host, 1, &before) && before.kind == SmithTokenKind_Identifier) {
        SmithLangWord word = smith_lang_word(before.text, before.length);
        if (word != SmithLangWord_Keyword) return false; // a declarator after its type
    }
    return !(before.kind == SmithTokenKind_Punctuator &&
             (smith_lang_punct(&before, ".") || smith_lang_punct(&before, "->") || smith_lang_punct(&before, "::")));
}

// The expression after an `=` up to the `;` or `,` that ends it, from the queue at `from`.
static size_t smith_lang_slice_right(SmithLang *lang, const SmithHost *host, size_t from, SmithToken *out, size_t capacity) {
    int depth = 0;
    size_t count = 0;
    for (; from + count < smith_lang_queue_max - 4 && count < capacity; count++) { // an initializer is a few tokens
        const SmithToken *token = smith_lang_peek(lang, host, from + count, false);
        if (token->kind == SmithTokenKind_Eof || token->foreign) return 0;
        depth += smith_lang_punct(token, "(") || smith_lang_punct(token, "[") || smith_lang_punct(token, "{");
        depth -= smith_lang_punct(token, ")") || smith_lang_punct(token, "]") || smith_lang_punct(token, "}");
        if (depth < 0 || (depth == 0 && (smith_lang_punct(token, ";") || smith_lang_punct(token, ",")))) break;
        out[count] = *token;
    }
    return count;
}

// The `=` being lowered, of `slice = array` or `T xs[:] = array`: the right side becomes the slice of the left's type.
// (The left side is typed from the tokens before it; a declaration's own name is not declared yet, so it is known by
// the tag its declaration made.)
static void smith_lang_slice_equals(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (token->foreign || token->synthetic || lang->recording || host->expr_type == nullptr || host->previous == nullptr ||
        !smith_lang_punct(token, "=")) {
        return;
    }
    SmithToken before;
    char tag[smith_lang_tuple_name_max] = "";
    if (lang->slice.armed_tag[0] != 0 && smith_lang_tuple_previous(lang, host, 1, &before) &&
        smith_lang_equals(before.text, before.length, lang->slice.armed)) {
        snprintf(tag, sizeof tag, "%s", lang->slice.armed_tag);
    } else {
        char text[512];
        SmithLangSliceType type = {};
        if (!smith_lang_tuple_base(lang, host, text, sizeof text) || !smith_lang_slice_type(host, text, &type) ||
            type.kind != SmithLangSlice_Slice) {
            return;
        }
        snprintf(tag, sizeof tag, "%s", type.tag);
    }
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, 0, right, smith_lang_slice_chain_max);
    if (count == 0 || smith_lang_punct(&right[0], "{")) return;
    static SmithLangSliceCur converted;
    converted = (SmithLangSliceCur){};
    smith_lang_slice_convert(lang, host, tag, right, count, &converted);
    if (converted.changed && !converted.overflow) smith_lang_tuple_replace(lang, count, converted.tokens, converted.count);
}

// `lhs = array;` where the chain `lhs` is a slice: the right side becomes the slice (read from the queue). Returns the
// tokens of the queue it took past the chain (the `=` and the right side), or 0.
static size_t smith_lang_slice_assign(SmithLang *lang, const SmithHost *host, const SmithLangSliceChain *chain,
                                      SmithLangSliceCur *cur) {
    char text[smith_lang_slice_text_max];
    SmithLangSliceType type = {};
    if (!smith_lang_slice_text(lang, cur->tokens, cur->count, text, sizeof text) ||
        !smith_lang_slice_type(host, text, &type) || type.kind != SmithLangSlice_Slice) {
        return 0;
    }
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, chain->count, right, smith_lang_slice_chain_max);
    if (count == 0 || smith_lang_punct(&right[0], "{")) return 0;
    static SmithLangSliceCur converted;
    converted = (SmithLangSliceCur){};
    smith_lang_slice_convert(lang, host, type.tag, right, count, &converted);
    if (!converted.changed || converted.overflow) return 0;
    smith_lang_slice_add(cur, smith_lang_peek(lang, host, chain->count - 1, false), 1); // the `=`
    smith_lang_slice_add(cur, converted.tokens, converted.count);
    return count + 1;
}

// A chain with a subscript, a call or an assignment: written again, or left as it is (false).
static bool smith_lang_slice_chain(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangSliceChain *chain = &smith_lang_slice_chain_state;
    if (!smith_lang_slice_eligible(lang, host, token) || !smith_lang_slice_read(lang, host, token, chain)) return false;
    if (!chain->subscripts && !chain->calls && !chain->assigned) return false;
    SmithLangSliceCur *cur = &smith_lang_slice_cur;
    cur->count = 0;
    cur->changed = cur->overflow = false;
    smith_lang_slice_add(cur, chain->tokens, chain->head_end);
    for (size_t i = 0; i < chain->element_count; i++) { // each member, call and subscript
        const SmithLangSliceElement *element = &chain->elements[i];
        if (element->kind == SmithLangSliceElement_Subscript) {
            smith_lang_slice_subscript_element(lang, host, chain, element, cur);
        } else if (element->kind == SmithLangSliceElement_Call && chain->calls) {
            smith_lang_slice_call(lang, host, chain, element, cur);
        } else {
            smith_lang_slice_add(cur, &chain->tokens[element->from], element->to - element->from);
        }
    }
    size_t extra = 0;
    if (chain->assigned) {
        extra = smith_lang_slice_assign(lang, host, chain, cur);
        cur->changed = cur->changed || extra > 0;
    }
    if (!cur->changed || cur->overflow) return false;
    for (size_t i = 0; i < cur->count; i++) { // the first token is skipped when it comes again
        if (!cur->tokens[i].synthetic && cur->tokens[i].location == token->location) {
            lang->slice.skip = token->location;
            break;
        }
    }
    if (!smith_lang_tuple_replace(lang, chain->count - 1 + extra, cur->tokens, cur->count)) return false;
    smith_lang_slice_flag(lang);
    return true; // the token itself is dropped: its copy leads the queue
}

// `return array;` in a function that returns a slice.
static void smith_lang_slice_return(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    char type[256];
    if (host->return_type == nullptr || !host->return_type(host->context, type, sizeof type) ||
        strncmp(type, "struct smith_array_", 19) != 0 || strchr(type + 7, ' ') != nullptr ||
        smith_lang_punct(smith_lang_peek(lang, host, 0, false), "{")) {
        return;
    }
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, 0, right, smith_lang_slice_chain_max);
    if (count == 0) return;
    static SmithLangSliceCur converted;
    converted = (SmithLangSliceCur){};
    smith_lang_slice_convert(lang, host, type + 7, right, count, &converted);
    if (converted.changed && !converted.overflow) smith_lang_tuple_replace(lang, count, converted.tokens, converted.count);
    (void)token;
}

// `countof(xs)` of a slice: `((xs).count[0])`. `span` holds the operand at 1 to close - 1.
static bool smith_lang_slice_countof(SmithLang *lang, const SmithHost *host, const SmithToken *token, const char *tokens,
                                     const SmithToken *span, size_t close) {
    SmithLangSliceType type = {};
    bool sliced = false; // `countof(xs[a:b])`: the operand is a slice whatever it is a slice of
    if (smith_lang_punct(&span[close - 1], "]")) {
        size_t open = close - 1;
        int depth = 0;
        for (; open > 1; open--) { // back to the matching `[`
            depth += smith_lang_punct(&span[open], "]");
            depth -= smith_lang_punct(&span[open], "[");
            if (depth == 0) break;
        }
        sliced = smith_lang_slice_shape(span + open + 1, close - 2 - open).colon != SIZE_MAX;
    }
    if (!sliced && (!smith_lang_slice_type(host, tokens, &type) || type.kind != SmithLangSlice_Slice)) return false;
    SmithLangSliceSpan spans[1] = {{span + 1, close - 1}};
    static SmithLangSliceCur cur;
    cur = (SmithLangSliceCur){};
    cur.count = smith_lang_slice_pattern(cur.tokens, 0, smith_lang_slice_cur_max, "( ( @0 ) . count [ 0 ] )", spans, token);
    cur.tokens[0].space_before = token->space_before;
    cur.tokens[0].line_start = token->line_start;
    return smith_lang_tuple_replace(lang, close + 1, cur.tokens, cur.count);
}
