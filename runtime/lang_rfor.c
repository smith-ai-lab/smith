// Range `for` (issue #243): `for (T x : xs) { ... }` and `for (usize i, T x : xs) { ... }` over a slice or a fixed
// array.
//
// A slice is evaluated once and read through its pointer, with no bounds check, since the index is in range by
// construction:
//     { auto r = xs; for (usize i = 0; i < r.count[0]; i++) { T x = r.data[i]; ... } }
// A fixed array, and any `for` where the lowering cannot ask the compiler (a generic's body, which is written for each
// instance, and the raw lowering), is the loop written with `countof` and a subscript, which the other lowerings
// then handle: `for (usize i = 0; i < countof(xs); i++) { T x = xs[i]; ... }`.
// The declared type decides between a copy and a pointer: `T x` copies when T is the element type, anything else is
// the address of the element (`T *p`), a choice of C's own (`__builtin_choose_expr`).

constexpr size_t smith_lang_rfor_head_max = 120; // the tokens of a header
constexpr size_t smith_lang_rfor_out_max = 640;  // the tokens it is written as
constexpr size_t smith_lang_rfor_commas_max = 4;

typedef struct {
    SmithToken tokens[smith_lang_rfor_head_max]; // between the parentheses
    size_t count;
    size_t colon;                              // the `:` that ends the declarations (SIZE_MAX: none)
    size_t commas[smith_lang_rfor_commas_max]; // the commas before it
    size_t comma_count;
    size_t after; // the queue index of the token after the `)`
} SmithLangRforHead;

static SmithLangRforHead smith_lang_rfor_head;
static SmithToken smith_lang_rfor_out[smith_lang_rfor_out_max];

// The header of the `for` being lowered, from its `(`: false when it is no range `for` (it has a `;`, or no `:`).
static bool smith_lang_rfor_read(SmithLang *lang, const SmithHost *host, SmithLangRforHead *head) {
    *head = (SmithLangRforHead){.colon = SIZE_MAX};
    if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(")) return false;
    int depth = 0;
    int conditions = 0;
    for (size_t at = 0; at < smith_lang_rfor_head_max + 2; at++) { // bounded by the header's size
        const SmithToken *token = smith_lang_peek(lang, host, at, false);
        if (token->kind == SmithTokenKind_Eof || token->foreign) return false;
        bool open = smith_lang_punct(token, "(") || smith_lang_punct(token, "[") || smith_lang_punct(token, "{");
        bool close = smith_lang_punct(token, ")") || smith_lang_punct(token, "]") || smith_lang_punct(token, "}");
        depth += open ? 1 : close ? -1 : 0;
        if (depth == 0) {
            head->after = at + 1;
            return head->colon != SIZE_MAX;
        }
        if (at == 0) continue;
        if (head->count >= smith_lang_rfor_head_max) return false;
        if (depth == 1 && smith_lang_punct(token, ";")) return false; // C's own loop
        if (depth == 1 && smith_lang_punct(token, "?")) conditions += 1;
        if (depth == 1 && smith_lang_punct(token, ":")) {
            if (conditions > 0) {
                conditions -= 1;
            } else if (head->colon == SIZE_MAX) {
                head->colon = head->count;
            }
        }
        if (depth == 1 && head->colon == SIZE_MAX && smith_lang_punct(token, ",") &&
            head->comma_count < smith_lang_rfor_commas_max) {
            head->commas[head->comma_count++] = head->count;
        }
        head->tokens[head->count++] = *token;
    }
    return false;
}

// The loop without its header: an error was said, so the tokens stand for `for (;;)` and the statement parses.
static void smith_lang_rfor_empty(SmithLang *lang, const SmithToken *token, size_t replaced) {
    static const char *const words[] = {"for", "(", ";", ";", ")"};
    SmithToken made[5];
    for (size_t i = 0; i < 5; i++) {
        made[i] = smith_lang_tuple_made(i == 0 ? SmithTokenKind_Identifier : SmithTokenKind_Punctuator, words[i],
                                        strlen(words[i]), token, i == 0 && token->space_before);
    }
    made[0].line_start = token->line_start;
    smith_lang_tuple_replace(lang, replaced, made, 5);
}

// What the header declares: the index (optional) and the element, as spans of the header's tokens.
typedef struct {
    bool has_index;
    SmithLangSliceSpan index; // the name
    SmithLangSliceSpan type;  // the element's type
    SmithLangSliceSpan name;  // the element's name
    SmithLangSliceSpan range; // after the `:`
    bool automatic;           // `auto` in the type
    bool pointer;             // a `*` in the type
    unsigned concept_id;      // 1 + the concept the type names, or 0
} SmithLangRforParts;

static bool smith_lang_rfor_has(const SmithLangSliceSpan *span, const char *word) {
    for (size_t i = 0; i < span->count; i++) {
        if (smith_lang_ident(&span->tokens[i], word) || smith_lang_punct(&span->tokens[i], word)) return true;
    }
    return false;
}

// The parts of the header, or an error (said) and false.
static bool smith_lang_rfor_parts(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                  const SmithLangRforHead *head, SmithLangRforParts *parts) {
    *parts = (SmithLangRforParts){};
    const SmithToken *tokens = head->tokens;
    size_t element = 0;
    if (head->comma_count > 1) {
        smith_lang_error(lang, host, token->location,
                         "a range `for` declares an index and an element at most: `for (Usize i, T x : xs)`; C's "
                         "loop walks several slices together");
        return false;
    }
    if (head->comma_count == 1) {
        size_t comma = head->commas[0];
        bool ok = comma == 2 && smith_lang_ident(&tokens[0], "Usize") && tokens[1].kind == SmithTokenKind_Identifier;
        if (!ok) {
            smith_lang_error(lang, host, token->location,
                             "the index of a range `for` is a `Usize`, counted from 0: `for (Usize i, T x : xs)`");
            return false;
        }
        parts->has_index = true;
        parts->index = (SmithLangSliceSpan){&tokens[1], 1};
        element = comma + 1;
    }
    size_t last = head->colon - 1;
    if (head->colon <= element + 1 || tokens[last].kind != SmithTokenKind_Identifier) {
        smith_lang_error(lang, host, token->location,
                         head->colon > element && smith_lang_punct(&tokens[last], "]")
                             ? "a range `for` has no structured bindings: write `for (Usize i, T x : xs)`"
                             : "a range `for` declares its element with a type: `for (T x : xs)`, or `auto x`");
        return false;
    }
    parts->type = (SmithLangSliceSpan){&tokens[element], last - element};
    parts->name = (SmithLangSliceSpan){&tokens[last], 1};
    parts->range = (SmithLangSliceSpan){&tokens[head->colon + 1], head->count - head->colon - 1};
    if (parts->range.count == 0) {
        smith_lang_error(lang, host, token->location,
                         "a range `for` loops over a slice or a fixed array: `for (T x : xs)`");
        return false;
    }
    parts->automatic = smith_lang_rfor_has(&parts->type, "auto");
    parts->pointer = smith_lang_rfor_has(&parts->type, "*");
    if (parts->automatic && parts->pointer) {
        smith_lang_error(lang, host, token->location,
                         "`auto *p` is not C: name the element's type to point at it, `T *p` (`typeof(xs[0]) *p` in "
                         "a generic), or write `auto x` for a copy");
        return false;
    }
    for (size_t i = 0; i < sizeof smith_lang_concepts / sizeof smith_lang_concepts[0]; i++) { // 10 concepts
        if (parts->type.count == 1 && smith_lang_ident(&parts->type.tokens[0], smith_lang_concepts[i])) {
            parts->concept_id = (unsigned)i + 1;
        }
    }
    return true;
}

typedef enum {
    SmithLangRfor_Source, // written with `countof` and a subscript
    SmithLangRfor_Slice,  // evaluated once, read through its pointer
    SmithLangRfor_Error,
} SmithLangRforForm;

// A name of the function that is an array parameter of one dimension.
static bool smith_lang_rfor_array_parameter(const SmithLang *lang, const SmithLangSliceSpan *range) {
    if (range->count != 1 || range->tokens[0].kind != SmithTokenKind_Identifier || !lang->in_function) return false;
    for (uint32_t i = 0; i < lang->array_param_count; i++) { // at most 8
        const SmithLangArrayParam *param = &lang->array_params[i];
        if (smith_lang_equals(range->tokens[0].text, range->tokens[0].length, param->name) && param->dims == 1 &&
            param->sizes[0].whole) {
            return true;
        }
    }
    return false;
}

// What the range is: a slice (a cut is one), a fixed array, or neither (said).
static SmithLangRforForm smith_lang_rfor_classify(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                                  const SmithLangSliceSpan *range, char *text, size_t capacity) {
    SmithLangRforForm form = SmithLangRfor_Error;
    size_t end = range->count;
    if (end > 1 && smith_lang_punct(&range->tokens[end - 1], "]")) { // `xs[a:b]`
        size_t open = end - 1;
        int depth = 0;
        for (; open > 0; open--) {
            depth += smith_lang_punct(&range->tokens[open], "]");
            depth -= smith_lang_punct(&range->tokens[open], "[");
            if (depth == 0) break;
        }
        if (open > 0 && smith_lang_slice_shape(range->tokens + open + 1, end - open - 2).colon != SIZE_MAX) {
            return SmithLangRfor_Slice;
        }
    }
    SmithLangSliceType type = {};
    if (smith_lang_rfor_array_parameter(lang, range)) {
        form = SmithLangRfor_Source;
    } else if (smith_lang_slice_text(lang, range->tokens, range->count, text, capacity) &&
               smith_lang_slice_type(host, text, &type)) {
        if (type.kind == SmithLangSlice_Slice && type.dims.count == 1) form = SmithLangRfor_Slice;
        if (type.kind == SmithLangSlice_Array && type.rank == 1) form = SmithLangRfor_Source;
    }
    if (form == SmithLangRfor_Error) {
        smith_lang_error(lang, host, token->location,
                         "a range `for` loops over a slice (`T xs[:]`, a cut `xs[a:b]`) or a fixed array (`T xs[N]`) "
                         "of one dimension; write C's loop for anything else: `for (Usize i = 0; i < n; ++i)`");
    }
    return form;
}

// A concept in the place of the element's type is `auto`, judged by the element: `E.data[0]` or `E[0]`.
static void smith_lang_rfor_concept(SmithLang *lang, const SmithHost *host, SmithLangRforParts *parts,
                                    SmithToken *automatic, SmithLangRforForm form, const char *text) {
    if (parts->concept_id == 0) return;
    char probe[smith_lang_slice_text_max + 16];
    snprintf(probe, sizeof probe, "(%s)%s[0]", text, form == SmithLangRfor_Slice ? ".data" : "");
    SmithExprType found = {};
    if (form != SmithLangRfor_Error && host->expr_type != nullptr && text[0] != 0 &&
        host->expr_type(host->context, probe, (uint32_t)strlen(probe), &found) &&
        !smith_lang_concept_holds(host, parts->concept_id, found.canonical)) {
        smith_lang_concept_why(lang, host, parts->type.tokens[0].location, parts->concept_id, found.canonical);
        smith_lang_error(lang, host, parts->type.tokens[0].location, "`%s` is not `%s`: the elements have that type",
                         found.written, smith_lang_concepts[parts->concept_id - 1]);
    }
    *automatic = smith_lang_tuple_made(SmithTokenKind_Identifier, "auto", 4, &parts->type.tokens[0],
                                       parts->type.tokens[0].space_before);
    parts->type = (SmithLangSliceSpan){automatic, 1};
    parts->automatic = true;
}

// The pattern of the loop's start, up to and with the `{` of the body and the element's declaration.
static void smith_lang_rfor_pattern(char *out, size_t capacity, SmithLangRforForm form,
                                    const SmithLangRforParts *parts) {
    // the names are the loop's own: the lowering of a subscript renames `smith_i<n>`
    unsigned counter = ++smith_lang_counter.rfor_pattern;
    char own[48];
    char copy[48];
    snprintf(own, sizeof own, "smith_rfor_i%u", counter);
    snprintf(copy, sizeof copy, "smith_rfor_r%u . data [ %%s ]", counter);
    const char *index = parts->has_index ? "@1" : own;
    const char *element = form == SmithLangRfor_Slice ? copy : "@0 [ %s ]";
    char place[96];
    char first[96];
    snprintf(place, sizeof place, element, index);
    snprintf(first, sizeof first, element, "0");
    size_t used = 0;
    if (form == SmithLangRfor_Slice) {
        used += (size_t)snprintf(out + used, capacity - used,
                                 "{ auto smith_rfor_r%u = @0 ; for ( __SIZE_TYPE__ %s = 0 ; %s < "
                                 "smith_rfor_r%u . count [ 0 ] ; %s ++ ) {",
                                 counter, index, index, counter, index);
    } else {
        used += (size_t)snprintf(out + used, capacity - used,
                                 "for ( __SIZE_TYPE__ %s = 0 ; %s < countof ( @0 ) ; %s ++ ) {", index, index, index);
    }
    if (parts->automatic || !parts->pointer) { // a copy, converted as C converts an initializer
        snprintf(out + used, capacity - used, " @2 @3 = %s ;", place);
    } else {
        snprintf(
            out + used, capacity - used,
            " @2 @3 = __builtin_choose_expr ( __builtin_types_compatible_p ( @2 , __typeof__ ( %s ) ) , %s , & %s ) ;",
            first, place, place);
    }
}

// `for (...) {` at the `for` token: true when the tokens were replaced (also after an error, by `for (;;)`).
static bool smith_lang_rfor(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangRforHead *head = &smith_lang_rfor_head;
    if (!smith_lang_rfor_read(lang, host, head)) return false;
    size_t replaced = head->after; // the header; the body's `{` is written again with the rest
    SmithLangRforParts parts;
    if (!smith_lang_rfor_parts(lang, host, token, head, &parts)) {
        smith_lang_rfor_empty(lang, token, replaced);
        return true;
    }
    if (!smith_lang_punct(smith_lang_peek(lang, host, head->after, false), "{")) {
        smith_lang_error(lang, host, token->location, "the body of a range `for` is a block: `for (T x : xs) { ... }`");
        smith_lang_rfor_empty(lang, token, replaced);
        return true;
    }
    static char text[smith_lang_slice_text_max];
    text[0] = 0;
    SmithLangRforForm form = SmithLangRfor_Source;
    if (!lang->recording && host->expr_type != nullptr) {
        form = smith_lang_rfor_classify(lang, host, token, &parts.range, text, sizeof text);
        if (form == SmithLangRfor_Error) {
            smith_lang_rfor_empty(lang, token, replaced);
            return true;
        }
    }
    replaced += 1; // and the body's `{`, written again with the rest
    SmithToken automatic;
    smith_lang_rfor_concept(lang, host, &parts, &automatic, form, text);
    char pattern[1024];
    smith_lang_rfor_pattern(pattern, sizeof pattern, form, &parts);
    SmithLangSliceSpan spans[4] = {parts.range, parts.index, parts.type, parts.name};
    size_t made = smith_lang_slice_pattern(smith_lang_rfor_out, 0, smith_lang_rfor_out_max, pattern, spans, token);
    if (made + 1 >= smith_lang_rfor_out_max || lang->rfor_count >= smith_lang_rfor_depth_max) {
        smith_lang_error(lang, host, token->location, "this range `for` is too long or nested too deep");
        smith_lang_rfor_empty(lang, token, replaced - 1);
        return true;
    }
    if (parts.has_index && form == SmithLangRfor_Slice) { // the index is spelled once: its other places are copies,
                                                          // which the source's order skips (#1008)
        bool first = true;
        for (size_t at = 0; at < made; at++) {
            const SmithToken *index = &smith_lang_rfor_out[at];
            if (index->synthetic || index->location != parts.index.tokens[0].location) continue;
            if (!first) smith_lang_rfor_out[at].synthetic = smith_lang_rfor_out[at].copy = true;
            first = false;
        }
    }
    smith_lang_rfor_out[0].space_before = token->space_before;
    smith_lang_rfor_out[0].line_start = token->line_start;
    smith_lang_slice_cover(smith_lang_rfor_out, made);
    if (!smith_lang_tuple_replace(lang, replaced, smith_lang_rfor_out, made)) return false;
    if (form == SmithLangRfor_Slice) { // the `}` of its body closes the block that holds the copy, too
        if (lang->rfor_count == 0) lang->rfor_braces = 0;
        lang->rfor[lang->rfor_count++] = lang->rfor_braces;
    }
    smith_lang_slice_flag(lang);
    return true;
}

// Counts the braces the lowering sees while a slice's loop is open.
static bool smith_lang_rfor_count(SmithLang *lang, const SmithToken *token) {
    if (lang->rfor_count == 0 || token->foreign || token->kind != SmithTokenKind_Punctuator) return false;
    if (smith_lang_punct(token, "{")) lang->rfor_braces += 1;
    if (!smith_lang_punct(token, "}")) return false;
    lang->rfor_braces -= 1;
    return !token->synthetic && lang->rfor_braces == lang->rfor[lang->rfor_count - 1] + 1;
}

// The `}` that ended the body was written: the block that holds the slice's copy ends after it.
static void smith_lang_rfor_close(SmithLang *lang, const SmithToken *token) {
    lang->rfor_count -= 1;
    SmithToken closing = smith_lang_tuple_made(SmithTokenKind_Punctuator, "}", 1, token, false);
    closing.line_start = false;
    smith_lang_tuple_replace(lang, 0, &closing, 1);
}
