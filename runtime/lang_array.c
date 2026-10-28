// Fixed-size arrays as results (issue #734): `Key[2] pair(Key k)`, `Key[n] split(Key k, constexpr usize n)`, and the
// slice result written bare, `i32[:] tail(i32 xs[:])`. C cannot return an array, so the function returns a struct that
// wraps it, `struct smith_array_<element>_<count> { T data[count]; }` (plain C, defined where it is first written, as a
// slice's struct is); `T xs[2] = call();` copies the elements out of the call's value. Both forms are invalid C (a
// name between a `]` and a `(`), so the C rule holds.
//
// This file is included after lang_slice.c, whose patterns, tags and queue it uses; the parts a generic needs
// (lang_generic.c) are declared there.

constexpr unsigned long long smith_lang_aret_elements_max = 256; // elements an initializer from a call writes out

// Whether the token at `at` is the `::` of a `c::` name: what follows must reach the stream unexpanded, so a read-ahead
// stops there (a macro would be expanded before the lowering has its turn).
static bool smith_lang_aret_c_scope(SmithLang *lang, const SmithToken *first, size_t at, const SmithToken *token) {
    if (at != 1 || !smith_lang_punct(token, "::")) return false;
    const SmithLangBinding *named =
        lang->bindings_count > 0 ? smith_lang_binding_find(lang, first->text, first->length) : nullptr;
    return named != nullptr && named->is_c;
}

// ---- Reading the head: `T [ size ] name (`

typedef struct {
    size_t open;  // the `[`, the first token of the declaration counted 0 (as smith_lang_slice_at)
    size_t close; // its `]`
    size_t name;  // the function's name
    bool slice;   // `[:]`
} SmithLangArrayResult;

// Whether the declaration that begins with `first` is a head whose result is written `T[...]`: the element's tokens, a
// bracket, and then the function's name and its `(`. (C has no name between a `]` and a `(`, so nothing else is.)
static bool smith_lang_aret_result_shape(SmithLang *lang, const SmithHost *host, const SmithToken *first,
                                         SmithLangArrayResult *shape) {
    if (first->kind != SmithTokenKind_Identifier || smith_lang_slice_statement_word(first)) return false;
    size_t at = 1;
    for (; at < smith_lang_slice_scan_max; at++) { // the element: names, stars and `::`
        const SmithToken *token = smith_lang_slice_at(lang, host, first, at);
        if (token->kind == SmithTokenKind_Eof || token->foreign || smith_lang_aret_c_scope(lang, first, at, token))
            return false;
        if (token->kind == SmithTokenKind_Identifier || smith_lang_punct(token, "*") || smith_lang_punct(token, "::")) {
            continue;
        }
        if (smith_lang_punct(token, "[") && smith_lang_punct(smith_lang_slice_at(lang, host, first, at + 1), "[")) {
            // an attribute list, `export [[nodiscard]] Key[:] f(`: skipped to its closing `]]`
            int depth = 0;
            for (; at < smith_lang_slice_scan_max - 4; at++) {
                const SmithToken *inner = smith_lang_slice_at(lang, host, first, at);
                if (inner->kind == SmithTokenKind_Eof || inner->foreign) return false;
                depth += smith_lang_punct(inner, "[") ? 1 : 0;
                depth -= smith_lang_punct(inner, "]") ? 1 : 0;
                if (depth == 0) break;
            }
            continue;
        }
        break;
    }
    if (at >= smith_lang_slice_scan_max - 4 || !smith_lang_punct(smith_lang_slice_at(lang, host, first, at), "[") ||
        smith_lang_slice_at(lang, host, first, at - 1)->kind != SmithTokenKind_Identifier) {
        return false;
    }
    int depth = 0;
    size_t close = at;
    for (; close < smith_lang_slice_scan_max - 2; close++) { // to the matching `]`
        const SmithToken *token = smith_lang_slice_at(lang, host, first, close);
        if (token->kind == SmithTokenKind_Eof || token->foreign) return false;
        depth += smith_lang_punct(token, "[") ? 1 : 0;
        depth -= smith_lang_punct(token, "]") ? 1 : 0;
        if (depth == 0) break;
    }
    const SmithToken *name = smith_lang_slice_at(lang, host, first, close + 1);
    if (close == at + 1 || name->kind != SmithTokenKind_Identifier ||
        smith_lang_word(name->text, name->length) != SmithLangWord_Other ||
        !smith_lang_punct(smith_lang_slice_at(lang, host, first, close + 2), "(")) {
        return false;
    }
    shape->open = at;
    shape->close = close;
    shape->name = close + 1;
    shape->slice = close == at + 2 && smith_lang_punct(smith_lang_slice_at(lang, host, first, at + 1), ":");
    return true;
}

// The number of elements a `[ ... ]` of tokens says: a number. (The header pass has no compiler to evaluate a name,
// and a C caller of the header needs the number.)
static bool smith_lang_aret_size(const SmithToken *tokens, size_t count, unsigned long long *size) {
    if (count != 1 || tokens[0].kind != SmithTokenKind_Number) return false;
    char text[64];
    snprintf(text, sizeof text, "%.*s", (int)(tokens[0].length < sizeof text - 1 ? tokens[0].length : sizeof text - 1),
             tokens[0].text);
    char *end = nullptr;
    *size = strtoull(text, &end, 0);
    return *end == 0;
}

// ---- The struct that wraps the array

// `smith_array_<element>_<count>`: the name of a slice's struct (runtime/lang_slice.c) with a fixed dimension in place
// of the run-time one, so one family names smith's array-shaped structs. Its one field is `data`, as a slice's is.
static void smith_lang_aret_tag(const char *spelled, unsigned long long size, char *tag, size_t capacity) {
    SmithLangSliceDims dims = {.count = 1};
    dims.fixed[0] = size;
    smith_lang_slice_tag_dims(spelled, &dims, tag, capacity);
}

// Whether the struct is an array result's: a slice-family tag with no run-time dimension.
static bool smith_lang_aret_is_tag(const char *tag) {
    size_t all = 0;
    size_t runtime = 0;
    return strncmp(tag, "smith_array_", 12) == 0 && smith_lang_slice_tag_dims_count(tag, &all, &runtime) &&
           runtime == 0;
}

// Remembers that the struct of this tag is defined at file scope: a later use names the tag only.
static void smith_lang_aret_register(SmithLang *lang, const SmithHost *host, const char *tag) {
    char returned[8];
    bool in_body = lang->in_function || (host != nullptr && host->return_type != nullptr &&
                                         host->return_type(host->context, returned, sizeof returned));
    if (!in_body && lang->paren_depth == 0 && lang->slice.tag_count < 32) {
        snprintf(lang->slice.tags[lang->slice.tag_count++], sizeof lang->slice.tags[0], "%s", tag);
    }
}

// `struct tag { T v[size]; }`, or only `struct tag` when an earlier declaration defined it at file scope. `count`
// tokens of the element, `size_count` of the size.
static size_t smith_lang_aret_wrap(SmithLang *lang, const SmithHost *host, SmithToken *out, size_t used,
                                   size_t capacity, const char *tag, const SmithToken *elem, size_t count,
                                   const SmithToken *size, size_t size_count, const SmithToken *like) {
    bool bare = smith_lang_slice_defined(lang, tag);
    char pattern[smith_lang_tuple_name_max + 80];
    if (bare) {
        snprintf(pattern, sizeof pattern, "struct %s", tag);
    } else {
        snprintf(pattern, sizeof pattern, "struct %s { @0 data [ @1 ] ; }", tag);
    }
    SmithLangSliceSpan spans[2] = {{elem, count}, {size, size_count}};
    size_t first = used;
    used = smith_lang_slice_pattern(out, used, capacity, pattern, spans, like);
    if (first > 0) out[first].space_before = true; // after `static` and the like
    if (bare) {
        char text[smith_lang_tuple_name_max];
        size_t length = smith_lang_slice_header_start(lang, text, sizeof text, tag, elem, count);
        char number[32] = "";
        for (size_t at = 0; at < size_count && at < 1; at++)
            snprintf(number, sizeof number, "%.*s", (int)(size[at].length < 31 ? size[at].length : 31), size[at].text);
        snprintf(text + length, sizeof text - length, " data [ %s ]; }", number);
        const char *kept = smith_lang_tuple_keep(text, strlen(text));
        out[used - 1].header_text = kept;
        out[used - 1].header_length = (uint32_t)strlen(kept);
        return used;
    }
    for (size_t i = first; i < used; i++) { // the braces of the definition, not those the element might hold
        if (out[i].synthetic && (smith_lang_punct(&out[i], "{") || smith_lang_punct(&out[i], "}")))
            out[i].tuple_brace = true;
    }
    smith_lang_aret_register(lang, host, tag);
    return used;
}

// ---- The head of a function: `T[n] f(`, `T[:] f(`

// At the first token of a declaration: a result written `T[:]` is the slice's struct, as `typeof(T[:])` would write
// it, and `T[3]` (a number) the struct that wraps the array. A generic's
// `T[n]` is recorded as written (`n` is a `constexpr` parameter): its instance writes the struct. True when the
// tokens were replaced.
static bool smith_lang_aret_result_begin(SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    if (lang->in_function) return false; // a head is at file scope: and a local array must not be read past its size
    SmithLangArrayResult shape = {};
    if (!smith_lang_aret_result_shape(lang, host, first, &shape)) return false;
    // A generic's `T[n]` is recorded as written (the declaration has not begun, so ask what it will be).
    if (!shape.slice && smith_lang_generic_candidate(lang, host, first) &&
        smith_lang_generic_ahead(lang, host, first)) {
        lang->generic_result_call = false;
        lang->array_result_open = smith_lang_slice_at(lang, host, first, shape.open)->location;
        return false;
    }
    static SmithToken span[smith_lang_slice_scan_max + 4];
    for (size_t at = 0; at <= shape.name; at++) // up to the name
        span[at] = *smith_lang_slice_at(lang, host, first, at);
    size_t leading = smith_lang_slice_leading(span, shape.open);
    const SmithToken *elem = span + leading;
    size_t elem_count = shape.open - leading;
    static SmithToken made[smith_lang_slice_scan_max + 40];
    size_t used = 0;
    // A private function of a module is `static`, which the lowering decides from the declaration's first token, and
    // the struct this head now begins with looks like a definition of its own.
    if (leading == 0 && lang->is_module && host->previous != nullptr && !lang->pending_export && !lang->decl.generic) {
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, "static", 6, first, first->space_before);
    }
    for (size_t at = 0; at < leading; at++)
        made[used++] = span[at];
    char tag[smith_lang_tuple_name_max];
    const char *problem = nullptr;
    uint32_t where = span[shape.open].location;
    char spelled[smith_lang_tuple_name_max];
    bool good =
        elem_count > 0 && smith_lang_tuple_spell(lang, elem, 0, elem_count, spelled, sizeof spelled, &problem, &where);
    if (elem_count == 0) problem = "an array needs its element type: `T[3] f()`";
    if (good && shape.slice) {
        SmithLangSliceDims dims = smith_lang_slice_dims_one();
        smith_lang_slice_tag_dims(spelled, &dims, tag, sizeof tag);
        used = smith_lang_slice_define(lang, host, made, used, sizeof made / sizeof made[0], tag, elem, elem_count,
                                       &dims, &span[leading]);
    } else if (good) {
        const SmithToken *size = span + shape.open + 1;
        size_t size_count = shape.close - shape.open - 1;
        unsigned long long value = 0;
        if (!smith_lang_aret_size(size, size_count, &value) || value == 0) {
            problem = "the size of an array a function returns is a number, or a `constexpr` parameter of a generic";
            where = size[0].location;
            good = false;
        } else {
            smith_lang_aret_tag(spelled, value, tag, sizeof tag);
            used = smith_lang_aret_wrap(lang, host, made, used, sizeof made / sizeof made[0], tag, elem, elem_count,
                                        size, size_count, &span[leading]);
        }
    }
    if (!good) {
        smith_lang_error(lang, host, where, "%s", problem != nullptr ? problem : "cannot spell the array's type");
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, "int", 3, &span[leading], true);
    }
    made[used++] = span[shape.name];
    if (!smith_lang_tuple_replace(lang, shape.name, made, used)) return false;
    smith_lang_slice_flag(lang);
    lang->slice.skip = span[shape.name].location; // the name just declared starts no chain
    return true;
}

// ---- `return array;` and `return {a, b};`

constexpr size_t smith_lang_aret_pattern_max = 700;

// The pattern of the value a `return` gives: the compound literal of the struct for a braced list, else the elements
// copied out of the array (a name, or any expression of an array's type) into the struct. `tag` is the struct's name,
// `@0` the returned expression and `@1` the message of the size check.
static const char *smith_lang_aret_value_pattern(bool braced) {
    return braced
               ? "( struct %s ) { . data = @0 }"
               : "__extension__ ( { struct %s smith_r ; auto smith_p = ( @0 ) ; static_assert ( sizeof smith_r . data "
                 "== sizeof ( @0 ) , @1 ) ; for ( __SIZE_TYPE__ smith_i = 0 ; smith_i < sizeof smith_r . data / sizeof "
                 "smith_r . data [ 0 ] ; smith_i ++ ) smith_r . data [ smith_i ] = smith_p [ smith_i ] ; smith_r ; } )";
}

// The tokens of the value, written from the expression `right`.
static size_t smith_lang_aret_value(SmithToken *out, size_t capacity, const char *tag, const SmithToken *right,
                                    size_t count, const SmithToken *like) {
    char pattern[smith_lang_aret_pattern_max];
    bool braced = smith_lang_punct(&right[0], "{");
    snprintf(pattern, sizeof pattern, smith_lang_aret_value_pattern(braced), tag);
    static const char message[] = "\"the array returned has another size than the function's result\"";
    SmithToken text = smith_lang_tuple_made(SmithTokenKind_String, message, sizeof message - 1, like, true);
    SmithLangSliceSpan spans[2] = {{right, count}, {&text, 1}};
    return smith_lang_slice_pattern(out, 0, capacity, pattern, spans, like);
}

// At the `return` of a function whose result is an array's struct: the expression after it becomes the struct.
// (A call of another such function is the struct already, and stays.) True when the expression was replaced.
static bool smith_lang_aret_return(SmithLang *lang, const SmithHost *host) {
    char type[256];
    if (host->return_type == nullptr || !host->return_type(host->context, type, sizeof type) ||
        strncmp(type, "struct ", 7) != 0 || strchr(type + 7, ' ') != nullptr || !smith_lang_aret_is_tag(type + 7)) {
        return false;
    }
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, 0, right, smith_lang_slice_chain_max);
    if (count == 0 || !smith_lang_punct(smith_lang_peek(lang, host, count, false), ";")) return false;
    if (!smith_lang_punct(&right[0], "{") && host->expr_type != nullptr) {
        char text[smith_lang_slice_text_max];
        SmithExprType found = {};
        if (smith_lang_slice_text(lang, right, count, text, sizeof text) &&
            host->expr_type(host->context, text, (uint32_t)strlen(text), &found) &&
            strncmp(found.canonical, "struct smith_array_", 19) == 0 && smith_lang_aret_is_tag(found.canonical + 7)) {
            return true; // the struct already: nothing to write
        }
    }
    static SmithToken made[smith_lang_slice_cur_max];
    size_t used = smith_lang_aret_value(made, sizeof made / sizeof made[0], type + 7, right, count, &right[0]);
    made[0].space_before = true;
    return smith_lang_tuple_replace(lang, count, made, used);
}

// ---- `T xs[3] = call();`

// At the `=` of `T xs[N] = value;` in a function, where the value is no list and no string (C has no other initializer
// for an array): the value is a call that returns the array's struct, and the array gets its elements,
//     T xs[N]; auto smith_w_xs = value; static_assert(...); xs[0] = smith_w_xs.data[0]; ...
// (the declaration is written already, so the `=` is dropped and the rest follows it). Read here and not at the
// declaration's first token because only the tokens up to the `=` are the declaration's: a look at the value from the
// first token reads ahead of the parser. True when the `=` was replaced.
// At the first token of a declaration `T xs[N] =` (the tokens up to the `=` only: reading farther ahead pulls the next
// statement's questions to the compiler into a batch it has not parsed): remembers the name, so that the `=` knows it
// follows a declaration (the type of `i32 xs[2]` is a sized name the host's record of the last tokens does not hold)
// and not an assignment to an element. Replaces nothing.
static void smith_lang_aret_init_arm(SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    lang->aret_armed = false;
    if (!lang->in_function || first->kind != SmithTokenKind_Identifier || smith_lang_slice_statement_word(first) ||
        smith_lang_word(first->text, first->length) == SmithLangWord_Storage) {
        return;
    }
    bool constant = smith_lang_ident(first, "const");
    size_t at = 1;
    for (; at < smith_lang_slice_scan_max; at++) { // the element's type: names and `::`
        const SmithToken *token = smith_lang_slice_at(lang, host, first, at);
        if (token->kind == SmithTokenKind_Eof || token->foreign || smith_lang_aret_c_scope(lang, first, at, token))
            return;
        if (token->kind != SmithTokenKind_Identifier && !smith_lang_punct(token, "::")) break;
        constant = constant || smith_lang_ident(token, "const");
    }
    const SmithToken *bracket = smith_lang_slice_at(lang, host, first, at);
    const SmithToken *name = smith_lang_slice_at(lang, host, first, at - 1);
    if (at < 2 || at >= smith_lang_slice_scan_max - 8 || !smith_lang_punct(bracket, "[") ||
        name->kind != SmithTokenKind_Identifier || smith_lang_word(name->text, name->length) != SmithLangWord_Other) {
        return;
    }
    // Only as far as the declaration's size: a read that goes on to the `=` puts it in the batch of the type, where the
    // parser has not yet seen the declaration (the host's record of the last tokens is then short of it).
    const SmithToken *number = smith_lang_slice_at(lang, host, first, at + 1);
    if (number->kind != SmithTokenKind_Number) return;
    char digits[32];
    snprintf(digits, sizeof digits, "%.*s", (int)(number->length < 31 ? number->length : 31), number->text);
    snprintf(lang->aret_name, sizeof lang->aret_name, "%.*s", (int)(name->length < 63 ? name->length : 63), name->text);
    lang->aret_armed = true;
    lang->aret_elements = strtoull(digits, nullptr, 0);
    lang->aret_const = constant;
}

static size_t smith_lang_aret_init_write(SmithToken *made, size_t made_max, const SmithToken *right, size_t count,
                                         unsigned long long elements, const char *name, const SmithToken *like) {
    char temporary[smith_lang_name_max];
    snprintf(temporary, sizeof temporary, "smith_w_%.40s", name);
    SmithToken declared = smith_lang_tuple_made(SmithTokenKind_Identifier, smith_lang_tuple_keep(name, strlen(name)),
                                                strlen(name), like, true);
    char pattern[smith_lang_aret_pattern_max];
    snprintf(pattern, sizeof pattern, "; auto %s =", temporary);
    size_t used = smith_lang_slice_pattern(made, 0, made_max, pattern, nullptr, like);
    for (size_t index = 0; index < count && used + 1 < made_max; index++) // the value, as written
        made[used++] = right[index];
    static const char message[] = "\"the array has another size than the call's result\"";
    SmithToken text = smith_lang_tuple_made(SmithTokenKind_String, message, sizeof message - 1, like, true);
    SmithLangSliceSpan spans[2] = {{&text, 1}, {&declared, 1}};
    snprintf(pattern, sizeof pattern, "; static_assert ( sizeof %s . data / sizeof %s . data [ 0 ] == %llu , @0 ) ;",
             temporary, temporary, elements);
    used = smith_lang_slice_pattern(made, used, made_max, pattern, spans, like);
    for (unsigned long long element = 0; element < elements; element++) { // the elements, one by one
        snprintf(pattern, sizeof pattern, "@1 [ %llu ] = %s . data [ %llu ] %s", element, temporary, element,
                 element + 1 < elements ? ";" : "");
        used = smith_lang_slice_pattern(made, used, made_max, pattern, spans, like);
    }
    return used;
}

static bool smith_lang_aret_init_equals(SmithLang *lang, const SmithHost *host, const SmithToken *equals) {
    if (!lang->aret_armed || equals->synthetic || lang->recording || host->expr_type == nullptr ||
        host->previous == nullptr) {
        return false;
    }
    lang->aret_armed = false;
    SmithToken before[5];
    for (unsigned back = 1; back <= 4; back++) { // `]`, the size, `[` and the name
        if (!smith_lang_tuple_previous(lang, host, back, &before[back])) return false;
    }
    if (!smith_lang_punct(&before[1], "]") || !smith_lang_punct(&before[3], "[") ||
        !smith_lang_equals(before[4].text, before[4].length, lang->aret_name)) {
        return false;
    }
    const SmithToken *value = smith_lang_peek(lang, host, 0, false);
    if (value->kind == SmithTokenKind_Eof || value->foreign || value->kind == SmithTokenKind_String ||
        smith_lang_punct(value, "{")) {
        return false;
    }
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, 0, right, smith_lang_slice_chain_max);
    if (count == 0 || !smith_lang_punct(smith_lang_peek(lang, host, count, false), ";")) return false;
    unsigned long long elements = lang->aret_elements;
    if (elements == 0 || elements > smith_lang_aret_elements_max || lang->aret_const) {
        smith_lang_error(lang, host, equals->location,
                         lang->aret_const ? "an array initialized from a call is not `const`: its elements are written "
                                            "after the declaration"
                                          : "an array initialized from a call has 1 to %llu elements",
                         smith_lang_aret_elements_max);
        return false;
    }
    static SmithToken made[smith_lang_slice_cur_max + 8 * smith_lang_aret_elements_max + 120];
    size_t used =
        smith_lang_aret_init_write(made, sizeof made / sizeof made[0], right, count, elements, lang->aret_name, equals);
    if (!smith_lang_tuple_replace(lang, count, made, used)) return false;
    smith_lang_slice_flag(lang);
    return true;
}

// ---- A generic that returns an array: `Key[n] split(Key key, constexpr usize n)`

// The generic's head is recorded as written, `Key [ n ] split (`; each instance writes the struct for its own `n` (and
// its `T`): the tokens of the element are [from, open) and the size is the one token after `open`, in smith_lang_gt.
typedef struct {
    uint32_t from; // the first token of the result, an index of smith_lang_gt
    uint32_t open; // its `[`
} SmithLangArrayHead;

static bool smith_lang_aret_generic_head(const SmithLangGeneric *generic, SmithLangArrayHead *head) {
    if (generic->name_index < 4) return false;
    uint32_t from = generic->first + smith_lang_generic_skip_attributes(generic);
    uint32_t last = generic->first + generic->name_index - 1;
    if (last <= from + 2 || !smith_lang_gt_punct(&smith_lang_gt.tokens[last], "]")) return false;
    uint32_t open = last;
    int depth = 0;
    for (; open > from; open--) { // back to the matching `[`
        depth += smith_lang_gt_punct(&smith_lang_gt.tokens[open], "]") ? 1 : 0;
        depth -= smith_lang_gt_punct(&smith_lang_gt.tokens[open], "[") ? 1 : 0;
        if (depth == 0) break;
    }
    if (open <= from || !smith_lang_gt_punct(&smith_lang_gt.tokens[open], "[") || open + 2 != last) return false;
    if (smith_lang_gt_punct(&smith_lang_gt.tokens[open + 1], ":")) return false;
    head->from = from;
    head->open = open;
    return true;
}

static bool smith_lang_aret_generic(const SmithLangGeneric *generic) {
    SmithLangArrayHead head;
    return smith_lang_aret_generic_head(generic, &head);
}

// The number of elements for this instance: a number written in the head, or the `constexpr usize n` it names.
static bool smith_lang_aret_generic_size(const SmithLangGeneric *generic, const SmithLangInstance *instance,
                                         const SmithLangArrayHead *head, unsigned long long *size) {
    const SmithLangGenToken *token = &smith_lang_gt.tokens[head->open + 1];
    char text[32] = "";
    if (token->kind == SmithTokenKind_Number) {
        snprintf(text, sizeof text, "%.*s", (int)(token->length < 31 ? token->length : 31), smith_lang_gt_str(token));
    } else if (token->kind == SmithTokenKind_Identifier && instance != nullptr) {
        for (size_t k = 0; k < generic->type_count && k < instance->entries; k++) { // the number parameters
            const SmithLangGenToken *name = &smith_lang_gt.tokens[generic->first + generic->type_token[k]];
            if (generic->type_kind[k] != SmithLangSlot_Number || name->length != token->length ||
                memcmp(smith_lang_gt_str(name), smith_lang_gt_str(token), token->length) != 0) {
                continue;
            }
            smith_lang_it_spelling(instance->arg_first[k], instance->arg_count[k], text, sizeof text);
        }
    }
    char *end = nullptr;
    *size = strtoull(text, &end, 0);
    return text[0] != 0 && *end == 0 && *size > 0;
}

// The struct's name for this instance: the element as the instance spells it, and the number.
static bool smith_lang_aret_generic_tag(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                        const SmithLangInstance *instance, const SmithLangArrayHead *head,
                                        uint32_t location, char *tag, size_t capacity) {
    unsigned long long size = 0;
    if (!smith_lang_aret_generic_size(generic, instance, head, &size)) {
        smith_lang_error(lang, host, location,
                         "the size of the array `%s` returns is a number, or a `constexpr` parameter given a number",
                         smith_lang_str(generic->member));
        snprintf(tag, capacity, "smith_array_error_1");
        return false;
    }
    static SmithToken span[48];
    size_t count = 0;
    for (uint32_t at = head->from; at < head->open && count + 1 < sizeof span / sizeof span[0]; at++) {
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (token->param != 0 && instance != nullptr && (size_t)token->param <= instance->entries) {
            unsigned slot = (unsigned)token->param - 1;
            for (uint32_t i = 0; i < instance->arg_count[slot] && count + 1 < sizeof span / sizeof span[0]; i++)
                span[count++] =
                    smith_lang_gt_token(&smith_lang_it, &smith_lang_it.tokens[instance->arg_first[slot] + i]);
        } else {
            span[count++] = smith_lang_gt_token(&smith_lang_gt, token);
        }
    }
    for (size_t i = 0; i < count; i++) { // `u8` was lowered
        for (size_t k = 0; k < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; k++) {
            if (!smith_lang_equals(span[i].text, span[i].length, smith_lang_sized[k].standard)) continue;
            span[i].text = smith_lang_sized[k].name;
            span[i].length = (uint32_t)strlen(smith_lang_sized[k].name);
        }
    }
    char spelled[512];
    const char *problem = nullptr;
    uint32_t where = location;
    if (!smith_lang_tuple_spell(lang, span, 0, count, spelled, sizeof spelled, &problem, &where)) {
        smith_lang_error(lang, host, location, "%s", problem != nullptr ? problem : "cannot spell the array's type");
        snprintf(spelled, sizeof spelled, "error");
    }
    smith_lang_aret_tag(spelled, size, tag, capacity);
    return true;
}

// The result of an instance's signature: `struct tag`.
static void smith_lang_aret_put_result(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                       const SmithLangInstance *instance, uint32_t location, uint32_t id) {
    SmithLangArrayHead head;
    char tag[smith_lang_tuple_name_max];
    if (!smith_lang_aret_generic_head(generic, &head)) return;
    smith_lang_aret_generic_tag(lang, host, generic, instance, &head, location, tag, sizeof tag);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "struct", location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, tag, location, id);
}

// `struct tag { T data[n]; };` at file scope before the instance, and in the block of a call before its declaration
// (C23 makes the definitions one type), unless the file defined it at file scope already.
static void smith_lang_aret_define(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                   const SmithLangInstance *instance, uint32_t location, uint32_t id) {
    SmithLangArrayHead head;
    char tag[smith_lang_tuple_name_max];
    if (!smith_lang_aret_generic_head(generic, &head)) return;
    if (!smith_lang_aret_generic_tag(lang, host, generic, instance, &head, location, tag, sizeof tag) ||
        smith_lang_slice_defined(lang, tag)) {
        return;
    }
    char size[32];
    unsigned long long value = 0;
    smith_lang_aret_generic_size(generic, instance, &head, &value);
    snprintf(size, sizeof size, "%llu", value);
    smith_lang_gen_note_break(lang);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "struct", location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, tag, location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "{", location, id);
    smith_lang_gen_put_run(lang, host, generic, instance, head.from, head.open, location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "data", location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "[", location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Number, size, location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "]", location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ";", location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "}", location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ";", location, id);
}

// At `return` in an instance's body: the expression becomes the struct, as a source's own `return` does (above).
static bool smith_lang_aret_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                   SmithLangFrame *frame) {
    uint32_t at = frame->pos;
    SmithLangArrayHead head;
    if (replay->instance == nullptr || replay->typing || at + 2 >= frame->end || replay->generic == nullptr ||
        !smith_lang_aret_generic_head(replay->generic, &head)) {
        return false;
    }
    SmithLangGenToken *keyword = &smith_lang_gt.tokens[at];
    if (keyword->special != SmithLangGen_Plain || keyword->param != 0 ||
        !smith_lang_gt_is(keyword, SmithTokenKind_Identifier, "return")) {
        return false;
    }
    uint32_t stop = at + 1;
    int depth = 0;
    for (; stop < frame->end; stop++) { // to the `;` that ends the statement
        const SmithLangGenToken *token = &smith_lang_gt.tokens[stop];
        if (token->special != SmithLangGen_Plain) continue;
        depth += smith_lang_gt_open(token) ? 1 : 0;
        depth -= smith_lang_gt_close(token) ? 1 : 0;
        if (depth == 0 && smith_lang_gt_punct(token, ";")) break;
    }
    if (stop >= frame->end || stop == at + 1) return false;
    char tag[smith_lang_tuple_name_max];
    uint32_t place = smith_lang_gen_place(host, replay->generic, keyword, replay->location);
    if (!smith_lang_aret_generic_tag(lang, host, replay->generic, replay->instance, &head, place, tag, sizeof tag))
        return false;
    bool braced = smith_lang_gt_punct(&smith_lang_gt.tokens[at + 1], "{");
    char open[512];
    char middle[512];
    char close[640];
    if (braced) {
        snprintf(open, sizeof open, "return ( struct %s ) { . data =", tag);
        snprintf(close, sizeof close, "}");
        frame->pos = stop;
        SmithLangSlicePart parts[] = {{open, 0, 0}, {nullptr, at + 1, stop}, {close, 0, 0}};
        smith_lang_slice_parts(replay, place, parts, 3);
    } else {
        snprintf(open, sizeof open, "return __extension__ ( { struct %s smith_aret_r ; auto smith_aret_p = (", tag);
        snprintf(middle, sizeof middle, ") ; static_assert ( sizeof smith_aret_r . data == sizeof (");
        snprintf(close, sizeof close,
                 ") , \"the array returned has another size than the function's result\" ) ; for ( __SIZE_TYPE__ "
                 "smith_aret_i = 0 ; smith_aret_i < sizeof smith_aret_r . data / sizeof smith_aret_r . data [ 0 ] ; "
                 "smith_aret_i ++ ) smith_aret_r . data [ smith_aret_i ] = smith_aret_p [ smith_aret_i ] ; "
                 "smith_aret_r ; } )");
        frame->pos = stop;
        SmithLangSlicePart parts[] = {
            {open, 0, 0}, {nullptr, at + 1, stop}, {middle, 0, 0}, {nullptr, at + 1, stop}, {close, 0, 0}};
        smith_lang_slice_parts(replay, place, parts, 5);
    }
    replay->frames[replay->count - 1].line_start = (keyword->flags & smith_lang_gen_flag_line_start) != 0;
    return true;
}
