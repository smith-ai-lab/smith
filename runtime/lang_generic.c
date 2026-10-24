// Generic functions (issue #15): a function whose parameters include `constexpr type T`, written once and compiled for
// each type it is called with. Included by runtime/lang.c, after runtime/lang_head.c.
//
//   export T *push(Arena *a, constexpr type T) { ... }      // in module arena
//   Point *p = arena::push(&a, Point);            // the call
//
// There is no template language and nothing reaches the parser until it is an ordinary function:
//   - The definition is recorded as the tokens the lowering would have written (names already prefixed, sized types
//     already standard), with the identifiers that name `T` marked. Nothing of it is written to the parser; a
//     placeholder declaration (`[[gnu::unavailable]] void arena_push(void);`) keeps the name for hover and the outline.
//     A module's header carries the record as a comment line (`// smith-generic:`), so C sees nothing and an importer
//     has the tokens it needs.
//   - A call is read whole, the `type` argument is taken out, and the call becomes
//     `__extension__ ({ extern R f(P) __asm__("label"); f(args); })`, where `f` names the instance for that type.
//     The arguments are inside the statement expression so that the call is direct, and `always_inline` applies at -O0
//     (`({ ...; f; })(args)` calls through a pointer, which is never inlined there).
//   - Each distinct type (by the parser's canonical spelling) is one instance. Instances are written at the end of the
//     file, as weak, hidden definitions with the label for their symbol, so that the linker folds the copies of other
//     files. A generic declared `inline` also gets `always_inline`.
//   - Inside a generic's body a call through a module's name is kept as a group (CallBegin ... CallEnd), because
//     whether it is generic is known only where the generic is instantiated.

// ---- Records
// ------------------------------------------------------------------------------------------------------------

static SmithLangGeneric smith_lang_generics[smith_lang_generics_max];

typedef struct {
    SmithTokenKind kind;
    const char *text;
} SmithLangPiece;

typedef struct {
    uint32_t start; // the tokens of one parameter: [start, end) in smith_lang_gt
    uint32_t end;
    int8_t type_index; // which `type T` or `constexpr auto f` it is (-1: any other parameter)
    uint8_t kind;      // a SmithLangSlot (Plain for a parameter that is no slot)
} SmithLangParam;

typedef struct {
    uint32_t generic;
    uint32_t arg_first[smith_lang_entries_max]; // the type arguments, lowered, in smith_lang_it: a slot each, and the
    uint32_t arg_count[smith_lang_entries_max]; // types of `auto... rest` where its slot is
    uint8_t entries;                            // how many (the last slot's list may be empty)
    SmithLangText key;
    SmithLangText cname;
    SmithLangText label;
    SmithLangText description;
    uint32_t call_location;
    uint32_t depth;
    uint32_t parent;        // 1 + the instance whose body asked for it (0: the file's own code)
    uint8_t result_state;   // a computed result type (issue #102): 0 not yet asked, 1 in `result`, 2 failed (reported)
    char result[160];       // its spelling
    uint64_t result_handle; // and its type's handle (a slice is written with its struct, runtime/lang_type.c)
} SmithLangInstance;

static SmithLangInstance smith_lang_instances[smith_lang_instances_max];

static const char *smith_lang_gt_str(const SmithLangGenToken *token) {
    return smith_lang_pool_text(&smith_lang_gt, token);
}

static bool smith_lang_gt_is(const SmithLangGenToken *token, SmithTokenKind kind, const char *literal) {
    return token->kind == kind && token->special == SmithLangGen_Plain &&
           smith_lang_equals(smith_lang_gt_str(token), token->length, literal);
}

static bool smith_lang_gt_punct(const SmithLangGenToken *token, const char *literal) {
    return smith_lang_gt_is(token, SmithTokenKind_Punctuator, literal);
}

static bool smith_lang_gt_open(const SmithLangGenToken *token) {
    return token->kind == SmithTokenKind_Punctuator && token->length == 1 &&
           strchr("([{", smith_lang_gt_str(token)[0]) != nullptr;
}

static bool smith_lang_gt_close(const SmithLangGenToken *token) {
    return token->kind == SmithTokenKind_Punctuator && token->length == 1 &&
           strchr(")]}", smith_lang_gt_str(token)[0]) != nullptr;
}

// The T of a generic, by name; its index, or -1.
static int smith_lang_param_named(const SmithLangGeneric *generic, const char *name, size_t length) {
    for (size_t k = 0; k < generic->type_count; k++) {             // bounded by the parameters
        if (generic->type_kind[k] == SmithLangSlot_Auto) continue; // an `auto x` has no name for its type
        const SmithLangGenToken *token = &smith_lang_gt.tokens[generic->first + generic->type_token[k]];
        if (token->length == length && memcmp(smith_lang_gt_str(token), name, length) == 0) return (int)k;
    }
    return -1;
}

static bool smith_lang_is_type_param(const SmithLang *lang, const char *name, size_t length) {
    return lang->recording && smith_lang_param_named(&smith_lang_generics[lang->recording_generic], name, length) >= 0;
}

// runtime/lang_tuple.c and runtime/lang_slice.c, which come after this file
static bool smith_lang_tuple_spell(SmithLang *lang, const SmithToken *span, size_t from, size_t to, char *out,
                                   size_t capacity, const char **problem, uint32_t *where);
static void smith_lang_slice_tag(const char *spelled, char *tag, size_t capacity);

// The type parameter a slice's tag is made of: `smith_array_T_n` in `typeof(T[:]) push_slice(...)`, whose struct is
// named after its element. The token is marked as a slice tag (the instance spells the tag of the type it was asked
// for); -1 for any other token.
static int smith_lang_slice_tag_param(const SmithLangGeneric *generic, SmithLangGenToken *token) {
    static const char prefix_text[] = "smith_array_"; // runtime/lang_slice.c's smith_lang_slice_prefix
    size_t prefix = sizeof prefix_text - 1;
    const char *text = smith_lang_gt_str(token);
    if (token->length <= prefix + 2 || memcmp(text, prefix_text, prefix) != 0 ||
        memcmp(text + token->length - 2, "_n", 2) != 0) {
        return -1;
    }
    int found = smith_lang_param_named(generic, text + prefix, token->length - prefix - 2);
    if (found >= 0) token->flags |= (uint8_t)smith_lang_gen_flag_slice;
    return found;
}

// A token that names the T of the generic being recorded is marked, unless it is a member (`x.T`).
static void smith_lang_capture_mark(SmithLang *lang, uint32_t index) {
    const SmithLangGeneric *generic = &smith_lang_generics[lang->recording_generic];
    SmithLangGenToken *token = &smith_lang_gt.tokens[index];
    int found = smith_lang_param_named(generic, smith_lang_gt_str(token), token->length);
    if (found < 0) found = smith_lang_slice_tag_param(generic, token);
    if (found < 0) return;
    if (index > generic->first) {
        const SmithLangGenToken *before = &smith_lang_gt.tokens[index - 1];
        if (smith_lang_gt_punct(before, ".") || smith_lang_gt_punct(before, "->")) return;
    }
    token->param = (uint8_t)(found + 1);
}

// The names of the `auto` parameters (a token each; length 0 for a type that has none), copied: the head moves while it
// is folded.
typedef struct {
    SmithLangGenToken names[smith_lang_type_params_max];
} SmithLangAutoNames;

static SmithLangAutoNames smith_lang_auto_names(const SmithLangGeneric *generic) {
    SmithLangAutoNames out = {};
    for (size_t k = 0; k < generic->type_count; k++) { // bounded by the parameters
        if (generic->type_kind[k] == SmithLangSlot_Auto)
            out.names[k] = smith_lang_gt.tokens[generic->first + generic->type_token[k] + 1];
    }
    return out;
}

// Whether the four tokens at `at` are `typeof ( x )` with x an `auto` parameter: its type's index, else -1.
static int smith_lang_typeof_auto(const SmithLangAutoNames *autos, uint32_t at, uint32_t limit) {
    if (at + 4 > limit) return -1;
    const SmithLangGenToken *tokens = &smith_lang_gt.tokens[at];
    bool keyword = smith_lang_gt_is(&tokens[0], SmithTokenKind_Identifier, "typeof") ||
                   smith_lang_gt_is(&tokens[0], SmithTokenKind_Identifier, "__typeof__");
    if (!keyword || !smith_lang_gt_punct(&tokens[1], "(") || tokens[2].kind != SmithTokenKind_Identifier ||
        tokens[2].special != SmithLangGen_Plain || !smith_lang_gt_punct(&tokens[3], ")")) {
        return -1;
    }
    for (size_t k = 0; k < smith_lang_type_params_max; k++) { // the auto parameters
        const SmithLangGenToken *name = &autos->names[k];
        if (name->length == tokens[2].length && name->length > 0 &&
            memcmp(smith_lang_gt_str(name), smith_lang_gt_str(&tokens[2]), name->length) == 0) {
            return (int)k;
        }
    }
    return -1;
}

// The one token that stands for `typeof ( x )`: it names the `auto` parameter's type (the instance writes the type
// for it), and its text is what hover shows. Appended after the pool's end and taken off again, which keeps its text.
static bool smith_lang_typeof_token(const SmithLangGenToken *keyword, const SmithLangGenToken *name, int type,
                                    SmithLangGenToken *out) {
    char text[smith_lang_name_max + 16];
    int length = snprintf(text, sizeof text, "typeof(%.*s)", (int)name->length, smith_lang_gt_str(name));
    uint32_t added = smith_lang_pool_add(&smith_lang_gt, *keyword, text, (size_t)length);
    if (added == UINT32_MAX) return false;
    *out = smith_lang_gt.tokens[added];
    out->param = (uint8_t)(type + 1);
    smith_lang_gt.count = added;
    return true;
}

// The head just recorded, at the end of the pool: each `typeof(x)` of an `auto` parameter becomes one token. The name
// moves down by the tokens taken out before it. False when the pool is full.
static bool smith_lang_generic_fold_head(SmithLangGeneric *generic) {
    SmithLangAutoNames autos = smith_lang_auto_names(generic);
    uint32_t write = 0;
    uint32_t name_shift = 0;
    for (uint32_t read = 0; read < generic->count; write++) { // each token of the head
        int type = smith_lang_typeof_auto(&autos, generic->first + read, generic->first + generic->count);
        SmithLangGenToken kept = smith_lang_gt.tokens[generic->first + read];
        if (type >= 0) {
            if (!smith_lang_typeof_token(&kept, &smith_lang_gt.tokens[generic->first + read + 2], type, &kept)) {
                return false;
            }
            if (read < generic->name_index) name_shift += 3;
            read += 3;
        }
        smith_lang_gt.tokens[generic->first + write] = kept;
        read += 1;
    }
    generic->count = write;
    generic->name_index -= name_shift;
    smith_lang_gt.count = generic->first + write;
    return true;
}

// A `)` was just recorded in a generic's body: when it ends `typeof(x)` of an `auto` parameter, the four tokens become
// one.
static void smith_lang_capture_fold(SmithLang *lang) {
    const SmithLangGeneric *generic = &smith_lang_generics[lang->recording_generic];
    uint32_t count = smith_lang_gt.count;
    if (generic->type_count == 0 || count < generic->first + 4) return;
    SmithLangAutoNames autos = smith_lang_auto_names(generic);
    int type = smith_lang_typeof_auto(&autos, count - 4, count);
    if (type < 0) return;
    SmithLangGenToken folded;
    if (!smith_lang_typeof_token(&smith_lang_gt.tokens[count - 4], &smith_lang_gt.tokens[count - 2], type, &folded)) {
        lang->broken = true;
        return;
    }
    smith_lang_gt.tokens[count - 4] = folded;
    smith_lang_gt.count = count - 3;
}

// Whether the parameter is `constexpr type T`: three tokens.
static bool smith_lang_param_is_type(const SmithLangParam *param) {
    const SmithLangGenToken *first = &smith_lang_gt.tokens[param->start];
    return param->end - param->start == 3 && smith_lang_gt_is(first, SmithTokenKind_Identifier, "constexpr") &&
           smith_lang_gt_is(first + 1, SmithTokenKind_Identifier, "type") && first[2].kind == SmithTokenKind_Identifier;
}

// Whether the parameter is a bare `type T`, which is an error: the owner marks every compile-time parameter constexpr.
static bool smith_lang_param_is_bare_type(const SmithLangParam *param) {
    const SmithLangGenToken *first = &smith_lang_gt.tokens[param->start];
    return param->end - param->start == 2 && smith_lang_gt_is(first, SmithTokenKind_Identifier, "type") &&
           first[1].kind == SmithTokenKind_Identifier;
}

// Whether the parameter is `auto x`: two tokens. Its type is the argument's, and `typeof(x)` names it.
static bool smith_lang_param_is_auto(const SmithLangParam *param) {
    const SmithLangGenToken *first = &smith_lang_gt.tokens[param->start];
    return param->end - param->start == 2 && smith_lang_gt_is(first, SmithTokenKind_Identifier, "auto") &&
           first[1].kind == SmithTokenKind_Identifier && first[1].special == SmithLangGen_Plain;
}

// Whether the parameter is `constexpr auto f`: three tokens. The argument is a function known at compile time, and each
// distinct function is an instance.
static bool smith_lang_param_is_function(const SmithLangParam *param) {
    const SmithLangGenToken *first = &smith_lang_gt.tokens[param->start];
    return param->end - param->start == 3 && smith_lang_gt_is(first, SmithTokenKind_Identifier, "constexpr") &&
           smith_lang_gt_is(first + 1, SmithTokenKind_Identifier, "auto") &&
           first[2].kind == SmithTokenKind_Identifier && first[2].special == SmithLangGen_Plain;
}

// Whether the parameter is `auto... rest`: three tokens, the last parameter. Each argument that remains has its own
// type.
static bool smith_lang_param_is_pack(const SmithLangParam *param) {
    const SmithLangGenToken *first = &smith_lang_gt.tokens[param->start];
    return param->end - param->start == 3 && smith_lang_gt_is(first, SmithTokenKind_Identifier, "auto") &&
           smith_lang_gt_punct(first + 1, "...") && first[2].kind == SmithTokenKind_Identifier &&
           first[2].special == SmithLangGen_Plain;
}

// The parameters of a generic: the tokens between the parentheses, split at the commas that stand outside any
// brackets. Returns how many; SIZE_MAX when there are more than `capacity`.
static size_t smith_lang_generic_params(const SmithLangGeneric *generic, SmithLangParam *out, size_t capacity) {
    size_t count = 0;
    uint32_t start = generic->first + generic->open_index + 1;
    uint32_t end = generic->first + generic->close_index;
    if (start == end) return 0;
    int depth = 0;
    for (uint32_t at = start; at <= end; at++) { // bounded by the head
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        bool split = at == end || (depth == 0 && smith_lang_gt_punct(token, ","));
        if (!split) {
            depth += smith_lang_gt_open(token) ? 1 : smith_lang_gt_close(token) ? -1 : 0;
            continue;
        }
        if (count >= capacity) return SIZE_MAX;
        out[count++] = (SmithLangParam){start, at, -1, SmithLangSlot_Plain};
        start = at + 1;
    }
    for (size_t i = 0; i < count; i++) { // `constexpr type T`, `constexpr auto f`, `auto x`, `auto... rest`
        const SmithLangGenToken *first = &smith_lang_gt.tokens[out[i].start];
        out[i].kind = SmithLangSlot_Plain;
        if (smith_lang_param_is_type(&out[i]) || smith_lang_param_is_function(&out[i])) {
            out[i].kind = smith_lang_param_is_type(&out[i]) ? SmithLangSlot_Type : SmithLangSlot_Function;
            out[i].type_index = (int8_t)smith_lang_param_named(generic, smith_lang_gt_str(first + 2), first[2].length);
        } else if (smith_lang_param_is_auto(&out[i])) {
            out[i].kind = SmithLangSlot_Auto;
        } else if (smith_lang_param_is_pack(&out[i])) {
            out[i].kind = SmithLangSlot_Pack;
        }
    }
    return count;
}

// ---- Seeing that a declaration is generic
// -----------------------------------------------------------------------------

// Whether a declaration that begins with this token may be generic: only the text of the file decides for the host's
// fast path, which asks before it has read anything.
static bool smith_lang_generic_candidate(const SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    if (!lang->is_module || !host->maybe_generic || lang->broken) return false;
    if (smith_lang_punct(first, "[")) return true;
    if (first->kind != SmithTokenKind_Identifier) return false;
    return smith_lang_word(first->text, first->length) != SmithLangWord_Typedef &&
           !smith_lang_ident(first, "static_assert") && !smith_lang_ident(first, "_Static_assert");
}

// A keyword that is followed by parentheses which are no parameter list.
static bool smith_lang_group_keyword(const SmithToken *token) {
    static const char *const words[] = {"__attribute__", "__attribute", "typeof",   "typeof_unqual", "__typeof__",
                                        "_Alignas",      "alignas",     "_Atomic",  "_BitInt",       "__asm__",
                                        "__asm",         "asm",         "_Generic", "sizeof",        "alignof",
                                        "_Alignof",      "__declspec"};
    if (token->kind != SmithTokenKind_Identifier) return false;
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) { // 17 entries
        if (smith_lang_equals(token->text, token->length, words[i])) return true;
    }
    return false;
}

// Whether the token read ahead at `index` is `type`, a name and then `,` or `)`. (`constexpr` before it is the correct
// spelling; a bare `type T` is found too, so that the error can say what to write.)
static bool smith_lang_type_parameter_at(SmithLang *lang, const SmithHost *host, size_t index) {
    const SmithToken *name = smith_lang_peek(lang, host, index + 1, false);
    if (name->kind != SmithTokenKind_Identifier || smith_lang_is_word(name)) return false;
    const SmithToken *end = smith_lang_peek(lang, host, index + 2, false);
    return smith_lang_punct(end, ",") || smith_lang_punct(end, ")");
}

// Whether the token read ahead at `index` is `auto`, then `...`, a name and `,` or `)`: `auto... rest`.
static bool smith_lang_pack_parameter_at(SmithLang *lang, const SmithHost *host, size_t index) {
    if (!smith_lang_punct(smith_lang_peek(lang, host, index + 1, false), "...")) return false;
    const SmithToken *name = smith_lang_peek(lang, host, index + 2, false);
    if (name->kind != SmithTokenKind_Identifier || smith_lang_is_word(name)) return false;
    const SmithToken *end = smith_lang_peek(lang, host, index + 3, false);
    return smith_lang_punct(end, ",") || smith_lang_punct(end, ")");
}

// Whether the bare `type T` at `index` (a parameter that is no `constexpr type T`) names a type of the head: the result
// type (the identifiers `before` it, outside the parameters) or a later parameter's type is `T`. Then it was meant as a
// type parameter, and the error says what to write; else it is a type value (runtime/lang_type.c), a parameter of an
// ordinary function.
static bool smith_lang_bare_type_used(SmithLang *lang, const SmithHost *host, const SmithToken *const *result,
                                      size_t result_count, size_t index) {
    const SmithToken *name = smith_lang_peek(lang, host, index + 1, false);
    for (size_t i = 0; i < result_count; i++) { // the result type
        if (result[i]->length == name->length && memcmp(result[i]->text, name->text, name->length) == 0) return true;
    }
    int depth = 1;
    for (size_t at = index + 2; depth > 0 && at < index + smith_lang_generic_scan_max; at++) { // bounded by the head
        const SmithToken *token = smith_lang_peek(lang, host, at, false);
        if (token->kind == SmithTokenKind_Eof) break;
        depth += smith_lang_punct(token, "(") ? 1 : smith_lang_punct(token, ")") ? -1 : 0;
        if (token->kind == SmithTokenKind_Identifier && token->length == name->length &&
            memcmp(token->text, name->text, name->length) == 0) {
            return true;
        }
    }
    return false;
}

// The `)` read ahead at `index` closes the result type's call, `slice_of(T, k) name(`: a name and its `(` follow
// (a parameter list never has a name after it but an attribute, a group keyword).
static bool smith_lang_computed_return_follows(SmithLang *lang, const SmithHost *host, size_t index) {
    const SmithToken *name = smith_lang_peek(lang, host, index + 1, false);
    return name->kind == SmithTokenKind_Identifier && !smith_lang_group_keyword(name) && !smith_lang_is_word(name) &&
           smith_lang_punct(smith_lang_peek(lang, host, index + 2, false), "(");
}

static void smith_lang_emit_pieces(SmithLang *lang, const SmithHost *host, const SmithLangPiece *pieces, size_t count,
                                   const SmithToken *like, uint32_t instance);

// The parameter list ended at the token read ahead at `index`, with a bare `type` in the result or a parameter: a
// function that runs in the compiler (issue #107). When it has a body it is `plain`: the header pass records it like a
// generic (an importer defines it, so a function there can call it at compile time), and the module's own compile
// writes it as a private function. A prototype is no record.
static bool smith_lang_plain_ahead(SmithLang *lang, const SmithHost *host, const SmithToken *first, bool over_type,
                                   size_t index) {
    if (!over_type || !smith_lang_punct(smith_lang_peek(lang, host, index + 1, false), "{")) return false;
    if (lang->tee ||
        host->type_query == nullptr) { // the header pass; the raw lowering writes no `type` for a C compiler
        lang->generic_plain = true;
        return true;
    }
    lang->pending_export = false; // it exists only at compile time: not a symbol, not a name for C
    // Unused is no fault of the module's (a function over types that only importers call): `[[maybe_unused]]` first.
    static const SmithLangPiece unused[] = {{SmithTokenKind_Punctuator, "["},
                                            {SmithTokenKind_Punctuator, "["},
                                            {SmithTokenKind_Identifier, "maybe_unused"},
                                            {SmithTokenKind_Punctuator, "]"},
                                            {SmithTokenKind_Punctuator, "]"}};
    smith_lang_emit_pieces(lang, host, unused, sizeof unused / sizeof unused[0], first, 0);
    return false;
}

// Reads ahead, to the end of the parameter list of the function the declaration begins with: is there a `type T` in it.
// The tokens stay queued; nothing is changed.
static bool smith_lang_generic_ahead(SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    if (!lang->is_module || !host->maybe_generic || lang->broken) return false;
    int depth = 0;
    bool in_params = false;
    bool over_type = false; // the result or a parameter is a bare `type`: an ordinary function over types (issue #107)
    const SmithToken *before = first;
    const SmithToken *result[8] = {first}; // the identifiers before the parameters: the result type, the name
    size_t result_count = 1;
    for (size_t index = 0; index < smith_lang_generic_scan_max; index++) { // bounded: a head is short
        const SmithToken *token = smith_lang_peek(lang, host, index, false);
        if (!in_params && depth == 0 && token->kind == SmithTokenKind_Identifier && result_count < 8) {
            result[result_count++] = token;
            over_type = over_type || smith_lang_ident(token, "type");
        }
        if (token->kind == SmithTokenKind_Eof) return false;
        if (token->kind == SmithTokenKind_Punctuator && token->length == 1) {
            char c = token->text[0];
            if (c == '(') {
                in_params = in_params || (depth == 0 && !smith_lang_group_keyword(before));
                depth += 1;
            } else if (c == ')') {
                depth -= 1;
                if (depth == 0 && in_params && smith_lang_computed_return_follows(lang, host, index)) {
                    in_params = false; // `slice_of(T, k)` was the result type: the parameter list is next
                    lang->generic_result_call = true;
                } else if (depth == 0 && in_params) {
                    // The parameter list ended without one: a function over types, with a body, is plain.
                    return smith_lang_plain_ahead(lang, host, first, over_type, index);
                }
            } else if (depth == 0 && (c == ';' || c == '=' || c == '{' || c == ',')) {
                return false;
            }
        }
        // A bare `type t` is a type value (runtime/lang_type.c), a parameter of an ordinary function.
        bool bare_value = in_params && depth == 1 && (smith_lang_punct(before, "(") || smith_lang_punct(before, ",")) &&
                          smith_lang_ident(token, "type") && smith_lang_type_parameter_at(lang, host, index);
        over_type = over_type || bare_value;
        bool bare = (smith_lang_punct(before, "(") || smith_lang_punct(before, ",")) &&
                    smith_lang_ident(token, "type") &&
                    smith_lang_bare_type_used(lang, host, result, result_count, index);
        if (in_params && depth == 1 && smith_lang_ident(token, "type") &&
            (smith_lang_ident(before, "constexpr") || bare) && smith_lang_type_parameter_at(lang, host, index)) {
            return host->lookup(host->context, "type", 4, SmithLookup_Type).kind == SmithLookup_None;
        }
        bool starts_parameter = smith_lang_punct(before, "(") || smith_lang_punct(before, ",");
        bool after_constexpr = smith_lang_ident(before, "constexpr");
        if (in_params && depth == 1 && smith_lang_ident(token, "auto") && (starts_parameter || after_constexpr) &&
            (smith_lang_type_parameter_at(lang, host, index) ||
             (starts_parameter && smith_lang_pack_parameter_at(lang, host, index)))) {
            return true; // `auto x`, `constexpr auto f`, `auto... rest`
        }
        before = token;
    }
    return false;
}

// ---- Recording the definition
// ------------------------------------------------------------------------------------------

static void smith_lang_generic_discard(SmithLang *lang) {
    lang->decl.generic = false;
    smith_lang_end_declaration(lang);
}

static uint32_t smith_lang_generic_skip_attributes(const SmithLangGeneric *generic);

// The result type is written before the parameters, where C does not know them: `typeof(*x) gradient(auto x)` has no
// `x` there. Each `auto` parameter's name in a `typeof(...)` of the result type stands for a value of its type, written
// `(*(T *)0)` (never evaluated), so any expression of the parameter has the type it would have.
static void smith_lang_generic_mark_values(const SmithLangGeneric *generic) {
    SmithLangAutoNames autos = smith_lang_auto_names(generic);
    uint32_t end = smith_lang_generic_skip_attributes(generic);
    for (uint32_t at = end; at + 1 < generic->name_index; at++) { // the result type, to the name
        SmithLangGenToken *keyword = &smith_lang_gt.tokens[generic->first + at];
        bool is_typeof = smith_lang_gt_is(keyword, SmithTokenKind_Identifier, "typeof") ||
                         smith_lang_gt_is(keyword, SmithTokenKind_Identifier, "typeof_unqual") ||
                         smith_lang_gt_is(keyword, SmithTokenKind_Identifier, "__typeof__");
        if (!is_typeof || !smith_lang_gt_punct(keyword + 1, "(")) continue;
        uint32_t close = at + 1;
        for (int depth = 0; close < generic->name_index; close++) { // to the matching parenthesis
            depth += smith_lang_gt_punct(keyword + (close - at), "(") ? 1 : 0;
            depth -= smith_lang_gt_punct(keyword + (close - at), ")") ? 1 : 0;
            if (depth == 0) break;
        }
        for (uint32_t inner = at + 2; inner < close; inner++) { // the names inside
            SmithLangGenToken *token = &smith_lang_gt.tokens[generic->first + inner];
            bool member = smith_lang_gt_punct(token - 1, ".") || smith_lang_gt_punct(token - 1, "->");
            for (size_t k = 0; token->kind == SmithTokenKind_Identifier && !member && k < smith_lang_type_params_max;
                 k++) { // the auto parameters
                const SmithLangGenToken *name = &autos.names[k];
                if (name->length == token->length && name->length > 0 &&
                    memcmp(smith_lang_gt_str(name), smith_lang_gt_str(token), name->length) == 0) {
                    token->param = (uint8_t)(k + 1);
                    token->flags |= (uint8_t)smith_lang_gen_flag_value;
                }
            }
        }
        at = close;
    }
}

// The type parameters of the head just recorded: `constexpr type T`, their names and which parameter each is. A
// message, or null.
static const char *smith_lang_generic_scan(SmithLangGeneric *generic) {
    SmithLangParam params[smith_lang_call_params_max];
    size_t count = smith_lang_generic_params(generic, params, smith_lang_call_params_max);
    if (count == SIZE_MAX) return "a generic function has at most 16 parameters";
    generic->param_count = (uint8_t)count;
    generic->type_count = 0;
    memset(generic->type_kind, 0, sizeof generic->type_kind);
    for (size_t i = 0; i < count; i++) { // the compile-time parameters
        const SmithLangGenToken *first = &smith_lang_gt.tokens[params[i].start];
        if (smith_lang_param_is_bare_type(&params[i]) && !generic->plain)
            return "a type parameter is compile-time: write `constexpr type T`";
        if (params[i].kind == SmithLangSlot_Auto) {
            if (generic->type_count >= smith_lang_type_params_max)
                return "a generic function has at most 4 `constexpr type`, `constexpr auto` and `auto` parameters";
            smith_lang_gt.tokens[params[i].start].param = (uint8_t)(generic->type_count + 1);
            generic->type_token[generic->type_count] = (uint8_t)(params[i].start - generic->first);
            generic->type_position[generic->type_count] = (uint8_t)i;
            generic->type_kind[generic->type_count] = SmithLangSlot_Auto;
            generic->type_count += 1;
            continue;
        }
        if (params[i].kind == SmithLangSlot_Function || params[i].kind == SmithLangSlot_Pack) {
            if (generic->type_count >= smith_lang_type_params_max)
                return "a generic function has at most 4 `constexpr type`, `constexpr auto` and `auto` parameters";
            if (params[i].kind == SmithLangSlot_Pack && i + 1 != count)
                return "`auto...` is the last parameter: the arguments that remain have no other parameter after them";
            if (smith_lang_param_named(generic, smith_lang_gt_str(first + 2), first[2].length) >= 0)
                return "two compile-time parameters have the same name";
            generic->type_token[generic->type_count] = (uint8_t)(params[i].start + 2 - generic->first);
            generic->type_position[generic->type_count] = (uint8_t)i;
            generic->type_kind[generic->type_count] = (uint8_t)params[i].kind;
            generic->type_count += 1;
            continue;
        }
        if (params[i].kind != SmithLangSlot_Type) continue;
        generic->type_kind[generic->type_count] = SmithLangSlot_Type;
        first += 1;
        if (generic->type_count >= smith_lang_type_params_max)
            return "a generic function has at most 4 `constexpr type` parameters";
        if (smith_lang_word(smith_lang_gt_str(first + 1), first[1].length) != SmithLangWord_Other) {
            return "a keyword cannot name a type parameter";
        }
        if (smith_lang_param_named(generic, smith_lang_gt_str(first + 1), first[1].length) >= 0) {
            return "two type parameters have the same name";
        }
        generic->type_token[generic->type_count] = (uint8_t)(params[i].start + 2 - generic->first);
        generic->type_position[generic->type_count] = (uint8_t)i;
        generic->type_count += 1;
    }
    if (generic->type_count == 0 && !generic->plain)
        return "a generic function needs a `constexpr type T`, `constexpr auto f` or `auto` parameter";
    smith_lang_generic_mark_values(generic);
    return nullptr;
}

static bool smith_lang_held_is_inline(const SmithLangHeld *held) {
    const char *text = smith_lang_scratch_str(held->text);
    return held->kind == SmithTokenKind_Identifier && (smith_lang_equals(text, held->text.length, "inline") ||
                                                       smith_lang_equals(text, held->text.length, "__inline") ||
                                                       smith_lang_equals(text, held->text.length, "__inline__"));
}

// A name written `x::name` is part of the body of the generic being recorded, whose record the module's header holds
// (and its importers compile): the header must have what it names.
// An exported generic's names are the header's at once; a private generic's wait, because the header has its record
// only if an exported generic calls it (lang_private.c applies them then).
static void smith_lang_generic_note(SmithLang *lang, SmithLangGeneric *generic, uint16_t via, const char *name,
                                    size_t length, bool tag) {
    if (generic->exported) {
        smith_lang_note_use(lang, via, name, length, tag);
        return;
    }
    if (lang->priv_notes_count >= smith_lang_priv_notes_max) return;
    if (generic->note_count == 0) generic->note_first = (uint32_t)lang->priv_notes_count;
    SmithLangPrivNote *note = &lang->priv_notes[lang->priv_notes_count++];
    note->via = via;
    note->tag = tag;
    note->name = smith_lang_store(lang, name, length);
    generic->note_count += 1;
}

static void smith_lang_generic_note_capture(SmithLang *lang, const SmithToken *token) {
    if (!lang->recording || lang->recording_skip) return;
    bool tag = false;
    if (smith_lang_gt.count > 0) {
        const SmithLangGenToken *before = &smith_lang_gt.tokens[smith_lang_gt.count - 1];
        tag = smith_lang_tag_before(smith_lang_gt_str(before), before->length);
    }
    smith_lang_generic_note(lang, &smith_lang_generics[lang->recording_generic], token->via, token->text, token->length,
                            tag);
}

static void smith_lang_generic_note_binding(SmithLang *lang, uint16_t via) {
    smith_lang_generic_note(lang, &smith_lang_generics[lang->recording_generic], via, "", 0, false);
}

// The call or the name of the generic being recorded is `known`'s (a private function, type or generic of the module).
static void smith_lang_generic_use(SmithLang *lang, const SmithLangName *known) {
    if (!lang->recording || lang->recording_skip || known->exported || lang->gen_uses_count >= smith_lang_gen_uses_max)
        return;
    lang->gen_uses[lang->gen_uses_count++] =
        (SmithLangGenUse){.generic = lang->recording_generic, .name = (uint32_t)(known - lang->names)};
}

// A body that calls a generic of its own module (written bare, or as `module::name`): a private one is used.
static void smith_lang_generic_use_member(SmithLang *lang, const char *module, size_t module_length, const char *member,
                                          size_t member_length) {
    if (!lang->recording || !lang->is_module || strlen(lang->module) != module_length ||
        memcmp(lang->module, module, module_length) != 0) {
        return;
    }
    const SmithLangName *known = smith_lang_name_find(lang, member, member_length);
    if (known != nullptr) smith_lang_generic_use(lang, known);
}

// Pushes the head's tokens into the pool; false when it is full.
static bool smith_lang_generic_push_head(SmithLang *lang, SmithLangGeneric *generic) {
    uint32_t pushed = 0;
    for (size_t i = 0; i < lang->head_count; i++) { // bounded by the head
        const SmithLangHeld *held = &lang->head[i];
        if (smith_lang_held_is_inline(held)) continue;
        if (i == lang->decl.last_name) generic->name_index = pushed;
        SmithToken token = smith_lang_held_token(lang, held);
        if (held->via != 0) { // the head of a generic is in its header's record
            bool tag = i > 0 && smith_lang_tag_before(smith_lang_scratch_str(lang->head[i - 1].text),
                                                      lang->head[i - 1].text.length);
            smith_lang_generic_note(lang, generic, held->via, token.text, token.length, tag);
        }
        if (smith_lang_pool_add(&smith_lang_gt, smith_lang_gt_from(&token), token.text, token.length) == UINT32_MAX) {
            return false;
        }
        pushed += 1;
    }
    generic->count = pushed;
    return true;
}

// `(` after the name and its `)`, as indexes of the recorded tokens; false if the name is not followed by a parameter
// list.
static bool smith_lang_generic_parens(SmithLangGeneric *generic) {
    uint32_t at = generic->name_index + 1;
    if (at >= generic->count || !smith_lang_gt_punct(&smith_lang_gt.tokens[generic->first + at], "(")) return false;
    generic->open_index = at;
    int depth = 0;
    for (; at < generic->count; at++) { // to the matching parenthesis
        const SmithLangGenToken *token = &smith_lang_gt.tokens[generic->first + at];
        depth += smith_lang_gt_punct(token, "(") ? 1 : smith_lang_gt_punct(token, ")") ? -1 : 0;
        if (depth == 0) {
            generic->close_index = at;
            return true;
        }
    }
    return false;
}

// What is wrong with the declaration that was taken for a generic, or null.
static const char *smith_lang_generic_problem(const SmithLang *lang, const SmithToken *terminator, bool named) {
    if (!lang->decl.is_function || !named || lang->decl.declared != 1) {
        return "a generic is a function that declares one name";
    }
    if (!smith_lang_punct(terminator, "{")) {
        return "a generic function has a body: the compiler writes it for each type it is called with";
    }
    if (lang->decl.storage && !lang->decl.plain)
        return "a generic function is neither `static` nor `extern`; `export` is what makes it public";
    if (lang->generics_count >= smith_lang_generics_max) return "this file has more generic functions than smith keeps";
    return nullptr;
}

static void smith_lang_generic_enter_body(SmithLang *lang, const SmithHost *host, const SmithToken *terminator) {
    smith_lang_flush_kept(lang, host);
    lang->head_count = 0;
    lang->scratch_used = 0;
    lang->pending = SIZE_MAX;
    lang->paren_depth = 0;
    lang->bracket_depth = 0;
    lang->recording = true;
    lang->export_active = false;
    smith_lang_emit(lang, host, *terminator); // the `{`, in the pool
    lang->state = SmithLangState_Stream;
    lang->prev_char = '{';
    lang->prev_member = false;
    lang->prev_tag = false;
    lang->typeish = false;
    lang->prev_name_length = 0;
    smith_lang_push_brace(lang, host, SmithLangBrace_Function, terminator->location);
    lang->in_function = true;
    lang->decl.checking = false;
}

// The head of the declaration is complete and its terminator is the `{` of the body: the head is recorded and the body
// is read into the pool, not written. (A generic with no body, or anything else the terminator says, is the error.)
static void smith_lang_generic_flush_head(SmithLang *lang, const SmithHost *host, const SmithToken *terminator) {
    uint32_t location = lang->head[0].location;
    SmithLangName *name = nullptr;
    if (lang->decl.last_name < lang->head_count) {
        const SmithLangHeld *held = &lang->head[lang->decl.last_name];
        name = smith_lang_name_find(lang, smith_lang_scratch_str(held->original), held->original.length);
    }
    const char *problem = smith_lang_generic_problem(lang, terminator, name != nullptr);
    SmithLangGeneric *generic = &smith_lang_generics[lang->generics_count];
    *generic = (SmithLangGeneric){};
    generic->first = smith_lang_gt.count;
    generic->exported = lang->decl.exported; // before its head is pushed: the names it writes through imports
    generic->plain = lang->decl.plain;
    if (problem == nullptr) {
        if (!smith_lang_generic_push_head(lang, generic) || !smith_lang_generic_parens(generic)) {
            problem = "a generic is a function with a parameter list after its name";
        } else {
            problem = smith_lang_generic_scan(generic);
            if (problem == nullptr && !smith_lang_generic_fold_head(generic))
                problem = "this generic's head is too long";
            if (problem == nullptr && !smith_lang_generic_parens(generic)) {
                problem = "a generic is a function with a parameter list after its name";
            }
            if (problem == nullptr) problem = smith_lang_generic_scan(generic);
        }
    }
    if (problem != nullptr) {
        smith_lang_error(lang, host, location, "%s", problem);
        if (smith_lang_gt.count > generic->first && generic->first >= smith_lang_gt.count - lang->head_count) {
            smith_lang_gt.count = generic->first;
        }
        if (smith_lang_punct(terminator, "{")) { // the body is read and dropped, so that it adds no error to this one
            lang->recording_skip = true;
            lang->recording_generic = (uint32_t)lang->generics_count;
            lang->skip_mark = smith_lang_gt.count;
            smith_lang_generic_enter_body(lang, host, terminator);
            return;
        }
        smith_lang_generic_discard(lang);
        return;
    }
    const SmithLangHeld *held = &lang->head[lang->decl.last_name];
    generic->is_inline = lang->decl.is_inline;
    generic->exported = lang->decl.exported;
    generic->own = true;
    generic->body_index = generic->count;
    generic->module = lang->module_text;
    generic->member = smith_lang_store(lang, smith_lang_scratch_str(held->original), held->original.length);
    generic->lowered = smith_lang_store(lang, smith_lang_scratch_str(held->text), held->text.length);
    const char *source = lang->main_file != nullptr ? lang->main_file : "";
    generic->source = smith_lang_store(lang, source, strlen(source));
    name->generic = (uint16_t)(lang->generics_count + 1);
    lang->recording_generic = (uint32_t)lang->generics_count;
    lang->generics_count += 1;
    for (uint32_t i = 0; i < generic->count; i++) { // the T of the head
        if (smith_lang_gt.tokens[generic->first + i].kind == SmithTokenKind_Identifier) {
            smith_lang_capture_mark(lang, generic->first + i);
        }
    }
    smith_lang_generic_enter_body(lang, host, terminator);
}

// ---- The placeholder
// -------------------------------------------------------------------------------------------------

// Writes pieces of text as tokens that are no source's, with the layout and place of `like`.
static void smith_lang_emit_pieces(SmithLang *lang, const SmithHost *host, const SmithLangPiece *pieces, size_t count,
                                   const SmithToken *like, uint32_t instance) {
    for (size_t i = 0; i < count; i++) { // the pieces
        SmithToken token = smith_lang_synth(pieces[i].kind, pieces[i].text, strlen(pieces[i].text), like);
        token.line_start = false;
        token.space_before = true;
        token.synthetic = true;
        token.instance = instance;
        smith_lang_emit_code(lang, host, token);
    }
}

// `[[gnu::unavailable("...")]] void arena_push(void);` where the generic stands: a declaration for hover and the
// outline, and an error if C calls it.
static void smith_lang_generic_placeholder(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic) {
    const SmithLangGenToken *name = &smith_lang_gt.tokens[generic->first + generic->name_index];
    char message[smith_lang_name_max * 2];
    snprintf(message, sizeof message, "\"%s::%s is %s\"", smith_lang_str(generic->module),
             smith_lang_str(generic->member),
             generic->plain ? "a function over `type`: it exists only at compile time"
                            : "a generic function: call it from a .smith module");
    SmithToken like = smith_lang_synth(SmithTokenKind_Identifier, "", 0, &(SmithToken){});
    like.location = name->location;
    like.line_start = true;
    static const SmithLangPiece before[] = {
        {SmithTokenKind_Punctuator, "["},           {SmithTokenKind_Punctuator, "["},
        {SmithTokenKind_Identifier, "clang"},       {SmithTokenKind_Punctuator, "::"},
        {SmithTokenKind_Identifier, "unavailable"}, {SmithTokenKind_Punctuator, "("}};
    static const SmithLangPiece after[] = {{SmithTokenKind_Punctuator, ")"},
                                           {SmithTokenKind_Punctuator, "]"},
                                           {SmithTokenKind_Punctuator, "]"},
                                           {SmithTokenKind_Identifier, "void"}};
    static const SmithLangPiece end[] = {{SmithTokenKind_Punctuator, "("},
                                         {SmithTokenKind_Identifier, "void"},
                                         {SmithTokenKind_Punctuator, ")"},
                                         {SmithTokenKind_Punctuator, ";"}};
    smith_lang_emit_pieces(lang, host, before, sizeof before / sizeof before[0], &like, 0);
    smith_lang_emit_pieces(lang, host, &(SmithLangPiece){SmithTokenKind_String, message}, 1, &like, 0);
    smith_lang_emit_pieces(lang, host, after, sizeof after / sizeof after[0], &like, 0);
    SmithToken token = smith_lang_synth(SmithTokenKind_Identifier, smith_lang_gt_str(name), name->length, &like);
    token.line_start = false;
    token.space_before = true;
    smith_lang_emit_code(lang, host, token); // the name, where the source has it
    smith_lang_emit_pieces(lang, host, end, sizeof end / sizeof end[0], &like, 0);
}

// The body has been read to its `}`: the record is complete.
static void smith_lang_generic_finish(SmithLang *lang) {
    const SmithHost *host = lang->host_now;
    lang->recording = false;
    if (lang->recording_skip) { // a generic with an error in its head: nothing of it is kept
        lang->recording_skip = false;
        smith_lang_gt.count = lang->skip_mark;
        return;
    }
    SmithLangGeneric *generic = &smith_lang_generics[lang->recording_generic];
    generic->count = smith_lang_gt.count - generic->first;
    smith_lang_generic_placeholder(lang, host, generic);
}

// ---- The record in a module's header
// ----------------------------------------------------------------------------------

constexpr char smith_lang_generic_marker[] = "// smith-generic: ";

// Appends `text` with the bytes that would break a word (space, `%`, control bytes) as %XX.
static size_t smith_lang_escape(char *out, size_t capacity, size_t used, const char *text, size_t length) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < length && used + 4 < capacity; i++) { // bounded by the text
        uint8_t c = (uint8_t)text[i];
        if (c <= ' ' || c == '%' || c >= 0x7f) {
            out[used++] = '%';
            out[used++] = digits[c >> 4];
            out[used++] = digits[c & 15];
        } else {
            out[used++] = (char)c;
        }
    }
    return used;
}

static size_t smith_lang_unescape(const char *text, size_t length, char *out, size_t capacity) {
    size_t used = 0;
    for (size_t i = 0; i < length && used + 1 < capacity; i++) { // bounded by the word
        if (text[i] == '%' && i + 2 < length) {
            char pair[3] = {text[i + 1], text[i + 2], 0};
            out[used++] = (char)strtol(pair, nullptr, 16);
            i += 2;
        } else {
            out[used++] = text[i];
        }
    }
    out[used] = 0;
    return used;
}

static char smith_lang_kind_letter(const SmithLangGenToken *token) {
    switch (token->special) {
    case SmithLangGen_CallBegin:
        return 'B';
    case SmithLangGen_CallEnd:
        return 'E';
    case SmithLangGen_IfConstexpr:
        return 'X';
    case SmithLangGen_ForConstexpr:
        return 'Y';
    case SmithLangGen_Plain:
        break;
    }
    switch ((SmithTokenKind)token->kind) {
    case SmithTokenKind_Identifier:
        return 'i';
    case SmithTokenKind_Number:
        return 'n';
    case SmithTokenKind_String:
        return 's';
    case SmithTokenKind_Character:
        return 'c';
    case SmithTokenKind_Punctuator:
    case SmithTokenKind_Eof:
        break;
    }
    return 'p';
}

// One line for an exported generic of the module: its tokens as an importer needs them, with where each was written
// (line and column of the module's source) so that a diagnostic in an instance points there.
[[gnu::format(printf, 4, 5)]] static size_t smith_lang_append(char *buffer, size_t capacity, size_t used,
                                                              const char *format, ...);

static size_t smith_lang_generic_serialize(const SmithLangGeneric *generic, char *out, size_t capacity, size_t used) {
    used = smith_lang_append(out, capacity, used, "%s", smith_lang_generic_marker);
    const SmithLangText words[] = {generic->module, generic->member, generic->lowered, generic->source};
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) { // module, member, lowered name, source file
        used = smith_lang_escape(out, capacity, used, smith_lang_str(words[i]), words[i].length);
        used = smith_lang_append(out, capacity, used, " ");
    }
    // i: inline, p: private (an exported generic calls it), q: both; f: a function over `type` (g: a private one)
    const char *kinds = generic->exported ? "-i" : "pq";
    char kind = generic->plain ? (generic->exported ? 'f' : 'g') : kinds[generic->is_inline ? 1 : 0];
    used = smith_lang_append(out, capacity, used, "%c %u %u %u %u |", kind, generic->name_index, generic->open_index,
                             generic->close_index, generic->body_index);
    for (uint32_t i = 0; i < generic->count; i++) { // the head, then the body
        const SmithLangGenToken *token = &smith_lang_gt.tokens[generic->first + i];
        used = smith_lang_append(out, capacity, used, " %c", smith_lang_kind_letter(token));
        if (token->param != 0) used = smith_lang_append(out, capacity, used, "P%u", token->param);
        if ((token->flags & smith_lang_gen_flag_expand) != 0) used = smith_lang_append(out, capacity, used, "x");
        if ((token->flags & smith_lang_gen_flag_space) != 0) used = smith_lang_append(out, capacity, used, "w");
        if ((token->flags & smith_lang_gen_flag_slice) != 0) used = smith_lang_append(out, capacity, used, "g");
        size_t line = smith_lang_lines_known() ? smith_lang_line_of(token->location) : 0;
        size_t column = smith_lang_lines_known() ? smith_lang_column_of(token->location) : 0;
        used = smith_lang_append(out, capacity, used, "@%zu:%zu=", line, column);
        used = smith_lang_escape(out, capacity, used, smith_lang_gt_str(token), token->length);
    }
    return smith_lang_append(out, capacity, used, "\n");
}

// The exported generics of the module, as lines of its header.
static size_t smith_lang_generic_lines(const SmithLang *lang, char *out, size_t capacity, size_t used) {
    for (size_t i = 0; i < lang->generics_count; i++) { // the module's own
        const SmithLangGeneric *generic = &smith_lang_generics[i];
        if (generic->own && (generic->exported || generic->needed))
            used = smith_lang_generic_serialize(generic, out, capacity, used);
    }
    return used;
}

// `// smith-types: slice_of small`: the functions over `type` the header records, which an importer defines (issue
// #107).
static size_t smith_lang_types_line(const SmithLang *lang, char *out, size_t capacity, size_t used) {
    bool any = false;
    for (size_t i = 0; i < lang->generics_count; i++) { // the module's own
        const SmithLangGeneric *generic = &smith_lang_generics[i];
        if (!generic->own || !generic->plain || !(generic->exported || generic->needed)) continue;
        used =
            smith_lang_append(out, capacity, used, any ? " %s" : "// smith-types: %s", smith_lang_str(generic->member));
        any = true;
    }
    return any ? smith_lang_append(out, capacity, used, "\n") : used;
}

typedef struct {
    const char *words[9];
    size_t lengths[9];
    const char *tokens;
    size_t tokens_length;
} SmithLangGenericLine;

// The words of a line before its `|`, and what follows it.
static bool smith_lang_generic_line_split(const char *line, size_t length, SmithLangGenericLine *out) {
    size_t at = 0;
    size_t count = 0;
    while (at < length && count < 10) { // bounded by the line
        while (at < length && line[at] == ' ')
            at += 1;
        size_t start = at;
        while (at < length && line[at] != ' ')
            at += 1;
        if (at == start) return false;
        if (line[start] == '|' && at - start == 1) {
            out->tokens = line + at;
            out->tokens_length = length - at;
            return count == 9;
        }
        if (count >= 9) return false;
        out->words[count] = line + start;
        out->lengths[count] = at - start;
        count += 1;
    }
    return false;
}

// One token of a line: `i`, `P1`, `x`, `w`, `@line:column=text`.
static bool smith_lang_generic_read_token(const char *word, size_t length, SmithLangGenToken *out, char *text,
                                          size_t capacity, size_t *text_length) {
    *out = (SmithLangGenToken){};
    out->location = UINT32_MAX;
    size_t at = 0;
    switch (word[at++]) {
    case 'i':
        out->kind = SmithTokenKind_Identifier;
        break;
    case 'n':
        out->kind = SmithTokenKind_Number;
        break;
    case 's':
        out->kind = SmithTokenKind_String;
        break;
    case 'c':
        out->kind = SmithTokenKind_Character;
        break;
    case 'p':
        out->kind = SmithTokenKind_Punctuator;
        break;
    case 'B':
        out->kind = SmithTokenKind_Identifier;
        out->special = SmithLangGen_CallBegin;
        break;
    case 'E':
        out->kind = SmithTokenKind_Identifier;
        out->special = SmithLangGen_CallEnd;
        break;
    case 'X':
        out->kind = SmithTokenKind_Identifier;
        out->special = SmithLangGen_IfConstexpr;
        break;
    case 'Y':
        out->kind = SmithTokenKind_Identifier;
        out->special = SmithLangGen_ForConstexpr;
        break;
    default:
        return false;
    }
    for (; at < length && word[at] != '@'; at++) { // the flags
        if (word[at] == 'P' && at + 1 < length) {
            out->param = (uint8_t)(word[++at] - '0');
        } else if (word[at] == 'x') {
            out->flags |= smith_lang_gen_flag_expand;
        } else if (word[at] == 'w') {
            out->flags |= smith_lang_gen_flag_space;
        } else if (word[at] == 'g') {
            out->flags |= smith_lang_gen_flag_slice;
        } else {
            return false;
        }
    }
    const char *equals = memchr(word + at, '=', length - at);
    if (at >= length || equals == nullptr) return false;
    out->line = (uint32_t)strtoul(word + at + 1, nullptr, 10);
    const char *colon = memchr(word + at, ':', (size_t)(equals - (word + at)));
    out->column = colon != nullptr ? (uint32_t)strtoul(colon + 1, nullptr, 10) : 0;
    *text_length = smith_lang_unescape(equals + 1, length - (size_t)(equals + 1 - word), text, capacity);
    return true;
}

static bool smith_lang_generic_read_tokens(const SmithLangGenericLine *parsed, SmithLangGeneric *generic) {
    const char *at = parsed->tokens;
    const char *end = parsed->tokens + parsed->tokens_length;
    char text[1024];
    generic->first = smith_lang_gt.count;
    uint32_t previous_line = UINT32_MAX;
    while (at < end) { // each token
        while (at < end && *at == ' ')
            at += 1;
        const char *start = at;
        while (at < end && *at != ' ')
            at += 1;
        if (at == start) break;
        SmithLangGenToken token;
        size_t length = 0;
        if (!smith_lang_generic_read_token(start, (size_t)(at - start), &token, text, sizeof text, &length)) {
            return false;
        }
        // The header says where each token was, not whether it began a line. The parser needs it (clang's
        // -Wmisleading-indentation reads a token's start of line), so a token on a later line than the one before it
        // begins one.
        if (token.line > previous_line) token.flags |= (uint8_t)smith_lang_gen_flag_line_start;
        previous_line = token.line;
        if (smith_lang_pool_add(&smith_lang_gt, token, text, length) == UINT32_MAX) return false;
    }
    generic->count = smith_lang_gt.count - generic->first;
    return generic->count > 0;
}

// The generic `member` of `module`; a function over `type` (plain) is no generic to call, unless `plain` asks for it.
static int smith_lang_generic_find_in(const SmithLang *lang, const char *module, size_t module_length,
                                      const char *member, size_t member_length, bool plain) {
    for (size_t i = 0; i < lang->generics_count; i++) { // the generics known
        const SmithLangGeneric *generic = &smith_lang_generics[i];
        if (generic->plain && !plain) continue;
        if (generic->module.length == module_length && generic->member.length == member_length &&
            memcmp(smith_lang_str(generic->module), module, module_length) == 0 &&
            memcmp(smith_lang_str(generic->member), member, member_length) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int smith_lang_generic_find(const SmithLang *lang, const char *module, size_t module_length, const char *member,
                                   size_t member_length) {
    return smith_lang_generic_find_in(lang, module, module_length, member, member_length, false);
}

// Reads the generics a header lists (`// smith-generic:` lines), once for each module.
static void smith_lang_generics_read(SmithLang *lang, const char *text, size_t length) {
    size_t marker = sizeof smith_lang_generic_marker - 1;
    for (size_t at = 0; at + marker < length;) { // each line
        const char *newline = memchr(text + at, '\n', length - at);
        size_t end = newline != nullptr ? (size_t)(newline - text) : length;
        if (end - at > marker && memcmp(text + at, smith_lang_generic_marker, marker) == 0 && lang->cap_frames == 0 &&
            lang->generics_count < smith_lang_generics_max) {
            SmithLangGenericLine parsed = {};
            SmithLangGeneric *generic = &smith_lang_generics[lang->generics_count];
            *generic = (SmithLangGeneric){};
            uint32_t mark = smith_lang_gt.count;
            uint32_t mark_text = smith_lang_gt.text_used;
            char words[4][smith_lang_name_max];
            bool good = smith_lang_generic_line_split(text + at + marker, end - at - marker, &parsed);
            for (size_t i = 0; good && i < 4; i++) // module, member, lowered name, source file
                smith_lang_unescape(parsed.words[i], parsed.lengths[i], words[i], sizeof words[i]);
            good = good && smith_lang_generic_read_tokens(&parsed, generic);
            if (good) {
                generic->module = smith_lang_store(lang, words[0], strlen(words[0]));
                generic->member = smith_lang_store(lang, words[1], strlen(words[1]));
                generic->lowered = smith_lang_store(lang, words[2], strlen(words[2]));
                generic->source = smith_lang_store(lang, words[3], strlen(words[3]));
                generic->is_inline = parsed.words[4][0] == 'i' || parsed.words[4][0] == 'q';
                generic->hidden = parsed.words[4][0] == 'p' || parsed.words[4][0] == 'q' || parsed.words[4][0] == 'g';
                generic->plain = parsed.words[4][0] == 'f' || parsed.words[4][0] == 'g';
                generic->name_index = (uint32_t)strtoul(parsed.words[5], nullptr, 10);
                generic->open_index = (uint32_t)strtoul(parsed.words[6], nullptr, 10);
                generic->close_index = (uint32_t)strtoul(parsed.words[7], nullptr, 10);
                generic->body_index = (uint32_t)strtoul(parsed.words[8], nullptr, 10);
                generic->exported = !generic->hidden;
                good = smith_lang_generic_scan(generic) == nullptr;
            }
            if (good &&
                smith_lang_generic_find_in(lang, words[0], strlen(words[0]), words[1], strlen(words[1]), true) < 0) {
                lang->generics_count += 1;
            } else {
                smith_lang_gt.count = mark;
                smith_lang_gt.text_used = mark_text;
            }
        }
        at = end + 1;
    }
}

// ---- Layout of what is written
// -------------------------------------------------------------------------------------------

static char smith_lang_gen_dump[(size_t)1 << 20];

// Whether a space goes before this token, given the one before it: `f(x)` and `a->b`, `x = y`.
static bool smith_lang_gen_space(const SmithLang *lang, SmithTokenKind kind, const char *text, size_t length) {
    char prev = lang->gen_prev;
    if (prev == 0 || prev == '(' || prev == '[' || prev == '.' || prev == '!' || prev == '~' || prev == ':')
        return false;
    bool word_before = (prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z') || (prev >= '0' && prev <= '9') ||
                       prev == '_' || prev == ')' || prev == ']' || prev == '"';
    if (kind == SmithTokenKind_Punctuator && length >= 1) {
        char c = text[0];
        if (c == ')' || c == ']' || c == ',' || c == ';' || c == '.') return false;
        if ((c == '(' || c == '[') && word_before) return false;
        if (length == 2 && ((c == ':' && text[1] == ':') || (c == '-' && text[1] == '>'))) return false;
    }
    return !(prev == '>' && lang->gen_dump_used > 1 && smith_lang_gen_dump[lang->gen_dump_used - 2] == '-');
}

// The text of what the generics write, laid out for reading: a line for each statement.
static void smith_lang_gen_note(SmithLang *lang, SmithTokenKind kind, const char *text, size_t length) {
    if (!lang->gen_dump_on || lang->gen_dump_used + length + 80 > sizeof smith_lang_gen_dump) return;
    char *out = smith_lang_gen_dump;
    bool close_brace = kind == SmithTokenKind_Punctuator && length == 1 && text[0] == '}' && lang->gen_dump_parens == 0;
    if (close_brace && lang->gen_dump_braces > 0) lang->gen_dump_braces -= 1;
    bool line_start = lang->gen_dump_used == 0 || out[lang->gen_dump_used - 1] == '\n';
    if (line_start) {
        for (int i = 0; i < lang->gen_dump_braces * 4 && i < 60; i++)
            out[lang->gen_dump_used++] = ' ';
    } else if (smith_lang_gen_space(lang, kind, text, length)) {
        out[lang->gen_dump_used++] = ' ';
    }
    memcpy(out + lang->gen_dump_used, text, length);
    lang->gen_dump_used += length;
    lang->gen_prev = length > 0 ? text[length - 1] : 0;
    if (kind != SmithTokenKind_Punctuator || length != 1) return;
    lang->gen_dump_parens += text[0] == '(' ? 1 : text[0] == ')' && lang->gen_dump_parens > 0 ? -1 : 0;
    if (text[0] == '{' && lang->gen_dump_parens == 0) lang->gen_dump_braces += 1;
    if (lang->gen_dump_parens == 0 && (text[0] == ';' || text[0] == '{' || text[0] == '}')) {
        out[lang->gen_dump_used++] = '\n';
        lang->gen_prev = 0;
    }
}

// A new line in the dump: a call or an instance begins.
static void smith_lang_gen_note_break(SmithLang *lang) {
    if (lang->gen_dump_on && lang->gen_dump_used > 0 && smith_lang_gen_dump[lang->gen_dump_used - 1] != '\n') {
        smith_lang_gen_dump[lang->gen_dump_used++] = '\n';
    }
    lang->gen_prev = 0;
}

// Writes a token of the lowering's own: no source has it, and `instance` says whose body it is in (0: none).
static void smith_lang_gen_put_expanding(SmithLang *lang, const SmithHost *host, SmithTokenKind kind, const char *text,
                                         uint32_t location, uint32_t instance, bool expand) {
    SmithToken token = {};
    token.expand = expand; // a builtin macro such as __SIZE_TYPE__
    token.kind = kind;
    token.handle = smith_token_no_handle;
    token.text = text;
    token.length = (uint32_t)strlen(text);
    token.space_before = true;
    token.location = location;
    token.synthetic = true;
    token.instance = instance;
    token.space_before = smith_lang_gen_space(lang, kind, text, token.length);
    smith_lang_gen_note(lang, kind, text, token.length);
    smith_lang_emit_code(lang, host, token);
}

static void smith_lang_gen_put(SmithLang *lang, const SmithHost *host, SmithTokenKind kind, const char *text,
                               uint32_t location, uint32_t instance) {
    smith_lang_gen_put_expanding(lang, host, kind, text, location, instance, false);
}

// Writes the pieces, in order.
static void smith_lang_gen_puts(SmithLang *lang, const SmithHost *host, const SmithLangPiece *pieces, size_t count,
                                uint32_t location, uint32_t instance) {
    for (size_t i = 0; i < count; i++) // the pieces
        smith_lang_gen_put(lang, host, pieces[i].kind, pieces[i].text, location, instance);
}

// Where a recorded token was written: the host's own, else (a token read from a header) the host's place for its line
// and column in the module's source, else the call's.
static uint32_t smith_lang_gen_place(const SmithHost *host, const SmithLangGeneric *generic, SmithLangGenToken *token,
                                     uint32_t fallback) {
    if (token->location != UINT32_MAX) return token->location;
    if (token->line != 0 && host->locate != nullptr) {
        uint32_t placed = host->locate(host->context, smith_lang_str(generic->source), token->line, token->column);
        if (placed != UINT32_MAX) {
            token->location = placed;
            return placed;
        }
    }
    return fallback;
}

// A token of a pool as written. The tokens of an instance are the generic's own, so errors in them point there.
static void smith_lang_gen_put_token(SmithLang *lang, const SmithHost *host, const SmithLangPool *pool,
                                     const SmithLangGenToken *token, uint32_t location, uint32_t instance, bool real) {
    SmithToken written = smith_lang_gt_token(pool, token);
    written.location = location;
    written.synthetic = !real;
    written.instance = instance;
    // An instance's tokens begin the lines the generic's did: the compiler reads it (-Wmisleading-indentation).
    // (Only a host with a parser: the raw lowering is text, and lines in it would change the bootstrap's bytes.)
    written.line_start =
        instance != 0 && host->type_query != nullptr && (token->flags & smith_lang_gen_flag_line_start) != 0;
    written.space_before = smith_lang_gen_space(lang, (SmithTokenKind)token->kind, written.text, written.length);
    smith_lang_gen_note(lang, (SmithTokenKind)token->kind, written.text, written.length);
    smith_lang_emit_code(lang, host, written);
}

// ---- The types an instance was asked for
// -------------------------------------------------------------------------------

static bool smith_lang_it_has_paren(uint32_t first, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) { // the tokens of one type
        const SmithLangGenToken *token = &smith_lang_it.tokens[first + i];
        if (token->kind == SmithTokenKind_Punctuator && token->length == 1 && smith_lang_it.text[token->text] == '(') {
            return true;
        }
    }
    return false;
}

// A type written as tokens for the parser to read: the lowered spelling, one space between tokens.
static size_t smith_lang_it_spelling(uint32_t first, uint32_t count, char *out, size_t capacity) {
    size_t used = 0;
    for (uint32_t i = 0; i < count && used + 2 < capacity; i++) { // the tokens of one type
        const SmithLangGenToken *token = &smith_lang_it.tokens[first + i];
        if (used > 0) out[used++] = ' ';
        size_t take = token->length < capacity - used - 1 ? token->length : capacity - used - 1;
        memcpy(out + used, smith_lang_pool_text(&smith_lang_it, token), take);
        used += take;
    }
    out[used] = 0;
    return used;
}

// The same type as the source wrote it: `geometry::Point` for the compiler's `geometry_Point`, `u32` for its own name.
static size_t smith_lang_it_written(const SmithLang *lang, uint32_t first, uint32_t count, char *out, size_t capacity) {
    size_t used = 0;
    for (uint32_t i = 0; i < count && used + 2 < capacity; i++) { // the tokens of one type
        const SmithLangGenToken *token = &smith_lang_it.tokens[first + i];
        const char *text = smith_lang_pool_text(&smith_lang_it, token);
        size_t length = token->length;
        const SmithLangWritten *written = smith_lang_written_lookup(lang, text, length);
        if (written != nullptr) {
            text = smith_lang_str(written->written);
            length = written->written.length;
        }
        for (size_t k = 0; k < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; k++) { // 13 entries
            if (smith_lang_equals(text, length, smith_lang_sized[k].standard)) {
                text = smith_lang_sized[k].name;
                length = strlen(text);
            }
        }
        if (used > 0) out[used++] = ' ';
        size_t take = length < capacity - used - 1 ? length : capacity - used - 1;
        memcpy(out + used, text, take);
        used += take;
    }
    out[used] = 0;
    return used;
}

// A name for the instance in C: the generic's, then the types, with whatever is no letter or digit made `_`.
static SmithLangText smith_lang_instance_cname(SmithLang *lang, const SmithLangGeneric *generic,
                                               const char *spellings) {
    char name[smith_lang_name_max];
    int used = snprintf(name, sizeof name, "%s__", smith_lang_str(generic->lowered));
    for (size_t i = 0; spellings[i] != 0 && (size_t)used + 8 < sizeof name; i++) { // the types
        char c = spellings[i];
        bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        char mapped = 'x';
        if (word) mapped = c;
        if (c == '*') mapped = 'P';
        if (c == ' ' || c == '\x1f') mapped = '_';
        name[used++] = mapped;
    }
    name[used] = 0;
    char unique[smith_lang_name_max];
    snprintf(unique, sizeof unique, "%s", name);
    for (unsigned n = 2; n < 1000; n++) { // two types that spell alike are told apart by a number
        bool taken = false;
        for (size_t i = 0; i < lang->instances_count; i++) { // the instances so far
            const SmithLangInstance *other = &smith_lang_instances[i];
            taken = taken || (other->cname.length == strlen(unique) &&
                              memcmp(smith_lang_str(other->cname), unique, other->cname.length) == 0);
        }
        if (!taken) break;
        snprintf(unique, sizeof unique, "%s_%u", name, n);
    }
    return smith_lang_store(lang, unique, strlen(unique));
}

static bool smith_lang_has_pack(const SmithLangGeneric *generic) {
    return generic->type_count > 0 && generic->type_kind[generic->type_count - 1] == SmithLangSlot_Pack;
}

// The kind of the entry `entry` of an instance: the slots in order, then each type of `auto... rest` (an `auto`).
static uint8_t smith_lang_entry_kind(const SmithLangGeneric *generic, size_t entry) {
    size_t slots = generic->type_count - (smith_lang_has_pack(generic) ? 1U : 0U);
    return entry < slots ? generic->type_kind[entry] : (uint8_t)SmithLangSlot_Auto;
}

// The index of the first entry of `auto... rest`: where the slots that are one entry each end.
static size_t smith_lang_pack_base(const SmithLangGeneric *generic) {
    return generic->type_count - 1;
}

// Whether the generic has a compile-time function: its instances are not told apart across files (a `static` function
// of two files may share a name).
static bool smith_lang_has_function(const SmithLangGeneric *generic) {
    for (size_t k = 0; k < generic->type_count; k++) { // the slots
        if (generic->type_kind[k] == SmithLangSlot_Function) return true;
    }
    return false;
}

// The list `auto... rest` of an instance (its uses are written below, after the constant expressions they may hold).
static bool smith_lang_pack_marked(const SmithLangGeneric *generic, const SmithLangGenToken *token) {
    return token->param != 0 && token->special == SmithLangGen_Plain && (size_t)token->param <= generic->type_count &&
           generic->type_kind[token->param - 1] == SmithLangSlot_Pack;
}

static const SmithLangGenToken *smith_lang_pack_name(const SmithLangGeneric *generic) {
    return &smith_lang_gt.tokens[generic->first + generic->type_token[smith_lang_pack_base(generic)]];
}

// How many arguments the instance's list has.
static size_t smith_lang_pack_size(const SmithLangGeneric *generic, const SmithLangInstance *instance) {
    return instance->entries - smith_lang_pack_base(generic);
}

typedef struct {
    char canonical[smith_lang_entries_max][256];
    char written[smith_lang_entries_max][256];
    char spelling[smith_lang_entries_max][256];
} SmithLangTypeNames;

static SmithLangTypeNames smith_lang_names;

// How the parser and the source name each type argument: the first is the identity of the type. A function is its name.
static void smith_lang_instance_names(const SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                      const uint32_t first[], const uint32_t count[], size_t entries,
                                      SmithLangTypeNames *names) {
    for (size_t k = 0; k < entries; k++) { // the type arguments
        smith_lang_it_spelling(first[k], count[k], names->spelling[k], sizeof names->spelling[k]);
        smith_lang_it_written(lang, first[k], count[k], names->written[k], sizeof names->written[k]);
        SmithTypeQuery query = {};
        if (smith_lang_entry_kind(generic, k) != SmithLangSlot_Function && host->type_query != nullptr &&
            host->type_query(host->context, names->spelling[k], (uint32_t)strlen(names->spelling[k]), &query) &&
            query.canonical_length < sizeof names->canonical[k]) {
            memcpy(names->canonical[k], query.canonical, query.canonical_length);
            names->canonical[k][query.canonical_length] = 0;
        } else {
            snprintf(names->canonical[k], sizeof names->canonical[k], "%s", names->spelling[k]);
        }
    }
}

// The index of the instance of this generic for these canonical types, or -1.
static int smith_lang_instance_find(const SmithLang *lang, uint32_t generic, const char *key, size_t key_length) {
    for (size_t i = 0; i < lang->instances_count; i++) { // the instances so far
        const SmithLangInstance *other = &smith_lang_instances[i];
        if (other->generic == generic && other->key.length == key_length &&
            memcmp(smith_lang_str(other->key), key, key_length) == 0) {
            return (int)i;
        }
    }
    return -1;
}

// The string that is the instance's symbol: the module, the generic and the canonical types (and, with a function, the
// file: two files may each have a `static` function of that name).
static void smith_lang_instance_label(const SmithLang *lang, const SmithLangGeneric *generic,
                                      const SmithLangTypeNames *names, size_t entries, char *out, size_t capacity) {
    int used = snprintf(out, capacity, "\"%s%s::%s(", smith_lang_symbol_underscore, smith_lang_str(generic->module),
                        smith_lang_str(generic->member));
    for (size_t k = 0; k < entries; k++) { // the types, canonical, in a string
        if (k > 0) used += snprintf(out + used, capacity - (size_t)used, ", ");
        for (const char *c = names->canonical[k]; *c != 0 && (size_t)used + 4 < capacity; c++) {
            if (*c == '"' || *c == '\\') out[used++] = '\\';
            out[used++] = *c;
        }
    }
    if (smith_lang_has_function(generic) && lang->main_file != nullptr) {
        used += snprintf(out + used, capacity - (size_t)used, ")@");
        for (const char *c = lang->main_file; *c != 0 && (size_t)used + 4 < capacity; c++) {
            if (*c == '"' || *c == '\\') out[used++] = '\\';
            out[used++] = *c;
        }
        out[used] = 0;
        snprintf(out + used, capacity - (size_t)used, "\"");
        return;
    }
    snprintf(out + used, capacity - (size_t)used, ")\"");
}

// The note that names an instance: `arena::push(Point)`.
static void smith_lang_instance_description(const SmithLangGeneric *generic, const SmithLangTypeNames *names,
                                            size_t entries, char *out, size_t capacity) {
    int used = snprintf(out, capacity, "%s::%s(", smith_lang_str(generic->module), smith_lang_str(generic->member));
    for (size_t k = 0; k < entries; k++) // the types, as written
        used += snprintf(out + used, capacity - (size_t)used, "%s%s", k > 0 ? ", " : "", names->written[k]);
    snprintf(out + used, capacity - (size_t)used, ")");
}

// Makes the instance (its types are staged in smith_lang_it) and tells the lowering the name the source gives it.
static int smith_lang_instance_make(SmithLang *lang, uint32_t generic_index, const uint32_t first[],
                                    const uint32_t count[], size_t entries, const SmithLangTypeNames *names,
                                    const char *key, uint32_t location) {
    const SmithLangGeneric *generic = &smith_lang_generics[generic_index];
    SmithLangInstance *instance = &smith_lang_instances[lang->instances_count];
    *instance = (SmithLangInstance){};
    instance->generic = generic_index;
    instance->entries = (uint8_t)entries;
    for (size_t k = 0; k < entries; k++) { // the types
        instance->arg_first[k] = first[k];
        instance->arg_count[k] = count[k];
    }
    static char spellings[smith_lang_entries_max * 257];
    spellings[0] = 0;
    for (size_t k = 0; k < entries; k++) // `Point`, or `Point\x1fu32`
        snprintf(spellings + strlen(spellings), sizeof spellings - strlen(spellings), "%s%s", k > 0 ? "\x1f" : "",
                 names->written[k]);
    char label[smith_lang_message_max];
    char description[smith_lang_message_max];
    smith_lang_instance_label(lang, generic, names, entries, label, sizeof label);
    smith_lang_instance_description(generic, names, entries, description, sizeof description);
    instance->key = smith_lang_store(lang, key, strlen(key));
    instance->cname = smith_lang_instance_cname(lang, generic, spellings);
    instance->label = smith_lang_store(lang, label, strlen(label));
    instance->description = smith_lang_store(lang, description, strlen(description));
    instance->call_location = location;
    instance->depth = lang->instance_depth + 1;
    instance->parent = lang->instance_current;
    char member[smith_lang_message_max]; // `push<Point>`: what the debugger shows after the module
    int used = snprintf(member, sizeof member, "%s<", smith_lang_str(generic->member));
    for (size_t k = 0; k < entries; k++) // the types, as written
        used += snprintf(member + used, sizeof member - (size_t)used, "%s%s", k > 0 ? ", " : "", names->written[k]);
    snprintf(member + used, sizeof member - (size_t)used, ">");
    smith_lang_written_add(lang, smith_lang_str(instance->cname), instance->cname.length, description,
                           strlen(description), 0, generic->module, true, member, strlen(member));
    smith_lang_it.keep = smith_lang_it.count;
    smith_lang_it.keep_text = smith_lang_it.text_used;
    lang->instances_count += 1;
    return (int)(lang->instances_count - 1);
}

// Takes the staged tokens out, back to `mark`, but not those of an instance made since (an argument that holds a call
// asks for the call's instance while the outer call's types are staged below it).
static void smith_lang_it_release(const uint32_t mark[2]) {
    smith_lang_it.count = mark[0] > smith_lang_it.keep ? mark[0] : smith_lang_it.keep;
    smith_lang_it.text_used = mark[1] > smith_lang_it.keep_text ? mark[1] : smith_lang_it.keep_text;
}

// Asks for the instance of a generic for the types staged in smith_lang_it (`entries` of them, at `first` and `count`;
// the pool was at `mark` before them). The instance made earlier for the same types is used when there is one, and the
// staged tokens are taken out. Returns the instance, or -1 after the error.
static int smith_lang_instance_request(SmithLang *lang, const SmithHost *host, uint32_t generic_index,
                                       const uint32_t first[], const uint32_t count[], size_t entries,
                                       const uint32_t mark[2], uint32_t location) {
    const SmithLangGeneric *generic = &smith_lang_generics[generic_index];
    SmithLangTypeNames *names = &smith_lang_names;
    smith_lang_instance_names(lang, host, generic, first, count, entries, names);
    static char key[smith_lang_entries_max * 257];
    key[0] = 0;
    for (size_t k = 0; k < entries; k++) // the identity: the canonical spellings
        snprintf(key + strlen(key), sizeof key - strlen(key), "%s%s", k > 0 ? "\x1f" : "", names->canonical[k]);
    int found = smith_lang_instance_find(lang, generic_index, key, strlen(key));
    if (found >= 0) {
        smith_lang_it_release(mark); // the staged tokens are kept only for a new instance
        return found;
    }
    if (lang->instances_count >= smith_lang_instances_max || lang->instance_depth + 1 > smith_lang_instance_depth_max) {
        smith_lang_error(lang, host, location,
                         "generic instances nest more than %zu deep, or there are more than %zu (does a generic ask "
                         "for itself with a bigger type?)",
                         smith_lang_instance_depth_max, smith_lang_instances_max);
        smith_lang_it_release(mark);
        return -1;
    }
    return smith_lang_instance_make(lang, generic_index, first, count, entries, names, key, location);
}

// ---- Writing a signature and a call
// --------------------------------------------------------------------------------------

constexpr size_t smith_lang_replay_depth_max = 256;
constexpr size_t smith_lang_replay_steps_max = (size_t)1 << 24;

typedef struct {
    uint32_t start; // [start, end) of smith_lang_gt
    uint32_t end;
} SmithLangSpan;

// A type argument in place of its T: its tokens, as a `__typeof__` when it has parentheses (a function pointer's). A
// function (`constexpr auto f`) is its name, as it is.
static void smith_lang_gen_put_arg(SmithLang *lang, const SmithHost *host, const SmithLangInstance *instance,
                                   unsigned param, uint32_t instance_id) {
    uint32_t first = instance->arg_first[param];
    uint32_t count = instance->arg_count[param];
    const SmithLangGeneric *generic = &smith_lang_generics[instance->generic];
    bool function = param < generic->type_count && generic->type_kind[param] == SmithLangSlot_Function;
    bool wrap = !function && smith_lang_it_has_paren(first, count);
    uint32_t location = smith_lang_it.tokens[first].location;
    if (wrap) {
        smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "__typeof__", location, instance_id);
        smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "(", location, instance_id);
    }
    for (uint32_t i = 0; i < count; i++) // the tokens of the type
        smith_lang_gen_put_token(lang, host, &smith_lang_it, &smith_lang_it.tokens[first + i],
                                 smith_lang_it.tokens[first + i].location, instance_id, false);
    if (wrap) smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ")", location, instance_id);
}

// The tag of a slice of the type argument, as `T xs[:]` would name it where the instance is written.
static void smith_lang_gen_put_slice_tag(SmithLang *lang, const SmithHost *host, const SmithLangInstance *instance,
                                         unsigned param, uint32_t location, uint32_t instance_id) {
    static SmithToken span[48];
    uint32_t first = instance->arg_first[param];
    uint32_t count = instance->arg_count[param];
    if (count > sizeof span / sizeof span[0]) count = sizeof span / sizeof span[0];
    for (uint32_t i = 0; i < count; i++) // the tokens of the type
    {
        span[i] = smith_lang_gt_token(&smith_lang_it, &smith_lang_it.tokens[first + i]);
        for (size_t k = 0; k < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; k++) { // `u8` was lowered
            if (!smith_lang_equals(span[i].text, span[i].length, smith_lang_sized[k].standard)) continue;
            span[i].text = smith_lang_sized[k].name;
            span[i].length = (uint32_t)strlen(smith_lang_sized[k].name);
        }
    }
    char spelled[512];
    char tag[640];
    const char *problem = nullptr;
    uint32_t where = location;
    if (!smith_lang_tuple_spell(lang, span, 0, count, spelled, sizeof spelled, &problem, &where)) {
        smith_lang_error(lang, host, location, "%s", problem != nullptr ? problem : "cannot spell the slice's type");
        snprintf(spelled, sizeof spelled, "error");
    }
    smith_lang_slice_tag(spelled, tag, sizeof tag);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, tag, location, instance_id);
}

// A token of a generic with the type arguments of `instance` in place of T (none: as it is).
static void smith_lang_gen_put_subst(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                     const SmithLangInstance *instance, SmithLangGenToken *token, uint32_t fallback,
                                     uint32_t instance_id, bool real) {
    bool pack = token->param != 0 && instance != nullptr &&
                smith_lang_generics[instance->generic].type_kind[token->param - 1] == SmithLangSlot_Pack;
    if (token->param != 0 && instance != nullptr && !pack) {
        if ((token->flags & smith_lang_gen_flag_value) != 0) { // a value of the type, `(*(T *)0)`
            static const SmithLangPiece open[] = {
                {SmithTokenKind_Punctuator, "("}, {SmithTokenKind_Punctuator, "*"}, {SmithTokenKind_Punctuator, "("}};
            static const SmithLangPiece close[] = {{SmithTokenKind_Punctuator, "*"},
                                                   {SmithTokenKind_Punctuator, ")"},
                                                   {SmithTokenKind_Number, "0"},
                                                   {SmithTokenKind_Punctuator, ")"}};
            uint32_t at = token->location != UINT32_MAX ? token->location : fallback;
            smith_lang_gen_puts(lang, host, open, sizeof open / sizeof open[0], at, instance_id);
            smith_lang_gen_put_arg(lang, host, instance, (unsigned)token->param - 1, instance_id);
            smith_lang_gen_puts(lang, host, close, sizeof close / sizeof close[0], at, instance_id);
            return;
        }
        if ((token->flags & smith_lang_gen_flag_slice) != 0) {
            // In a generic whose result type is computed (`slice_of(T, countof(dims)) push_slice(...)`), the struct
            // the body names is the result's, whatever its dimensions: the instance's `struct tag` (issue #107).
            const SmithLangInstance *kept = &smith_lang_instances[instance - smith_lang_instances];
            if (kept->result_state == 1 && strncmp(kept->result, "struct smith_array_", 19) == 0) {
                smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, kept->result + 7,
                                   token->location != UINT32_MAX ? token->location : fallback, instance_id);
                return;
            }
            smith_lang_gen_put_slice_tag(lang, host, instance, (unsigned)token->param - 1,
                                         token->location != UINT32_MAX ? token->location : fallback, instance_id);
            return;
        }
        smith_lang_gen_put_arg(lang, host, instance, (unsigned)token->param - 1, instance_id);
        return;
    }
    uint32_t location = generic != nullptr ? smith_lang_gen_place(host, generic, token, fallback) : token->location;
    smith_lang_gen_put_token(lang, host, &smith_lang_gt, token, location, instance_id, real);
}

// The index of the first token of the head after its leading attributes (`[[nodiscard]]`).
static uint32_t smith_lang_generic_skip_attributes(const SmithLangGeneric *generic) {
    uint32_t at = 0;
    const SmithLangGenToken *tokens = &smith_lang_gt.tokens[generic->first];
    while (at + 1 < generic->name_index && smith_lang_gt_punct(&tokens[at], "[") &&
           smith_lang_gt_punct(&tokens[at + 1], "[")) {
        int depth = 0;
        for (; at < generic->name_index; at++) { // to the end of the attribute
            depth += smith_lang_gt_punct(&tokens[at], "[") ? 1 : smith_lang_gt_punct(&tokens[at], "]") ? -1 : 0;
            if (depth == 0) {
                at += 1;
                break;
            }
        }
    }
    return at;
}

// The parameters `auto... rest` is for this instance: `T0 rest_0, T1 rest_1`, one for each argument the call passed.
// Returns the number of parameters written so far.
static size_t smith_lang_gen_pack_params(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                         const SmithLangInstance *instance, const SmithLangParam *param, size_t written,
                                         uint32_t location, uint32_t instance_id) {
    const SmithLangGenToken *name = &smith_lang_gt.tokens[param->start + 2];
    for (size_t e = smith_lang_pack_base(generic); e < instance->entries; e++) { // the arguments of the list
        if (written++ > 0) smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ",", location, instance_id);
        static const SmithLangPiece unused[] = {{SmithTokenKind_Punctuator, "["},
                                                {SmithTokenKind_Punctuator, "["},
                                                {SmithTokenKind_Identifier, "maybe_unused"},
                                                {SmithTokenKind_Punctuator, "]"},
                                                {SmithTokenKind_Punctuator, "]"}};
        smith_lang_gen_puts(lang, host, unused, sizeof unused / sizeof unused[0], location,
                            instance_id); // a list may be only counted
        smith_lang_gen_put_arg(lang, host, instance, (unsigned)e, instance_id);
        char element[smith_lang_name_max];
        snprintf(element, sizeof element, "%.*s_%zu", (int)name->length, smith_lang_gt_str(name),
                 e - smith_lang_pack_base(generic));
        smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, element, location, instance_id);
    }
    return written;
}

static bool smith_lang_result_call(const SmithLangGeneric *generic);
static void smith_lang_gen_put_result(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                      const SmithLangInstance *instance, uint32_t location, uint32_t instance_id);

// `R name(P)` for an instance: the head with the type arguments for T, the instance's name, and the parameters that are
// no `type`. `attributes` keeps the head's own leading attributes (a definition has them, a declaration cannot).
static void smith_lang_gen_signature(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                     const SmithLangInstance *instance, bool attributes, uint32_t location,
                                     uint32_t instance_id) {
    SmithLangGenToken *tokens = &smith_lang_gt.tokens[generic->first];
    uint32_t result = smith_lang_generic_skip_attributes(generic);
    for (uint32_t at = 0; attributes && at < result; at++)
        smith_lang_gen_put_subst(lang, host, generic, instance, &tokens[at], location, instance_id, false);
    if (smith_lang_result_call(generic)) {
        smith_lang_gen_put_result(lang, host, generic, instance, location, instance_id);
    } else {
        for (uint32_t at = result; at < generic->name_index; at++)
            smith_lang_gen_put_subst(lang, host, generic, instance, &tokens[at], location, instance_id, false);
    }
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, smith_lang_str(instance->cname), location, instance_id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "(", location, instance_id);
    SmithLangParam params[smith_lang_call_params_max];
    size_t count = smith_lang_generic_params(generic, params, smith_lang_call_params_max);
    size_t written = 0;
    for (size_t i = 0; i < count && count != SIZE_MAX; i++) { // the parameters that are no `type T`
        if (params[i].kind == SmithLangSlot_Type || params[i].kind == SmithLangSlot_Function) continue;
        if (params[i].kind == SmithLangSlot_Pack) {
            written =
                smith_lang_gen_pack_params(lang, host, generic, instance, &params[i], written, location, instance_id);
            continue;
        }
        if (written++ > 0) smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ",", location, instance_id);
        for (uint32_t at = params[i].start; at < params[i].end; at++)
            smith_lang_gen_put_subst(lang, host, generic, instance, &smith_lang_gt.tokens[at], location, instance_id,
                                     false);
    }
    if (written == 0) smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "void", location, instance_id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ")", location, instance_id);
}

// `extern R f(P) __asm__("label");`: what a call site and the instance's definition both declare.
static void smith_lang_gen_declaration(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                       const SmithLangInstance *instance, uint32_t location, uint32_t instance_id) {
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "extern", location, instance_id);
    smith_lang_gen_signature(lang, host, generic, instance, false, location, instance_id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "__asm__", location, instance_id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "(", location, instance_id);
    smith_lang_gen_put(lang, host, SmithTokenKind_String, smith_lang_str(instance->label), location, instance_id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ")", location, instance_id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ";", location, instance_id);
}

// The start of a call: `__extension__ ({ extern R f(P) __asm__("label"); f(`. The arguments follow, then `) ; } )`.
// `real` is the place of the name in the source when this is the call as written (the name is its token), else
// UINT32_MAX.
static void smith_lang_gen_call_start(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                      const SmithLangInstance *instance, uint32_t location, uint32_t real,
                                      uint32_t instance_id) {
    smith_lang_gen_note_break(lang);
    static const SmithLangPiece start[] = {{SmithTokenKind_Identifier, "__extension__"},
                                           {SmithTokenKind_Punctuator, "("},
                                           {SmithTokenKind_Punctuator, "{"}};
    smith_lang_gen_puts(lang, host, start, sizeof start / sizeof start[0], location, instance_id);
    smith_lang_gen_declaration(lang, host, generic, instance, location, instance_id);
    if (real == UINT32_MAX) {
        smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, smith_lang_str(instance->cname), location,
                           instance_id);
    } else {
        SmithToken name = {};
        name.kind = SmithTokenKind_Identifier;
        name.handle = smith_token_no_handle;
        name.text = smith_lang_str(instance->cname);
        name.length = instance->cname.length;
        name.space_before = smith_lang_gen_space(lang, SmithTokenKind_Identifier, name.text, name.length);
        name.location = real;
        smith_lang_gen_note(lang, SmithTokenKind_Identifier, name.text, name.length);
        smith_lang_emit_code(lang, host, name);
    }
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "(", location, instance_id);
}

// ---- Replaying tokens
// ----------------------------------------------------------------------------------------------------

constexpr size_t smith_lang_pack_name_max = 64;         // characters of the argument list's or a loop variable's name
constexpr size_t smith_lang_vars_max = 8;               // `for constexpr` loops open at once
constexpr size_t smith_lang_loop_iterations_max = 1024; // turns of one `for constexpr`

// The variable of a `for constexpr`: a number while the loop is written out.
typedef struct {
    char name[smith_lang_pack_name_max];
    uint16_t length;
    int64_t value;
} SmithLangVar;

typedef enum {
    SmithLangFrame_Tokens,      // [pos, end) of smith_lang_gt
    SmithLangFrame_Loop,        // a `for constexpr`
    SmithLangFrame_TypedStart,  // the arguments of a call that holds a call are written to the pool, to be typed
    SmithLangFrame_TypedFinish, // ... and the call is written, now that they are typed
} SmithLangFrameAction;

typedef struct {
    uint32_t cond_start; // the condition, [start, end) of smith_lang_gt
    uint32_t cond_end;
    uint32_t step_start;
    uint32_t step_end;
    uint32_t body_start; // the statement written once for each turn
    uint32_t body_end;
    uint32_t iterations;
    bool stepping; // the body has run: the step comes before the condition
    bool wrap;     // the body is one statement without braces: each turn is written in braces of its own
} SmithLangLoop;

typedef struct {
    uint32_t pos; // [pos, end) of smith_lang_gt, or a punctuator to write
    uint32_t end;
    char literal;
    uint32_t place; // of a punctuator to write (0: the call that asked for the instance)
    uint32_t begin; // where the frame's tokens began (an empty `rest...` looks at the token before it)
    uint8_t action; // SmithLangFrameAction
    bool started;   // TypedFinish: the typing flag to restore
    int32_t source; // TypedStart: the finish
    SmithLangLoop loop;
    uint32_t pool_first; // TypedFinish: where in smith_lang_gt the captured arguments begin
    uint32_t pool_text;
    uint32_t group; // TypedFinish: the call's CallBegin; `end` is its CallEnd
    uint16_t generic_index;
} SmithLangFrame;

typedef struct {
    SmithLangFrame frames[smith_lang_replay_depth_max];
    size_t count;
    SmithLangVar vars[smith_lang_vars_max];
    size_t var_count;
    const SmithLangInstance *instance; // whose type arguments are written for T (none: the file's own code)
    uint32_t instance_id;
    const SmithLangGeneric *generic; // whose tokens these are
    uint32_t location;               // of the call that asked for the instance
    bool typing; // the tokens are written to be typed by the compiler: a parameter of the instance stands for a value
    // The call being finished holds a call: its arguments as the instance writes them, for the compiler to type.
    SmithLangSpan spans[smith_lang_call_params_max]; // the arguments as the generic's tokens have them
    SmithLangSpan typed[smith_lang_call_params_max]; // and as captured (same count)
    size_t typed_count;
    uint32_t typed_first; // the captured arguments of the call being finished: [first, last) of smith_lang_gt
    uint32_t typed_last;
} SmithLangReplay;

static SmithLangReplay smith_lang_replay;

static bool smith_lang_replay_push(SmithLangReplay *replay, uint32_t pos, uint32_t end, char literal) {
    if (replay->count >= smith_lang_replay_depth_max) return false;
    replay->frames[replay->count++] = (SmithLangFrame){.pos = pos, .end = end, .literal = literal, .begin = pos};
    return true;
}

// A punctuator of the lowered call, written where the call is: in an instance that is a place of the generic, and
// never the call that asked for the instance (a statement then seems to end at that line, which misleads the
// compiler's -Wmisleading-indentation about the lines in between).
static bool smith_lang_replay_literal(SmithLangReplay *replay, char literal, uint32_t place) {
    if (!smith_lang_replay_push(replay, 0, 0, literal)) return false;
    replay->frames[replay->count - 1].place = place;
    return true;
}

// ---- Lists and loop variables of an instance's body
// ------------------------------------------------------------------------------------

// What an identifier of the body names, when it is the argument list `auto... rest`: its elements are the instance's
// parameters `rest_0`, ...
typedef struct {
    char name[smith_lang_pack_name_max];
    size_t count;
} SmithLangPackRef;

static bool smith_lang_member_before(uint32_t at) {
    if (at == 0) return false;
    const SmithLangGenToken *before = &smith_lang_gt.tokens[at - 1];
    return smith_lang_gt_punct(before, ".") || smith_lang_gt_punct(before, "->");
}

// Whether the identifier at `at` names the argument list of the replay's instance.
static bool smith_lang_pack_resolve(const SmithLangReplay *replay, uint32_t at, SmithLangPackRef *ref) {
    *ref = (SmithLangPackRef){};
    const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
    if (replay->generic == nullptr || replay->instance == nullptr || token->kind != SmithTokenKind_Identifier ||
        token->special != SmithLangGen_Plain || !smith_lang_pack_marked(replay->generic, token)) {
        return false;
    }
    const SmithLangGenToken *name = smith_lang_pack_name(replay->generic);
    snprintf(ref->name, sizeof ref->name, "%.*s", (int)name->length, smith_lang_gt_str(name));
    ref->count = smith_lang_pack_size(replay->generic, replay->instance);
    return true;
}

// The name of the argument list's `index`th element: `rest_0`.
static void smith_lang_pack_element(const SmithLangPackRef *ref, size_t index, char *out, size_t capacity) {
    snprintf(out, capacity, "%s_%zu", ref->name, index);
}

// Whether the tokens at `at` are `countof ( rest )`.
static bool smith_lang_pack_countof_at(const SmithLangReplay *replay, uint32_t at, uint32_t limit,
                                       SmithLangPackRef *ref) {
    return at + 3 < limit && smith_lang_gt_is(&smith_lang_gt.tokens[at], SmithTokenKind_Identifier, "countof") &&
           smith_lang_gt_punct(&smith_lang_gt.tokens[at + 1], "(") && smith_lang_pack_resolve(replay, at + 2, ref) &&
           smith_lang_gt_punct(&smith_lang_gt.tokens[at + 3], ")");
}

// Whether the identifier at `at` is a variable of an open `for constexpr`: its value.
static bool smith_lang_var_value(const SmithLangReplay *replay, uint32_t at, int64_t *value) {
    const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
    if (replay->var_count == 0 || token->kind != SmithTokenKind_Identifier || token->special != SmithLangGen_Plain ||
        token->param != 0 || smith_lang_member_before(at)) {
        return false;
    }
    for (size_t i = replay->var_count; i-- > 0;) { // the innermost loop first
        const SmithLangVar *var = &replay->vars[i];
        if (var->length == token->length && memcmp(var->name, smith_lang_gt_str(token), token->length) == 0) {
            *value = var->value;
            return true;
        }
    }
    return false;
}

// The CallEnd that closes the CallBegin at `begin`.
static uint32_t smith_lang_group_end(uint32_t begin, uint32_t limit) {
    int depth = 0;
    for (uint32_t at = begin; at < limit; at++) { // bounded by the generic
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        depth += token->special == SmithLangGen_CallBegin ? 1 : token->special == SmithLangGen_CallEnd ? -1 : 0;
        if (depth == 0) return at;
    }
    return limit;
}

// Splits the tokens [first, last) at the commas that stand outside brackets.
static size_t smith_lang_split_args(uint32_t first, uint32_t last, SmithLangSpan *out, size_t capacity) {
    if (first >= last) return 0;
    size_t count = 0;
    int depth = 0;
    uint32_t start = first;
    for (uint32_t at = first; at <= last; at++) { // bounded by the call
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        bool split = at == last || (depth == 0 && smith_lang_gt_punct(token, ","));
        if (!split) {
            depth += smith_lang_gt_open(token) ? 1 : smith_lang_gt_close(token) ? -1 : 0;
            continue;
        }
        if (count >= capacity) return SIZE_MAX;
        out[count++] = (SmithLangSpan){start, at};
        start = at + 1;
    }
    return count;
}

// Stages the tokens of one type argument in smith_lang_it, with the type arguments of the instance being written in
// place of its T. The staged range, or false when the pool is full.
static bool smith_lang_stage_type(SmithLangSpan span, const SmithLangInstance *instance, uint32_t fallback,
                                  uint32_t *first, uint32_t *count) {
    *first = smith_lang_it.count;
    for (uint32_t at = span.start; at < span.end; at++) { // the tokens of the type
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (token->param != 0 && instance != nullptr) {
            if (smith_lang_pack_marked(&smith_lang_generics[instance->generic], token)) return false; // no one type
            const SmithLangInstance *source = instance;
            unsigned param = (unsigned)token->param - 1;
            for (uint32_t i = 0; i < source->arg_count[param]; i++) { // the instance's own type
                SmithLangGenToken copy = smith_lang_it.tokens[source->arg_first[param] + i];
                if (smith_lang_pool_add(&smith_lang_it, copy, smith_lang_pool_text(&smith_lang_it, &copy),
                                        copy.length) == UINT32_MAX) {
                    return false;
                }
            }
            continue;
        }
        SmithLangGenToken copy = *token;
        if (copy.location == UINT32_MAX) copy.location = fallback;
        if (smith_lang_pool_add(&smith_lang_it, copy, smith_lang_gt_str(token), token->length) == UINT32_MAX)
            return false;
    }
    *count = smith_lang_it.count - *first;
    return true;
}

// ---- The type of an `auto` parameter, from its argument
// -----------------------------------------------------------------------------------------------

// The text of the calls to generics already lowered, as the source wrote them, for a message that quotes an argument
// (`pick::larger(a, 3)` and not the statement expression it became). Made again at each outermost call.
static char smith_lang_written_text[1 << 14];
static uint32_t smith_lang_written_used;

// The argument as the source wrote it (a nested call to a generic is still its own tokens here, not the lowered
// statement expression), for a message.
static void smith_lang_infer_written(SmithLangSpan span, char *out, size_t capacity) {
    size_t used = 0;
    out[0] = 0;
    for (uint32_t at = span.start; at < span.end; at++) { // the tokens
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (token->special == SmithLangGen_CallEnd) continue;
        const char *text = smith_lang_gt_str(token);
        size_t length = token->length;
        for (size_t i = 0; i < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; i++) { // 13 entries
            if (smith_lang_sized[i].name[0] != 'f' && smith_lang_equals(text, length, smith_lang_sized[i].standard)) {
                text = smith_lang_sized[i].name; // the lowering wrote `__INT32_TYPE__` for `i32`
                length = strlen(text);
                break;
            }
        }
        if (token->written != 0 && token->written <= smith_lang_written_used) { // a call already lowered
            text = smith_lang_written_text + token->written - 1;
            length = strlen(text);
            at += token->covers > 0 ? token->covers - 1 : 0;
        }
        if (used + length + 2 >= capacity) {
            snprintf(out + used, capacity - used, "...");
            return;
        }
        if (used > 0 && (token->flags & smith_lang_gen_flag_space) != 0) out[used++] = ' ';
        memcpy(out + used, text, length);
        used += length;
        out[used] = 0;
    }
}

// A type as the parser wrote it, with the standard names a header gives the sized types spelled as Smith does
// (`uint16_t u32 *` is `u16 u32 *`): the spelling the fix uses.
static void smith_lang_smith_spelling(const char *written, char *out, size_t capacity) {
    size_t used = 0;
    for (size_t at = 0; written[at] != 0 && used + 2 < capacity;) { // words, and each other character
        size_t end = at + 1;
        bool word = isalnum((unsigned char)written[at]) || written[at] == '_';
        while (word && (isalnum((unsigned char)written[end]) || written[end] == '_'))
            end += 1;
        const char *text = written + at;
        size_t length = end - at;
        for (size_t i = 0; word && i < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; i++) { // 13 entries
            if (smith_lang_sized[i].name[0] != 'f' && smith_lang_equals(text, length, smith_lang_sized[i].header)) {
                text = smith_lang_sized[i].name;
                length = strlen(text);
                break;
            }
        }
        if (used + length + 1 >= capacity) break;
        memcpy(out + used, text, length);
        used += length;
        at = end;
    }
    out[used] = 0;
    static const struct {
        const char *builtin;
        const char *name;
    } builtins[] = {{"int", "i32"},        {"unsigned int", "u32"},       {"long", "i64"},  {"unsigned long", "u64"},
                    {"long long", "i64"},  {"unsigned long long", "u64"}, {"short", "i16"}, {"unsigned short", "u16"},
                    {"signed char", "i8"}, {"unsigned char", "u8"},       {"float", "f32"}, {"double", "f64"}};
    for (size_t i = 0; i < sizeof builtins / sizeof builtins[0]; i++) { // 12 entries
        if (strcmp(out, builtins[i].builtin) == 0) snprintf(out, capacity, "%s", builtins[i].name);
    }
}

// Where an argument is written, for an error about it: its first token, else the call.
static uint32_t smith_lang_infer_place(const SmithHost *host, const SmithLangReplay *replay, SmithLangSpan span,
                                       uint32_t call) {
    if (span.start >= span.end) return call;
    SmithLangGenToken *token = &smith_lang_gt.tokens[span.start];
    if (replay->generic != nullptr) return smith_lang_gen_place(host, replay->generic, token, call);
    return token->location != UINT32_MAX && token->location != 0 ? token->location : call;
}

// The argument's tokens, with the instance's types for T, separated by \x1f for the host's question.
static bool smith_lang_infer_text(SmithLangSpan span, const SmithLangInstance *instance, uint32_t location, char *out,
                                  size_t capacity) {
    uint32_t mark[2] = {smith_lang_it.count, smith_lang_it.text_used};
    uint32_t first = 0;
    uint32_t count = 0;
    bool staged = smith_lang_stage_type(span, instance, location, &first, &count);
    size_t used = 0;
    for (uint32_t i = 0; staged && i < count; i++) { // the tokens
        const SmithLangGenToken *token = &smith_lang_it.tokens[first + i];
        if (used + token->length + 2 >= capacity) {
            staged = false;
            break;
        }
        if (i > 0) out[used++] = '\x1f';
        memcpy(out + used, smith_lang_pool_text(&smith_lang_it, token), token->length);
        used += token->length;
    }
    out[used] = 0;
    smith_lang_it.count = mark[0];
    smith_lang_it.text_used = mark[1];
    return staged;
}

// Stages the type the host wrote (`unsigned int`, `Point *`, `int (*)(int)`) as tokens in smith_lang_it.
static bool smith_lang_infer_stage(const char *written, uint32_t location, uint32_t *first, uint32_t *count) {
    *first = smith_lang_it.count;
    for (size_t at = 0; written[at] != 0;) { // words, and each other character by itself
        char c = written[at];
        if (c == ' ') {
            at += 1;
            continue;
        }
        size_t end = at + 1;
        bool word = isalnum((unsigned char)c) || c == '_';
        while (word && (isalnum((unsigned char)written[end]) || written[end] == '_'))
            end += 1;
        SmithLangGenToken token = {};
        token.kind = word ? SmithTokenKind_Identifier : SmithTokenKind_Punctuator;
        token.location = location;
        if (at > 0) token.flags = (uint8_t)smith_lang_gen_flag_space;
        if (smith_lang_pool_add(&smith_lang_it, token, written + at, end - at) == UINT32_MAX) return false;
        at = end;
    }
    *count = smith_lang_it.count - *first;
    return *count > 0;
}

static void smith_lang_callee_name(const SmithLang *lang, const char *module, size_t module_length, const char *member,
                                   size_t member_length, char *out, size_t capacity);

#include "lang_constexpr.c"

static void smith_lang_generic_poison(SmithLang *lang, const SmithHost *host, uint32_t location);

// ---- Argument lists: `auto... rest`
// ------------------------------------------------------------------------------------------------------

// `auto... rest` is a compile-time list of the arguments that remain, each with its own type. An instance has one
// parameter for each (`rest_0`, `rest_1`, ...), and the body uses the list in three ways, each a C construct run at
// compile time: `rest...` is the elements as a comma-separated list, `rest[i]` is the `i`th (a compile-time index, with
// its own type), and `countof(rest)` is how many there are.

typedef enum {
    SmithLangPack_None,   // not a use of a list
    SmithLangPack_Failed, // an error was reported; the tokens are skipped
    SmithLangPack_Expand,
    SmithLangPack_Index,
    SmithLangPack_Count,
    SmithLangPack_Var, // the variable of a `for constexpr`: its value
} SmithLangPackKind;

typedef struct {
    SmithLangPackKind kind;
    uint32_t consumed; // the tokens of the use
    size_t index;      // Index: which element; Count: how many there are
    int64_t number;    // Var: the value
    uint32_t place;    // where the argument list is written
    SmithLangPackRef args;
} SmithLangPackUse;

// The index in `rest[...]`, the tokens [first, last) of the generic, as the instance has it: the element, or an error.
static SmithLangPackUse smith_lang_pack_index(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                              uint32_t at, uint32_t limit, uint32_t place,
                                              const SmithLangPackRef *args) {
    uint32_t after = smith_lang_cx_skip_group(at + 1, limit);
    SmithLangPackUse use = {.kind = SmithLangPack_Failed,
                            .consumed = after == UINT32_MAX ? limit - at : after - at,
                            .place = place,
                            .args = *args};
    static SmithLangCx evaluation;
    SmithLangCx *cx = &evaluation;
    *cx = (SmithLangCx){.lang = lang, .host = host, .instance = replay->instance, .replay = replay, .location = place};
    int64_t value = 0;
    if (after == UINT32_MAX || after - 1 <= at + 2) {
        smith_lang_error(lang, host, place, "`%s[` is not closed by `]`, or has no index", args->name);
        return use;
    }
    if (!smith_lang_cx_eval(cx, at + 2, after - 1, &value)) {
        smith_lang_error(lang, host, place, "the index of `%s[...]` must be a compile-time constant: %s", args->name,
                         cx->message);
        return use;
    }
    if (value < 0 || (uint64_t)value >= args->count) {
        smith_lang_error(lang, host, place, "`%s[%lld]` is out of range: `%s` has %zu argument%s", args->name,
                         (long long)value, args->name, args->count, args->count == 1 ? "" : "s");
        return use;
    }
    use.kind = SmithLangPack_Index;
    use.index = (size_t)value;
    return use;
}

// What the tokens at `at` are, when they use a list or a loop variable of the instance being written: `rest...`,
// `rest[i]`, `countof(rest)`, `i`; anything else is None.
static SmithLangPackUse smith_lang_pack_use(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                            uint32_t at, uint32_t limit) {
    SmithLangPackUse use = {};
    const SmithLangGeneric *generic = replay->generic;
    if (generic == nullptr || replay->instance == nullptr ||
        (replay->var_count == 0 && !smith_lang_has_pack(generic))) {
        return use;
    }
    SmithLangGenToken *token = &smith_lang_gt.tokens[at];
    SmithLangPackRef args;
    bool counted = smith_lang_pack_countof_at(replay, at, limit, &args);
    int64_t number = 0;
    if (!counted && !smith_lang_pack_resolve(replay, at, &args)) {
        if (!smith_lang_var_value(replay, at, &number)) return use;
        return (SmithLangPackUse){.kind = SmithLangPack_Var,
                                  .consumed = 1,
                                  .number = number,
                                  .place = smith_lang_gen_place(host, generic, token, replay->location)};
    }
    uint32_t place = smith_lang_gen_place(host, generic, token, replay->location);
    if (counted) {
        return (SmithLangPackUse){
            .kind = SmithLangPack_Count, .consumed = 4, .index = args.count, .place = place, .args = args};
    }
    if (at + 1 < limit && smith_lang_gt_punct(&smith_lang_gt.tokens[at + 1], "...")) {
        return (SmithLangPackUse){.kind = SmithLangPack_Expand, .consumed = 2, .place = place, .args = args};
    }
    if (at + 1 < limit && smith_lang_gt_punct(&smith_lang_gt.tokens[at + 1], "[")) {
        return smith_lang_pack_index(lang, host, replay, at, limit, place, &args);
    }
    smith_lang_error(lang, host, place,
                     "`%s` is the tuple of the arguments, and is not passed whole: spread it into a call with `%s...`, "
                     "or write `%s[i]` for one argument or `countof(%s)` for how many",
                     args.name, args.name, args.name, args.name);
    return (SmithLangPackUse){.kind = SmithLangPack_Failed, .consumed = 1, .place = place};
}

// An empty `rest...` takes a comma with it: the one before it, else the one after. Whether the comma at `at` is the one
// before an empty expansion (the expansion then writes nothing, and the comma is dropped).
static bool smith_lang_pack_comma_before_empty(const SmithLangReplay *replay, uint32_t at, uint32_t limit) {
    SmithLangPackRef args;
    if (replay->generic == nullptr || replay->instance == nullptr || at + 2 >= limit ||
        !smith_lang_gt_punct(&smith_lang_gt.tokens[at], ",") || !smith_lang_pack_resolve(replay, at + 1, &args) ||
        !smith_lang_gt_punct(&smith_lang_gt.tokens[at + 2], "...")) {
        return false;
    }
    return args.count == 0;
}

// An empty expansion at `at` (after `begin`, before `limit`): 1 when the comma after it goes too, because none came
// before it.
static uint32_t smith_lang_pack_comma_after_empty(uint32_t at, uint32_t begin, uint32_t limit, uint32_t consumed) {
    if (at > begin && smith_lang_gt_punct(&smith_lang_gt.tokens[at - 1], ",")) return 0;
    return at + consumed < limit && smith_lang_gt_punct(&smith_lang_gt.tokens[at + consumed], ",") ? 1U : 0U;
}

// Writes a number the way the parser reads one: a negative value in parentheses.
static void smith_lang_number_write(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                    int64_t value, uint32_t place) {
    char text[32];
    if (value < 0) {
        smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "(", place, replay->instance_id);
        smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "-", place, replay->instance_id);
        snprintf(text, sizeof text, "%llu", (unsigned long long)0 - (unsigned long long)value);
    } else {
        snprintf(text, sizeof text, "%lld", (long long)value);
    }
    smith_lang_gen_put(lang, host, SmithTokenKind_Number, text, place, replay->instance_id);
    if (value < 0) smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ")", place, replay->instance_id);
}

// Writes a use of the list: the number, the element or all of them.
static void smith_lang_pack_write(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                  const SmithLangPackUse *use) {
    char text[smith_lang_name_max];
    if (use->kind == SmithLangPack_Failed) {
        smith_lang_generic_poison(lang, host, use->place); // what stands there has an error already
    } else if (use->kind == SmithLangPack_Count) {
        snprintf(text, sizeof text, "%zu", use->index);
        smith_lang_gen_put(lang, host, SmithTokenKind_Number, text, use->place, replay->instance_id);
    } else if (use->kind == SmithLangPack_Var) {
        smith_lang_number_write(lang, host, replay, use->number, use->place);
    } else if (use->kind == SmithLangPack_Index) {
        smith_lang_pack_element(&use->args, use->index, text, sizeof text);
        smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, text, use->place, replay->instance_id);
    } else if (use->kind == SmithLangPack_Expand) {
        for (size_t i = 0; i < use->args.count; i++) { // the elements, in order
            if (i > 0) smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ",", use->place, replay->instance_id);
            smith_lang_pack_element(&use->args, i, text, sizeof text);
            smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, text, use->place, replay->instance_id);
        }
    }
}

// A token in the pool's end, with the text given.
static bool smith_lang_pack_push(const SmithLangGenToken *like, SmithTokenKind kind, const char *text, bool copy) {
    SmithLangGenToken made = *like;
    made.kind = (uint8_t)kind;
    if (!copy) { // a token of the list's own is a name or a number, not a parameter of the generic
        made.param = 0;
        made.written = 0;
        made.covers = 0;
    }
    return smith_lang_pool_add(&smith_lang_gt, made, text, strlen(text)) != UINT32_MAX;
}

// A loop variable's value, as tokens at the pool's end.
static bool smith_lang_pack_push_number(const SmithLangGenToken *like, int64_t value) {
    char text[32];
    if (value >= 0) {
        snprintf(text, sizeof text, "%lld", (long long)value);
        return smith_lang_pack_push(like, SmithTokenKind_Number, text, false);
    }
    snprintf(text, sizeof text, "%llu", (unsigned long long)0 - (unsigned long long)value);
    return smith_lang_pack_push(like, SmithTokenKind_Punctuator, "(", false) &&
           smith_lang_pack_push(like, SmithTokenKind_Punctuator, "-", false) &&
           smith_lang_pack_push(like, SmithTokenKind_Number, text, false) &&
           smith_lang_pack_push(like, SmithTokenKind_Punctuator, ")", false);
}

// Whether the identifier at `at` is a list or a loop variable of the replay.
static bool smith_lang_pack_named(const SmithLangReplay *replay, uint32_t at) {
    SmithLangPackRef args;
    int64_t number;
    return smith_lang_pack_resolve(replay, at, &args) || smith_lang_var_value(replay, at, &number);
}

// The tokens of a call's arguments [*begin, *end), with each use of a list or a loop variable written out as the
// elements, the number or the name, in new tokens at the pool's end (the tokens of the generic are kept: another
// instance reads them again). False after the error. The range is the new one.
static bool smith_lang_pack_expand_range(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                         uint32_t *begin, uint32_t *end) {
    const SmithLangGeneric *generic = replay->generic;
    if (generic == nullptr || replay->instance == nullptr) return true;
    bool used = false;
    for (uint32_t at = *begin; at < *end && !used; at++) // is there a use of a list at all
        used = smith_lang_pack_named(replay, at) || smith_lang_fields_begins(replay, at, *end);
    if (!used) return true;
    uint32_t first = smith_lang_gt.count;
    bool good = true;
    for (uint32_t at = *begin; at < *end && good;) { // each token, copied unless it uses a list
        char text[smith_lang_name_max];
        const SmithLangGenToken *like = &smith_lang_gt.tokens[at];
        static SmithLangFieldPieces pieces;
        uint32_t taken = 0;
        int field = smith_lang_fields_use(lang, host, replay, at, *end, &pieces, &taken);
        if (field != 0) { // a use of fields(T), written in new tokens
            good = field > 0;
            for (size_t i = 0; good && i < pieces.count; i++)
                good = smith_lang_pack_push(like, pieces.kind[i], pieces.text[i], false);
            at += taken;
            continue;
        }
        SmithLangPackUse use = smith_lang_pack_use(lang, host, replay, at, *end);
        if (use.kind == SmithLangPack_None) {
            bool skip = smith_lang_pack_comma_before_empty(replay, at, *end);
            good = skip || smith_lang_pack_push(like, (SmithTokenKind)like->kind, smith_lang_gt_str(like), true);
            at += 1;
            continue;
        }
        uint32_t consumed = use.consumed;
        if (use.kind == SmithLangPack_Count) {
            snprintf(text, sizeof text, "%zu", use.index);
            good = smith_lang_pack_push(like, SmithTokenKind_Number, text, false);
        } else if (use.kind == SmithLangPack_Var) {
            good = smith_lang_pack_push_number(like, use.number);
        } else if (use.kind == SmithLangPack_Index) {
            smith_lang_pack_element(&use.args, use.index, text, sizeof text);
            good = smith_lang_pack_push(like, SmithTokenKind_Identifier, text, false);
        } else if (use.kind == SmithLangPack_Expand) {
            for (size_t i = 0; good && i < use.args.count; i++) { // the elements, in order
                if (i > 0) good = smith_lang_pack_push(like, SmithTokenKind_Punctuator, ",", false);
                smith_lang_pack_element(&use.args, i, text, sizeof text);
                good = good && smith_lang_pack_push(like, SmithTokenKind_Identifier, text, false);
            }
            if (use.args.count == 0) consumed += smith_lang_pack_comma_after_empty(at, *begin, *end, consumed);
        } else {
            good = false; // the error is reported
        }
        at += consumed;
    }
    if (!good) return false;
    *begin = first;
    *end = smith_lang_gt.count;
    return good;
}

// The parameter of a generic that a lone identifier names, when its type is one of the generic's types (`auto x`,
// `typeof(x) y`, `T v`): the type's index, else -1. In an instance's body, that parameter's type is the instance's.
static int smith_lang_param_type_of(const SmithLangGeneric *generic, const SmithLangInstance *instance,
                                    SmithLangSpan span) {
    if (span.end - span.start != 1) return -1;
    const SmithLangGenToken *name = &smith_lang_gt.tokens[span.start];
    if (name->kind != SmithTokenKind_Identifier || name->special != SmithLangGen_Plain) return -1;
    SmithLangParam params[smith_lang_call_params_max];
    size_t count = smith_lang_generic_params(generic, params, smith_lang_call_params_max);
    for (size_t i = 0; i < count && count != SIZE_MAX; i++) { // the parameters
        const SmithLangGenToken *first = &smith_lang_gt.tokens[params[i].start];
        if (params[i].end - params[i].start != 2 || first->param == 0) continue;
        const SmithLangGenToken *own = first + 1;
        if (own->length == name->length && memcmp(smith_lang_gt_str(own), smith_lang_gt_str(name), name->length) == 0) {
            return (int)first->param - 1;
        }
    }
    if (instance == nullptr || !smith_lang_has_pack(generic)) return -1;
    const SmithLangGenToken *pack = smith_lang_pack_name(generic); // `rest_2`: the third element of the list
    const char *text = smith_lang_gt_str(name);
    if (name->length <= pack->length + 1 || memcmp(text, smith_lang_gt_str(pack), pack->length) != 0 ||
        text[pack->length] != '_') {
        return -1;
    }
    char *digits_end = nullptr;
    unsigned long element = strtoul(text + pack->length + 1, &digits_end, 10);
    if (*digits_end != 0 || element >= smith_lang_pack_size(generic, instance)) return -1;
    return (int)(smith_lang_pack_base(generic) + element);
}

// The value of a constant argument, as the compiler folded it (the host's SmithExprType).
typedef struct {
    bool is_float;
    bool negative;
    bool overflow;
    uint64_t magnitude;
    double real;
} SmithLangLiteral;

typedef struct {
    char written[256];
    char canonical[256];
    bool constant;    // the compiler folds the argument to a number
    bool is_function; // the argument is a function (a designator), not a pointer to one
    SmithLangLiteral value;
} SmithLangArgType;

// Whether a type as the parser wrote it names a typedef of a system header's own (`__size_t`), which a module that
// does not include that header cannot spell: the type is then written as the compiler's canonical spelling.
static bool smith_lang_internal_type(const char *written) {
    for (const char *at = written; *at != 0; at++) { // the words
        bool begins =
            (at == written || !(isalnum((unsigned char)at[-1]) || at[-1] == '_')) && at[0] == '_' && at[1] == '_';
        if (!begins) continue;
        size_t length = 0;
        while (isalnum((unsigned char)at[length]) || at[length] == '_')
            length += 1;
        if (length > 4 && !(at[length - 1] == '_' && at[length - 2] == '_')) return true;
    }
    return false;
}

// Whether the argument holds a call kept as a group (a generic's call nested in the body of an instance).
static bool smith_lang_span_has_group(SmithLangSpan span) {
    for (uint32_t at = span.start; at < span.end; at++) {
        if (smith_lang_gt.tokens[at].special == SmithLangGen_CallBegin) return true;
    }
    return false;
}

// The argument `span`, as the instance writes it: the tokens a call that holds a call was captured as (smith_lang_gt
// at replay->typed_first on, split like the arguments of the call), for the compiler to type. False when there are
// none.
static bool smith_lang_typed_for(const SmithLangReplay *replay, SmithLangSpan span, SmithLangSpan *typed) {
    for (size_t i = 0; i < replay->typed_count; i++) {
        if (replay->spans[i].start == span.start && replay->spans[i].end == span.end) {
            *typed = replay->typed[i];
            return true;
        }
    }
    return false;
}

// The type of an argument, from the compiler (or, for a parameter of the generic being instanced, from the instance).
// False with `why` when it cannot be said.
static bool smith_lang_arg_type(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay, SmithLangSpan span,
                                uint32_t location, SmithLangArgType *out, const char **why, bool explain) {
    int own = replay->instance != nullptr && replay->generic != nullptr
                  ? smith_lang_param_type_of(replay->generic, replay->instance, span)
                  : -1;
    if (own >= 0) {
        char spelling[256];
        smith_lang_it_spelling(replay->instance->arg_first[own], replay->instance->arg_count[own], spelling,
                               sizeof spelling);
        smith_lang_it_written(lang, replay->instance->arg_first[own], replay->instance->arg_count[own], out->written,
                              sizeof out->written);
        SmithTypeQuery query = {};
        if (host->type_query != nullptr &&
            host->type_query(host->context, spelling, (uint32_t)strlen(spelling), &query) &&
            query.canonical_length < sizeof out->canonical) {
            memcpy(out->canonical, query.canonical, query.canonical_length);
            out->canonical[query.canonical_length] = 0;
        } else {
            snprintf(out->canonical, sizeof out->canonical, "%s", spelling);
        }
        return true;
    }
    char tokens[1024];
    SmithLangSpan text_span = span;
    const SmithLangInstance *text_instance = replay->instance;
    if (smith_lang_typed_for(replay, span, &text_span)) {
        text_instance = nullptr; // the types of T are written already
    } else {
        text_span = span;
    }
    if (!smith_lang_infer_text(text_span, text_instance, location, tokens, sizeof tokens)) {
        *why = "the argument is too long to take its type from";
        return false;
    }
    SmithExprType type = {.explain = explain};
    if (host->expr_type == nullptr) {
        *why = "the raw lowering has no compiler to type it (a bootstrapped module and the stage0 cannot call a "
               "function with `auto` parameters)";
        return false;
    }
    if (!host->expr_type(host->context, tokens, (uint32_t)strlen(tokens), &type)) {
        *why = type.why != nullptr ? type.why : "the compiler cannot say";
        return false;
    }
    memcpy(out->written, type.written, sizeof out->written);
    memcpy(out->canonical, type.canonical, sizeof out->canonical);
    out->constant = type.constant;
    out->is_function = type.is_function;
    out->value = (SmithLangLiteral){.is_float = type.is_float,
                                    .negative = type.negative,
                                    .overflow = type.overflow,
                                    .magnitude = type.magnitude,
                                    .real = type.real};
    if (smith_lang_internal_type(out->written)) memcpy(out->written, out->canonical, sizeof out->written);
    return true;
}

// The type of the `auto` parameter at `span`, staged in smith_lang_it. False after the error.
static bool smith_lang_infer_auto(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                  const SmithLangGeneric *generic, SmithLangSpan span, uint32_t location,
                                  uint32_t *first, uint32_t *count) {
    int own = replay->instance != nullptr && replay->generic != nullptr
                  ? smith_lang_param_type_of(replay->generic, replay->instance, span)
                  : -1;
    if (own >= 0) { // the instance's own type, as it is
        *first = smith_lang_it.count;
        for (uint32_t i = 0; i < replay->instance->arg_count[own]; i++) { // the tokens of the type
            SmithLangGenToken copy = smith_lang_it.tokens[replay->instance->arg_first[own] + i];
            if (smith_lang_pool_add(&smith_lang_it, copy, smith_lang_pool_text(&smith_lang_it, &copy), copy.length) ==
                UINT32_MAX) {
                smith_lang_error(lang, host, location, "the type of an argument of `%s` is too long",
                                 smith_lang_str(generic->member));
                return false;
            }
        }
        *count = smith_lang_it.count - *first;
        return true;
    }
    SmithLangArgType type = {};
    const char *why = nullptr;
    if (!smith_lang_arg_type(lang, host, replay, span, location, &type, &why, true)) {
        char shown[256];
        smith_lang_infer_written(span, shown, sizeof shown);
        smith_lang_error(lang, host, smith_lang_infer_place(host, replay, span, location),
                         "cannot tell the type of `%s`, the argument of `%s`: %s; give it a type with a cast or a "
                         "variable",
                         shown, smith_lang_str(generic->member), why);
        return false;
    }
    if (!smith_lang_infer_stage(type.written, location, first, count)) {
        smith_lang_error(lang, host, location, "the type %s is too long", type.written);
        return false;
    }
    return true;
}

// The function passed to `constexpr auto f` at `span`, staged in smith_lang_it as its name. It must be a function
// designator (a name the compiler says is a function, not a pointer to one): False after the error.
static bool smith_lang_infer_function(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                      const SmithLangGeneric *generic, size_t slot, SmithLangSpan span,
                                      uint32_t location, uint32_t *first, uint32_t *count) {
    const SmithLangGenToken *parameter = &smith_lang_gt.tokens[generic->first + generic->type_token[slot]];
    char shown[256];
    smith_lang_infer_written(span, shown, sizeof shown);
    uint32_t place = smith_lang_infer_place(host, replay, span, location);
    SmithLangArgType type = {};
    const char *why = nullptr;
    if (!smith_lang_arg_type(lang, host, replay, span, location, &type, &why, true)) {
        smith_lang_error(lang, host, place,
                         "cannot tell what `%s`, the argument of `%s`, is: %s; the parameter `%.*s` is `constexpr "
                         "auto` and takes the name of a function",
                         shown, smith_lang_str(generic->member), why, (int)parameter->length,
                         smith_lang_gt_str(parameter));
        return false;
    }
    if (!type.is_function) {
        smith_lang_error(lang, host, place,
                         "the argument `%s` of `%s` is not a function: the parameter `%.*s` is `constexpr auto`, which "
                         "takes a function by its name (a pointer is known only when the program runs)",
                         shown, smith_lang_str(generic->member), (int)parameter->length, smith_lang_gt_str(parameter));
        return false;
    }
    uint32_t mark_count = smith_lang_it.count;
    uint32_t mark_text = smith_lang_it.text_used;
    if (!smith_lang_stage_type(span, replay->instance, location, first, count) || *count != 1 ||
        smith_lang_it.tokens[*first].kind != SmithTokenKind_Identifier) {
        smith_lang_it.count = mark_count;
        smith_lang_it.text_used = mark_text;
        smith_lang_error(lang, host, place,
                         "the argument `%s` of `%s` is a function, but not written as its name: write the name of "
                         "the function (`loss`, `module::loss`), not an expression",
                         shown, smith_lang_str(generic->member));
        return false;
    }
    return true;
}

// ---- A later argument converts to the type of an `auto` parameter
// ---------------------------------------------------------------------------------

// `check_equal(my_u8, 300)` and `check_near(my_f32, 0.1)` are errors: a later argument converts to `typeof(a)` by C's
// rules, but a conversion that changes the value is refused at the call (which cast or first argument to change is in
// the message), whatever warnings the project enables. A literal (and a `constexpr` constant of the file) is judged by
// its value, and any other argument by its type: a value of a type with the range of the target is accepted, any other
// number is not. An expression of literals only (`1 << 4`), a bool, an enum or a pointer converts by C's rules.

typedef struct {
    bool known; // a number the check judges
    bool integer;
    bool floating;
    bool is_signed;
    unsigned bits;
} SmithLangNumber;

static SmithLangNumber smith_lang_number_of(const SmithHost *host, const char *spelling) {
    SmithLangNumber number = {};
    SmithTypeQuery query = {};
    if (host->type_query == nullptr || !host->type_query(host->context, spelling, (uint32_t)strlen(spelling), &query) ||
        !query.complete) {
        return number;
    }
    number.integer = query.is_integer;
    number.floating = query.is_float;
    number.is_signed = query.is_signed;
    number.bits = (unsigned)(query.size * 8);
    number.known = (number.integer || number.floating) && query.size >= 1 && query.size <= 8;
    return number;
}

// The bits of the significand of a floating type of that many bits, and a number that fits it exactly.
static unsigned smith_lang_significand_bits(unsigned bits) {
    return bits == 16 ? 11 : bits == 32 ? 24 : 53;
}

// Whether every value of a type converts to another unchanged.
static bool smith_lang_range_fits(SmithLangNumber from, SmithLangNumber to) {
    if (from.floating) return to.floating && to.bits >= from.bits;
    if (to.floating) return from.bits <= smith_lang_significand_bits(to.bits);
    if (from.is_signed == to.is_signed) return to.bits >= from.bits;
    return !from.is_signed && to.bits > from.bits;
}

static bool smith_lang_literal_fits(const SmithLangLiteral *literal, SmithLangNumber to) {
    if (to.integer) {
        if (to.bits > 64) return true;
        if (literal->is_float) {
            double low = 0;
            double high = 1;
            for (unsigned i = 0; i < to.bits - (to.is_signed ? 1U : 0U); i++)
                high *= 2;
            if (to.is_signed) low = -high;
            return literal->real == __builtin_floor(literal->real) && literal->real >= low && literal->real < high;
        }
        if (literal->overflow) return false;
        if (literal->negative && literal->magnitude != 0) {
            return to.is_signed && (to.bits == 64 ? literal->magnitude <= (uint64_t)1 << 63
                                                  : literal->magnitude <= (uint64_t)1 << (to.bits - 1));
        }
        uint64_t most = to.bits == 64 ? UINT64_MAX : ((uint64_t)1 << to.bits) - 1;
        if (to.is_signed) most >>= 1;
        return literal->magnitude <= most;
    }
    if (!to.floating || to.bits > 64) return true;
    if (literal->is_float) {
        if (to.bits != 32 || !__builtin_isfinite(literal->real)) return true;
        return (double)(float)literal->real == literal->real;
    }
    return !literal->overflow && literal->magnitude <= (uint64_t)1 << smith_lang_significand_bits(to.bits);
}

// The parameter at `position` is `typeof(x) name` for an `auto` x of the generic (it converts its argument): the
// index of the type, else -1.
static int smith_lang_converts_to(const SmithLangGeneric *generic, const SmithLangParam *param, size_t position) {
    const SmithLangGenToken *first = &smith_lang_gt.tokens[param->start];
    if (param->end - param->start != 2 || first->param == 0 || first[1].kind != SmithTokenKind_Identifier) return -1;
    int type = (int)first->param - 1;
    if ((size_t)type >= generic->type_count || generic->type_kind[type] != SmithLangSlot_Auto ||
        generic->type_position[type] == position) {
        return -1;
    }
    return type;
}

// An expression as the operand of a cast: in parentheses unless it is one name, field chain or literal.
static void smith_lang_parenthesize(char *text, size_t capacity) {
    bool simple = text[0] == '-' || text[0] == '+' ? text[1] != 0 : true;
    for (const char *c = text + (text[0] == '-' || text[0] == '+' ? 1 : 0); *c != 0; c++) {
        bool part = isalnum((unsigned char)*c) || *c == '_' || *c == '.' || *c == ':' || *c == '\'';
        simple = simple && part;
    }
    size_t length = strlen(text);
    if (simple || length + 3 > capacity) return;
    memmove(text + 1, text, length);
    text[0] = '(';
    text[length + 1] = ')';
    text[length + 2] = 0;
}

// Whether the argument at `span` converts to the staged type unchanged, as the module for `auto` says; the error
// otherwise. `first` is the argument that gave the type.
static bool smith_lang_convert_ok(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                  const SmithLangGeneric *generic, SmithLangSpan span, SmithLangSpan first_span,
                                  const uint32_t type_first, const uint32_t type_count, uint32_t location) {
    char to_spelling[256];
    char to_written[256];
    smith_lang_it_spelling(type_first, type_count, to_spelling, sizeof to_spelling);
    SmithLangNumber to = smith_lang_number_of(host, to_spelling);
    if (!to.known) return true;
    SmithLangArgType from = {};
    const char *why = nullptr;
    if (!smith_lang_arg_type(lang, host, replay, span, location, &from, &why, false)) return true;
    SmithLangNumber source = smith_lang_number_of(host, from.canonical);
    if (!source.known) return true;  // a bool, an enum, a pointer: C's rules
    bool is_literal = from.constant; // the compiler folded it: its value is what is judged
    if (is_literal ? smith_lang_literal_fits(&from.value, to) : smith_lang_range_fits(source, to)) return true;
    char argument[256];
    char first[256];
    char from_spelling[256];
    smith_lang_it_written(lang, type_first, type_count, to_written, sizeof to_written);
    smith_lang_infer_written(span, argument, sizeof argument);
    smith_lang_infer_written(first_span, first, sizeof first);
    smith_lang_smith_spelling(from.written, from_spelling, sizeof from_spelling);
    const char *name = smith_lang_str(generic->member);
    uint32_t place = smith_lang_infer_place(host, replay, span, location);
    char argument_cast[256];
    char first_cast[256];
    snprintf(argument_cast, sizeof argument_cast, "%s", argument);
    snprintf(first_cast, sizeof first_cast, "%s", first);
    smith_lang_parenthesize(argument_cast, sizeof argument_cast);
    smith_lang_parenthesize(first_cast, sizeof first_cast);
    if (is_literal) {
        smith_lang_error(
            lang, host, place,
            "the argument `%s` of `%s` converts to %s, the type of its first argument `%s`, and changes its "
            "value; cast it, `(%s)%s`, or the first argument, `(%s)%s`",
            argument, name, to_written, first, to_written, argument_cast, from_spelling, first_cast);
        return false;
    }
    smith_lang_error(lang, host, place,
                     "the argument `%s` of `%s` is %s and converts to %s, the type of its first argument `%s`, which "
                     "may change its value; cast it, `(%s)%s`, or the first argument, `(%s)%s`",
                     argument, name, from_spelling, to_written, first, to_written, argument_cast, from_spelling,
                     first_cast);
    return false;
}

// A call that leaves out `constexpr type` arguments: the error names each and shows the call with them in place.
static void smith_lang_missing_type(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                    const SmithLangSpan spans[], uint32_t location) {
    SmithLangParam params[smith_lang_call_params_max];
    size_t count = smith_lang_generic_params(generic, params, smith_lang_call_params_max);
    char names[128] = "";
    char call[512] = "";
    size_t next = 0;
    for (size_t i = 0; i < count && count != SIZE_MAX; i++) { // the parameters, the types where the call has none
        char piece[256];
        if (params[i].kind == SmithLangSlot_Type) {
            const SmithLangGenToken *name = &smith_lang_gt.tokens[params[i].start + 2];
            snprintf(piece, sizeof piece, "%.*s", (int)name->length, smith_lang_gt_str(name));
            snprintf(names + strlen(names), sizeof names - strlen(names), "%s`%s`", names[0] != 0 ? ", " : "", piece);
        } else {
            smith_lang_infer_written(spans[next++], piece, sizeof piece);
        }
        snprintf(call + strlen(call), sizeof call - strlen(call), "%s%s", i > 0 ? ", " : "", piece);
    }
    smith_lang_error(lang, host, location, "`%s` is called without its type %s; pass the type: %s::%s(%s)",
                     smith_lang_str(generic->member), names, smith_lang_str(generic->module),
                     smith_lang_str(generic->member), call);
}

// Whether the parameter at `position` is a `constexpr type T`, which the call passes and no instance has.
static bool smith_lang_position_is_type(const SmithLangGeneric *generic, size_t position) {
    for (size_t k = 0; k < generic->type_count; k++) { // the types
        if (generic->type_kind[k] == SmithLangSlot_Type && generic->type_position[k] == position) return true;
    }
    return false;
}

// Whether the parameter at `position` is compile-time (a `constexpr type T` or a `constexpr auto f`): the call passes
// it, and the instance does not take it.
static bool smith_lang_position_is_static(const SmithLangGeneric *generic, size_t position) {
    for (size_t k = 0; k < generic->type_count; k++) { // the slots
        bool is_static = generic->type_kind[k] == SmithLangSlot_Type || generic->type_kind[k] == SmithLangSlot_Function;
        if (is_static && generic->type_position[k] == position) return true;
    }
    return false;
}

// Whether the call has the arguments the generic takes: the error otherwise.
static bool smith_lang_call_arity(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                  const SmithLangSpan spans[], size_t count, uint32_t location) {
    bool has_pack = smith_lang_has_pack(generic);
    size_t fixed = generic->param_count - (has_pack ? 1U : 0U); // the parameters before `auto... rest`
    size_t passed = 0;                                          // the `constexpr type` parameters
    for (size_t i = 0; i < generic->param_count; i++)
        passed += smith_lang_position_is_type(generic, i) ? 1 : 0;
    if (!has_pack && count != SIZE_MAX && count != generic->param_count && passed > 0 &&
        count + passed == generic->param_count) {
        smith_lang_missing_type(lang, host, generic, spans, location);
        return false;
    }
    if (count == SIZE_MAX || (has_pack ? count < fixed : count != generic->param_count)) {
        smith_lang_error(lang, host, location, "`%s` takes %s%zu argument%s, and the call has %zu",
                         smith_lang_str(generic->member), has_pack ? "at least " : "", fixed,
                         has_pack && fixed == 1 ? "" : "s",
                         count == SIZE_MAX ? (size_t)smith_lang_call_params_max : count);
        return false;
    }
    return true;
}

typedef struct {
    uint32_t first[smith_lang_entries_max]; // the staged types, in smith_lang_it
    uint32_t count[smith_lang_entries_max];
    size_t entries;
} SmithLangCallTypes;

// The types of a call: each slot's, then the arguments of `auto... rest`, staged in smith_lang_it, and the check that a
// later argument converts to the type of an `auto` parameter. False after the error.
static bool smith_lang_call_types(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                  const SmithLangGeneric *generic, const SmithLangSpan spans[], size_t count,
                                  uint32_t location, SmithLangCallTypes *types) {
    bool has_pack = smith_lang_has_pack(generic);
    size_t fixed = generic->param_count - (has_pack ? 1U : 0U);
    size_t slots = generic->type_count - (has_pack ? 1U : 0U);
    types->entries = slots + (has_pack ? count - fixed : 0U);
    bool good = true;
    for (size_t k = 0; good && k < types->entries; k++) { // the types: the slots, then the list's
        uint8_t kind = smith_lang_entry_kind(generic, k);
        SmithLangSpan span = k < slots ? spans[generic->type_position[k]] : spans[fixed + (k - slots)];
        if (kind == SmithLangSlot_Auto) {
            good =
                smith_lang_infer_auto(lang, host, replay, generic, span, location, &types->first[k], &types->count[k]);
        } else if (kind == SmithLangSlot_Function) {
            good = smith_lang_infer_function(lang, host, replay, generic, k, span, location, &types->first[k],
                                             &types->count[k]);
        } else if (span.start == span.end ||
                   !smith_lang_stage_type(span, replay->instance, location, &types->first[k], &types->count[k])) {
            smith_lang_error(lang, host, location, "the type argument of `%s` is missing or too long",
                             smith_lang_str(generic->member));
            good = false;
        }
    }
    SmithLangParam params[smith_lang_call_params_max];
    smith_lang_generic_params(generic, params, smith_lang_call_params_max);
    for (size_t i = 0; good && i < count && i < generic->param_count; i++) { // a later argument converts to `auto`'s
        int type = smith_lang_converts_to(generic, &params[i], i);
        if (type < 0) continue;
        good = smith_lang_convert_ok(lang, host, replay, generic, spans[i], spans[generic->type_position[type]],
                                     types->first[type], types->count[type], location);
    }
    return good;
}

// A call to a generic: `args` is the `( ... )` with its parentheses, in smith_lang_gt at [first, last). The types are
// taken out of the arguments (a `constexpr type T` is passed; an `auto` parameter's is its argument's) and asked for,
// and the start of the call is written; what follows it (the other arguments and the end) is left in the replay for it
// to write.
static bool smith_lang_call_finish(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                   uint32_t generic_index, uint32_t first, uint32_t last, uint32_t location,
                                   uint32_t real) {
    const SmithLangGeneric *generic = &smith_lang_generics[generic_index];
    SmithLangSpan spans[smith_lang_call_params_max];
    uint32_t begin = first + 1;
    uint32_t end = last - 1;
    if (!smith_lang_pack_expand_range(lang, host, replay, &begin, &end)) return false;
    size_t count = smith_lang_split_args(begin, end, spans, smith_lang_call_params_max);
    if (!smith_lang_call_arity(lang, host, generic, spans, count, location)) return false;
    // The header pass writes declarations only: a call in a body needs no instance, so it stays a plain call.
    if (host->expr_type == nullptr && host->declarations_only) return false;
    uint32_t mark[2] = {smith_lang_it.count, smith_lang_it.text_used};
    SmithLangCallTypes types;
    replay->typed_count = 0;
    if (replay->typed_last > replay->typed_first) { // the arguments as the instance writes them, to type
        SmithLangSpan typed[smith_lang_call_params_max];
        size_t typed_count =
            smith_lang_split_args(replay->typed_first + 1, replay->typed_last - 1, typed, smith_lang_call_params_max);
        if (typed_count == count) {
            memcpy(replay->spans, spans, count * sizeof spans[0]);
            memcpy(replay->typed, typed, count * sizeof typed[0]);
            replay->typed_count = count;
        }
    }
    bool typed_ok = smith_lang_call_types(lang, host, replay, generic, spans, count, location, &types);
    replay->typed_count = 0;
    if (!typed_ok) {
        smith_lang_it_release(mark);
        return false;
    }
    int index =
        smith_lang_instance_request(lang, host, generic_index, types.first, types.count, types.entries, mark, location);
    if (index < 0) return false;
    smith_lang_gen_call_start(lang, host, generic, &smith_lang_instances[index], location, real, replay->instance_id);
    if (real != UINT32_MAX && lang->call_site_count < smith_lang_call_sites_max) {
        lang->call_sites[lang->call_site_count++] =
            (SmithLangCallSite){.location = real, .generic = generic_index, .instance = (uint32_t)index};
    }
    static const char closers[] = {')', '}', ';', ')'}; // written in the reverse, last first
    for (size_t i = 0; i < sizeof closers; i++)         // the end of the statement expression
        smith_lang_replay_literal(replay, closers[i], location);
    bool more = false;
    for (size_t i = count; i > 0; i--) { // the arguments that are no type, last first
        if (smith_lang_position_is_static(generic, i - 1)) continue;
        if (more) smith_lang_replay_literal(replay, ',', location);
        smith_lang_replay_push(replay, spans[i - 1].start, spans[i - 1].end, 0);
        more = true;
    }
    return true;
}

// ---- Resolving a name to a generic
// -----------------------------------------------------------------------------------------

static void smith_lang_generics_load(SmithLang *lang, const SmithHost *host, const char *module) {
    for (size_t i = 0; i < lang->generic_modules_count; i++) { // the modules read
        if (strcmp(smith_lang_str(lang->generic_modules[i]), module) == 0) return;
    }
    if (lang->generic_modules_count >= smith_lang_generic_modules_max || host->read_header == nullptr) return;
    lang->generic_modules[lang->generic_modules_count++] = smith_lang_store(lang, module, strlen(module));
    char path[smith_lang_name_max];
    smith_lang_module_to_path(module, path, sizeof path);
    const char *text = nullptr;
    uint32_t length = 0;
    if (host->read_header(host->context, path, &text, &length)) smith_lang_generics_read(lang, text, length);
}

// The generic `member` of `module`, reading the module's header if it was not read yet. Its index, or -1.
static int smith_lang_generic_resolve(SmithLang *lang, const SmithHost *host, const char *module, size_t module_length,
                                      const char *member, size_t member_length) {
    int found = smith_lang_generic_find(lang, module, module_length, member, member_length);
    if (found >= 0 || lang->cap_frames > 0 || module_length >= smith_lang_name_max) return found;
    char name[smith_lang_name_max];
    memcpy(name, module, module_length);
    name[module_length] = 0;
    if (lang->is_module && strcmp(name, lang->module) == 0) return -1;
    smith_lang_generics_load(lang, host, name);
    return smith_lang_generic_find(lang, module, module_length, member, member_length);
}

// The name a module's member has in C: the module's prefix and the member.
static void smith_lang_member_name(const char *module, size_t module_length, const char *member, size_t member_length,
                                   char *out, size_t capacity) {
    size_t used = 0;
    for (size_t i = 0; i < module_length && used + 2 < capacity; i++) { // a::b is a_b
        if (module[i] == ':') {
            if (i + 1 < module_length && module[i + 1] == ':') i += 1;
            out[used++] = '_';
        } else {
            out[used++] = module[i];
        }
    }
    out[used++] = '_';
    size_t take = member_length < capacity - used - 1 ? member_length : capacity - used - 1;
    memcpy(out + used, member, take);
    out[used + take] = 0;
}

// The name a call to `module::member` is written by: `m_f`, or the C name an export chose (`[[smith::c_name("m_g")]]`),
// which the importing file read from the header.
static void smith_lang_callee_name(const SmithLang *lang, const char *module, size_t module_length, const char *member,
                                   size_t member_length, char *out, size_t capacity) {
    smith_lang_member_name(module, module_length, member, member_length, out, capacity);
    const SmithLangCName *chosen = lang->cnames_count > 0 ? smith_lang_cname_find(lang, out, strlen(out)) : nullptr;
    if (chosen != nullptr) snprintf(out, capacity, "%s", smith_lang_str(chosen->c_name));
}

// A call that was kept as a group in a generic's body, now that its types are known: to a generic, it becomes the call
// of the instance; to anything else it is the plain call it always was.
// ---- Built lists and `for constexpr`: the statements of a body that are gone or repeat
// ----------------------------------------------------------------------------------------

// Built lists are gone: `auto name = {...};` and `name = {name..., x};` in a generic's body are this error, and the
// statement is skipped. Returns the index of the name, or 0 when the frame's next tokens are no such statement.
static uint32_t smith_lang_list_gone_at(const SmithLangReplay *replay, const SmithLangFrame *frame) {
    const SmithLangGenToken *first = &smith_lang_gt.tokens[frame->pos];
    if (replay->instance == nullptr || frame->pos + 4 >= frame->end || first[0].kind != SmithTokenKind_Identifier) {
        return 0;
    }
    bool declare = smith_lang_gt_is(first, SmithTokenKind_Identifier, "auto") &&
                   first[1].kind == SmithTokenKind_Identifier && first[1].special == SmithLangGen_Plain &&
                   smith_lang_gt_punct(&first[2], "=") && smith_lang_gt_punct(&first[3], "{");
    bool grow = first[0].special == SmithLangGen_Plain && smith_lang_gt_punct(&first[1], "=") &&
                smith_lang_gt_punct(&first[2], "{") && first[3].kind == SmithTokenKind_Identifier &&
                smith_lang_gt_punct(&first[4], "...") && !smith_lang_pack_marked(replay->generic, &first[3]);
    return declare ? frame->pos + 1 : grow ? frame->pos : 0;
}

static bool smith_lang_list_gone(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                 SmithLangFrame *frame) {
    uint32_t name = smith_lang_list_gone_at(replay, frame);
    if (name == 0) return false;
    SmithLangGenToken *token = &smith_lang_gt.tokens[name];
    uint32_t place = smith_lang_gen_place(host, replay->generic, token, replay->location);
    smith_lang_error(lang, host, place,
                     "built lists are gone: write a struct or a tuple type (`struct { i32, f32 } %.*s = {a, b};`, "
                     "named with a typedef outside the generic) and index it; the arguments of a generic are "
                     "`auto... rest`",
                     (int)token->length, smith_lang_gt_str(token));
    uint32_t after = smith_lang_cx_skip_group(name + 2, frame->end);
    frame->pos = after == UINT32_MAX ? frame->end : after;
    while (frame->pos < frame->end && !smith_lang_gt_punct(&smith_lang_gt.tokens[frame->pos], ";"))
        frame->pos += 1;
    if (frame->pos < frame->end) frame->pos += 1;
    return true;
}

// What a frame that is no plain run of tokens does when it is on top: false when its tokens are to be written as any
// others.
static bool smith_lang_replay_action(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                     SmithLangFrame *frame);

static bool smith_lang_replay_eval(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                   uint32_t first, uint32_t last, uint32_t place, const char *what, int64_t *value) {
    static SmithLangCx evaluation;
    evaluation =
        (SmithLangCx){.lang = lang, .host = host, .instance = replay->instance, .replay = replay, .location = place};
    if (first >= last || !smith_lang_cx_eval(&evaluation, first, last, value)) {
        smith_lang_error(lang, host, place, "in `for constexpr`: the %s must be a compile-time constant: %s", what,
                         first >= last ? "it is empty" : evaluation.message);
        return false;
    }
    return true;
}

// The step of a `for constexpr`: `i++`, `++i`, `i--`, `--i`, `i += n`, `i -= n`, `i = n`. False after the error.
static bool smith_lang_loop_step(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                 const SmithLangLoop *loop, SmithLangVar *var, uint32_t place) {
    const SmithLangGenToken *tokens = &smith_lang_gt.tokens[loop->step_start];
    size_t count = loop->step_end - loop->step_start;
    bool named_first = count >= 2 && tokens[0].length == var->length &&
                       memcmp(smith_lang_gt_str(&tokens[0]), var->name, var->length) == 0;
    bool named_last = count == 2 && tokens[1].length == var->length &&
                      memcmp(smith_lang_gt_str(&tokens[1]), var->name, var->length) == 0;
    int64_t amount = 1;
    int sign = 0;
    bool assign = false;
    if (named_first && count == 2 && smith_lang_gt_punct(&tokens[1], "++")) sign = 1;
    if (named_first && count == 2 && smith_lang_gt_punct(&tokens[1], "--")) sign = -1;
    if (named_last && smith_lang_gt_punct(&tokens[0], "++")) sign = 1;
    if (named_last && smith_lang_gt_punct(&tokens[0], "--")) sign = -1;
    if (named_first && count >= 3 &&
        (smith_lang_gt_punct(&tokens[1], "+=") || smith_lang_gt_punct(&tokens[1], "-=") ||
         smith_lang_gt_punct(&tokens[1], "="))) {
        assign = smith_lang_gt_punct(&tokens[1], "=");
        sign = smith_lang_gt_punct(&tokens[1], "-=") ? -1 : 1;
        if (!smith_lang_replay_eval(lang, host, replay, loop->step_start + 2, loop->step_end, place, "step", &amount))
            return false;
    }
    if (sign == 0) {
        smith_lang_error(
            lang, host, place,
            "the step of `for constexpr` changes its variable `%s`: `%s++`, `%s--`, `%s += n`, `%s -= n` or "
            "`%s = n`",
            var->name, var->name, var->name, var->name, var->name, var->name);
        return false;
    }
    var->value = assign ? amount : (int64_t)((uint64_t)var->value + (uint64_t)(sign * amount));
    return true;
}

// One turn of the `for constexpr` frame on top: the step (after a turn), the condition, then the body once more, or the
// end of the loop. The body is a frame above it, so the loop is not a recursion of C.
static void smith_lang_loop_run(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                SmithLangFrame *frame) {
    SmithLangLoop loop = frame->loop;
    uint32_t place = frame->place;
    SmithLangVar *var = &replay->vars[replay->var_count - 1];
    int64_t go = 0;
    bool good = !loop.stepping || smith_lang_loop_step(lang, host, replay, &loop, var, place);
    good = good && smith_lang_replay_eval(lang, host, replay, loop.cond_start, loop.cond_end, place, "condition", &go);
    if (good && go != 0 && ++loop.iterations > smith_lang_loop_iterations_max) {
        smith_lang_error(lang, host, place,
                         "`for constexpr` ran %zu turns without ending: is its condition ever false?",
                         smith_lang_loop_iterations_max);
        good = false;
    }
    if (!good || go == 0) { // the end of the loop
        replay->var_count -= 1;
        replay->count -= 1;
        return;
    }
    loop.stepping = true;
    frame->loop = loop;
    if (loop.wrap) smith_lang_replay_literal(replay, '}', place);
    smith_lang_replay_push(replay, loop.body_start, loop.body_end, 0);
    if (loop.wrap) smith_lang_replay_literal(replay, '{', place);
}

// `for constexpr (init; condition; step) body`, at the `for` of the frame: C's own for, run now. The variable is a
// number in each turn of the body (`i` is written as its value, `rest[i]` is the element), and the loop is written out
// turn by turn.
static void smith_lang_replay_for(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                  SmithLangFrame *frame) {
    uint32_t at = frame->pos;
    uint32_t place = smith_lang_gen_place(host, replay->generic, &smith_lang_gt.tokens[at], replay->location);
    uint32_t open = at + 1;
    frame->pos = frame->end; // after an error nothing more of this body is written
    if (open >= frame->end || !smith_lang_gt_punct(&smith_lang_gt.tokens[open], "(")) {
        smith_lang_error(lang, host, place, "`for constexpr` is followed by `(init; condition; step)` in parentheses");
        return;
    }
    uint32_t body_start = smith_lang_cx_skip_group(open, frame->end);
    uint32_t body_end = body_start == UINT32_MAX ? UINT32_MAX : smith_lang_cx_statement_end(body_start, frame->end);
    if (body_end == UINT32_MAX) {
        smith_lang_error(lang, host, place, "this `for constexpr` has no body that ends");
        return;
    }
    frame->pos = body_end; // the statement is replaced by its turns, or by nothing after an error
    uint32_t semis[3];
    size_t semi_count = 0;
    int depth = 0;
    for (uint32_t i = open + 1; i < body_start - 1; i++) { // the two `;` of the head
        const SmithLangGenToken *token = &smith_lang_gt.tokens[i];
        depth += smith_lang_gt_open(token) ? 1 : smith_lang_gt_close(token) ? -1 : 0;
        if (depth == 0 && smith_lang_gt_punct(token, ";") && semi_count < 3) semis[semi_count++] = i;
    }
    uint32_t equals = 0;
    for (uint32_t i = open + 1; semi_count == 2 && i < semis[0] && equals == 0; i++)
        equals = smith_lang_gt_punct(&smith_lang_gt.tokens[i], "=") ? i : 0;
    const SmithLangGenToken *variable = equals > open + 2 ? &smith_lang_gt.tokens[equals - 1] : nullptr;
    if (semi_count != 2 || variable == nullptr || variable->kind != SmithTokenKind_Identifier ||
        variable->length >= smith_lang_pack_name_max) {
        smith_lang_error(lang, host, place,
                         "`for constexpr` is written `for constexpr (usize i = 0; condition; step) body`: it declares "
                         "its variable");
        return;
    }
    int64_t start = 0;
    if (replay->var_count >= smith_lang_vars_max) {
        smith_lang_error(lang, host, place, "`for constexpr` loops are nested at most 8 deep");
        return;
    }
    if (!smith_lang_replay_eval(lang, host, replay, equals + 1, semis[0], place, "initial value", &start)) return;
    SmithLangVar *var = &replay->vars[replay->var_count++];
    *var = (SmithLangVar){.length = (uint16_t)variable->length, .value = start};
    snprintf(var->name, sizeof var->name, "%.*s", (int)variable->length, smith_lang_gt_str(variable));
    bool block = smith_lang_gt_punct(&smith_lang_gt.tokens[body_start], "{");
    SmithLangLoop loop = {.cond_start = semis[0] + 1,
                          .cond_end = semis[1],
                          .step_start = semis[1] + 1,
                          .step_end = body_start - 1,
                          .body_start = body_start,
                          .body_end = body_end,
                          .wrap = !block};
    if (!smith_lang_replay_push(replay, 0, 0, 0)) {
        replay->var_count -= 1;
        smith_lang_error(lang, host, place, "this `for constexpr` is nested too deep");
        return;
    }
    SmithLangFrame *made = &replay->frames[replay->count - 1];
    made->action = SmithLangFrame_Loop;
    made->place = place;
    made->loop = loop;
}

// A call that holds a call, in the body of an instance: its arguments are written to the pool first (with the
// parameters of the instance as values, `(*(int *)0)`, which the compiler can type where the instance is not declared
// yet), so that the compiler can type them as they will stand; then the call is written, from the arguments as they
// were. The frames do it in turn, so a call in a call is not a recursion of C.
static void smith_lang_typed_start(SmithLang *lang, SmithLangReplay *replay, SmithLangFrame *frame) {
    SmithLangFrame *finish = &replay->frames[frame->source];
    finish->started = replay->typing;
    finish->pool_first = smith_lang_gt.count;
    finish->pool_text = smith_lang_gt.text_used;
    replay->typing = true;
    lang->cap_frames += 1; // the tokens are kept, not written
    replay->count -= 1;
}

static void smith_lang_typed_finish(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                    SmithLangFrame *frame) {
    const SmithLangFrame done = *frame;
    replay->count -= 1;
    lang->cap_frames -= 1;
    replay->typing = done.started;
    replay->typed_first = done.pool_first;
    replay->typed_last = smith_lang_gt.count;
    uint32_t captured_end = smith_lang_gt.count;
    bool written = smith_lang_call_finish(lang, host, replay, done.generic_index, done.group + 1, done.end, done.place,
                                          UINT32_MAX);
    replay->typed_first = 0;
    replay->typed_last = 0;
    if (smith_lang_gt.count == captured_end) { // the arguments are not needed any more (unless a list was expanded)
        smith_lang_gt.count = done.pool_first;
        smith_lang_gt.text_used = done.pool_text;
    }
    if (!written) {
        const SmithLangGenToken *token = &smith_lang_gt.tokens[done.group];
        const char *text = smith_lang_gt_str(token);
        const char *colons = text + token->length;
        while (colons > text && !(colons[0] == ':' && colons[-1] == ':'))
            colons -= 1;
        size_t module_length = colons > text ? (size_t)(colons - 1 - text) : 0;
        char callee[smith_lang_name_max];
        smith_lang_member_name(text, module_length, colons + 1, token->length - module_length - 2, callee,
                               sizeof callee);
        smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, callee, done.place, replay->instance_id);
        smith_lang_replay_push(replay, done.group + 1, done.end, 0);
    }
}

static bool smith_lang_replay_action(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                     SmithLangFrame *frame) {
    switch ((SmithLangFrameAction)frame->action) {
    case SmithLangFrame_Loop:
        smith_lang_loop_run(lang, host, replay, frame);
        return true;
    case SmithLangFrame_TypedStart:
        smith_lang_typed_start(lang, replay, frame);
        return true;
    case SmithLangFrame_TypedFinish:
        smith_lang_typed_finish(lang, host, replay, frame);
        return true;
    case SmithLangFrame_Tokens:
        break;
    }
    return false;
}

// An identifier of the body that is spelled like an element of the argument list (`rest_0`) would be that element.
static void smith_lang_pack_clash(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                  const SmithLangGenToken *token, uint32_t place) {
    uint32_t at = (uint32_t)(token - smith_lang_gt.tokens);
    if (token->kind != SmithTokenKind_Identifier || token->special != SmithLangGen_Plain || token->param != 0 ||
        token->length < 3 || at < replay->generic->first || at >= replay->generic->first + replay->generic->count ||
        smith_lang_member_before(at)) { // only the generic's own tokens: the elements `rest...` expands to are not
        return;
    }
    const char *text = smith_lang_gt_str(token);
    if (text[token->length - 1] < '0' || text[token->length - 1] > '9') return;
    const SmithLangGenToken *name = smith_lang_pack_name(replay->generic);
    size_t pack_length = name->length;
    if (token->length > pack_length + 1 && memcmp(text, smith_lang_gt_str(name), pack_length) == 0 &&
        text[pack_length] == '_' && strspn(text + pack_length + 1, "0123456789") == token->length - pack_length - 1) {
        smith_lang_error(lang, host, place,
                         "`%.*s` is the name of one of the arguments `%.*s`: give the variable another name",
                         (int)token->length, text, (int)pack_length, smith_lang_gt_str(name));
    }
}

// Whether an argument of the call [first, last) is an expression with a parameter of the instance in it (`*first`,
// `piece.m`, `first + 1`), not just the parameter: the parameters are not declared while the body is written, so the
// arguments are typed with them written as values of their types.
static bool smith_lang_expression_of_param(const SmithLangReplay *replay, uint32_t first, uint32_t last) {
    for (uint32_t at = first; at < last; at++) { // bounded by the call
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (token->kind != SmithTokenKind_Identifier || token->special != SmithLangGen_Plain || token->param != 0 ||
            smith_lang_member_before(at) ||
            smith_lang_param_type_of(replay->generic, replay->instance, (SmithLangSpan){at, at + 1}) < 0) {
            continue;
        }
        const SmithLangGenToken *before = &smith_lang_gt.tokens[at - 1]; // the span holds the call's parentheses
        const SmithLangGenToken *after = &smith_lang_gt.tokens[at + 1];
        bool starts = smith_lang_gt_punct(before, ",") || smith_lang_gt_punct(before, "(");
        bool ends = smith_lang_gt_punct(after, ",") || smith_lang_gt_punct(after, ")");
        if (!starts || !ends) return true;
    }
    return false;
}

static void smith_lang_replay_group(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay, uint32_t begin) {
    SmithLangFrame *frame = &replay->frames[replay->count - 1];
    SmithLangGenToken *token = &smith_lang_gt.tokens[begin];
    uint32_t end = smith_lang_group_end(begin, frame->end);
    frame->pos = end + 1;
    const char *text = smith_lang_gt_str(token);
    const char *colons = text + token->length;
    while (colons > text && !(colons[0] == ':' && colons[-1] == ':'))
        colons -= 1;
    size_t module_length = colons > text ? (size_t)(colons - 1 - text) : 0;
    size_t member_length = token->length - module_length - 2;
    int found = smith_lang_generic_resolve(lang, host, text, module_length, colons + 1, member_length);
    uint32_t location = smith_lang_gen_place(host, replay->generic, token, replay->location);
    if (found >= 0 && replay->instance != nullptr &&
        (smith_lang_span_has_group((SmithLangSpan){begin + 1, end}) || smith_lang_fields_in(begin + 1, end) ||
         smith_lang_expression_of_param(replay, begin + 1, end)) &&
        replay->count + 3 <= smith_lang_replay_depth_max) {
        // Its arguments hold a call: they are written to the pool first, to be typed (smith_lang_typed_start).
        smith_lang_replay_push(replay, 0, 0, 0);
        SmithLangFrame *finish = &replay->frames[replay->count - 1];
        finish->action = SmithLangFrame_TypedFinish;
        finish->group = begin;
        finish->end = end;
        finish->generic_index = (uint16_t)found;
        finish->place = location;
        size_t finish_at = replay->count - 1;
        smith_lang_replay_push(replay, begin + 1, end, 0);
        smith_lang_replay_push(replay, 0, 0, 0);
        SmithLangFrame *start = &replay->frames[replay->count - 1];
        start->action = SmithLangFrame_TypedStart;
        start->source = (int32_t)finish_at;
        return;
    }
    if (found >= 0 &&
        smith_lang_call_finish(lang, host, replay, (uint32_t)found, begin + 1, end, location, UINT32_MAX)) {
        return;
    }
    char callee[smith_lang_name_max];
    smith_lang_callee_name(lang, text, module_length, colons + 1, member_length, callee, sizeof callee);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, callee, location, replay->instance_id);
    smith_lang_replay_push(replay, begin + 1, end, 0);
}

// A parameter of the instance in tokens that the compiler only types (an argument that holds a call): the instance's
// parameters are not declared yet while its body is written, so a name of one stands for a value of its type,
// `(*(int *)0)`, never evaluated. True when the token was one.
static bool smith_lang_typing_param(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                    const SmithLangGenToken *token, uint32_t place) {
    uint32_t at = (uint32_t)(token - smith_lang_gt.tokens);
    if (token->kind != SmithTokenKind_Identifier || token->param != 0 || smith_lang_member_before(at)) return false;
    int own = smith_lang_param_type_of(replay->generic, replay->instance, (SmithLangSpan){at, at + 1});
    if (own < 0) return false;
    static const char *const opening[] = {"(", "*", "("};
    for (size_t i = 0; i < 3; i++)
        smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, opening[i], place, replay->instance_id);
    for (uint32_t i = 0; i < replay->instance->arg_count[own]; i++) { // the type's tokens
        const SmithLangGenToken *part = &smith_lang_it.tokens[replay->instance->arg_first[own] + i];
        char text[256];
        size_t length = part->length < sizeof text - 1 ? part->length : sizeof text - 1;
        memcpy(text, smith_lang_pool_text(&smith_lang_it, part), length);
        text[length] = 0;
        smith_lang_gen_put(lang, host, (SmithTokenKind)part->kind, text, place, replay->instance_id);
    }
    static const char *const closing[] = {"*", ")", "0", ")"};
    for (size_t i = 0; i < 4; i++) {
        smith_lang_gen_put(lang, host, i == 2 ? SmithTokenKind_Number : SmithTokenKind_Punctuator, closing[i], place,
                           replay->instance_id);
    }
    return true;
}

// Writes the tokens the replay holds, in order. A group becomes its call; a token that names T becomes the type.
static void smith_lang_replay_run(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay) {
    for (size_t step = 0; replay->count > 0 && step < smith_lang_replay_steps_max; step++) { // bounded
        SmithLangFrame *frame = &replay->frames[replay->count - 1];
        if (frame->literal != 0) {
            char text[2] = {frame->literal, 0};
            uint32_t frame_place = frame->place;
            replay->count -= 1;
            smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, text,
                               frame_place != 0 ? frame_place : replay->location, replay->instance_id);
            continue;
        }
        if (frame->action != SmithLangFrame_Tokens && smith_lang_replay_action(lang, host, replay, frame)) continue;
        if (frame->pos >= frame->end) {
            replay->count -= 1;
            continue;
        }
        SmithLangGenToken *token = &smith_lang_gt.tokens[frame->pos];
        if (token->special == SmithLangGen_CallBegin) {
            smith_lang_replay_group(lang, host, replay, frame->pos);
            continue;
        }
        if (token->special == SmithLangGen_IfConstexpr) {
            smith_lang_replay_if(lang, host, replay, frame);
            continue;
        }
        if (token->special == SmithLangGen_ForConstexpr) {
            smith_lang_replay_for(lang, host, replay, frame);
            continue;
        }
        if (smith_lang_fields_replay(lang, host, replay, frame)) continue;
        if (smith_lang_list_gone(lang, host, replay, frame)) continue;
        SmithLangPackUse use = smith_lang_pack_use(lang, host, replay, frame->pos, frame->end);
        if (use.kind == SmithLangPack_None && smith_lang_pack_comma_before_empty(replay, frame->pos, frame->end)) {
            frame->pos += 1; // the comma before an empty `rest...` goes with it
            continue;
        }
        if (use.kind != SmithLangPack_None) {
            bool empty = use.kind == SmithLangPack_Expand && use.args.count == 0;
            if (empty)
                use.consumed += smith_lang_pack_comma_after_empty(frame->pos, frame->begin, frame->end, use.consumed);
            smith_lang_pack_write(lang, host, replay, &use);
            frame->pos += use.consumed;
            continue;
        }
        frame->pos += 1;
        if (token->special == SmithLangGen_Plain && replay->generic != nullptr &&
            smith_lang_has_pack(replay->generic)) {
            smith_lang_pack_clash(lang, host, replay, token,
                                  smith_lang_gen_place(host, replay->generic, token, replay->location));
        }
        if (token->special == SmithLangGen_Plain && replay->typing &&
            smith_lang_typing_param(lang, host, replay, token,
                                    smith_lang_gen_place(host, replay->generic, token, replay->location))) {
            continue;
        }
        if (token->special == SmithLangGen_Plain) {
            smith_lang_gen_put_subst(lang, host, replay->generic, replay->instance, token, replay->location,
                                     replay->instance_id, replay->instance_id == 0);
        }
    }
}

// ---- Writing the instances
// ----------------------------------------------------------------------------------------------------

static void smith_lang_instance_emit(SmithLang *lang, const SmithHost *host, size_t index) {
    const SmithLangInstance *instance = &smith_lang_instances[index];
    const SmithLangGeneric *generic = &smith_lang_generics[instance->generic];
    uint32_t id = (uint32_t)index + 1;
    uint32_t location = instance->call_location;
    lang->instance_depth = instance->depth;
    lang->instance_current = id;
    if (host->instance != nullptr) {
        host->instance(host->context, id, instance->parent, instance->call_location,
                       smith_lang_str(instance->description),
                       smith_lang_gen_place(host, generic, &smith_lang_gt.tokens[generic->first + generic->name_index],
                                            instance->call_location));
    }
    lang->carry_line_start = true;
    smith_lang_gen_note_break(lang);
    smith_lang_fields_declare(lang, host, generic, instance, id, location);
    smith_lang_gen_declaration(lang, host, generic, instance, location, id);
    static const SmithLangPiece weak[] = {{SmithTokenKind_Punctuator, "["},    {SmithTokenKind_Punctuator, "["},
                                          {SmithTokenKind_Identifier, "gnu"},  {SmithTokenKind_Punctuator, "::"},
                                          {SmithTokenKind_Identifier, "weak"}, {SmithTokenKind_Punctuator, "]"},
                                          {SmithTokenKind_Punctuator, "]"}};
    static const SmithLangPiece hidden[] = {
        {SmithTokenKind_Punctuator, "["},          {SmithTokenKind_Punctuator, "["},
        {SmithTokenKind_Identifier, "gnu"},        {SmithTokenKind_Punctuator, "::"},
        {SmithTokenKind_Identifier, "visibility"}, {SmithTokenKind_Punctuator, "("},
        {SmithTokenKind_String, "\"hidden\""},     {SmithTokenKind_Punctuator, ")"},
        {SmithTokenKind_Punctuator, "]"},          {SmithTokenKind_Punctuator, "]"}};
    static const SmithLangPiece inlined[] = {{SmithTokenKind_Punctuator, "["},
                                             {SmithTokenKind_Punctuator, "["},
                                             {SmithTokenKind_Identifier, "gnu"},
                                             {SmithTokenKind_Punctuator, "::"},
                                             {SmithTokenKind_Identifier, "always_inline"},
                                             {SmithTokenKind_Punctuator, "]"},
                                             {SmithTokenKind_Punctuator, "]"}};
    lang->carry_line_start = true;
    smith_lang_gen_note_break(lang);
    smith_lang_gen_puts(lang, host, weak, sizeof weak / sizeof weak[0], location, id);
    smith_lang_gen_puts(lang, host, hidden, sizeof hidden / sizeof hidden[0], location, id);
    if (generic->is_inline) smith_lang_gen_puts(lang, host, inlined, sizeof inlined / sizeof inlined[0], location, id);
    smith_lang_gen_signature(lang, host, generic, instance, true, location, id);
    SmithLangReplay *replay = &smith_lang_replay;
    *replay = (SmithLangReplay){};
    replay->instance = instance;
    replay->instance_id = id;
    replay->generic = generic;
    replay->location = location;
    smith_lang_replay_push(replay, generic->first + generic->body_index, generic->first + generic->count, 0);
    smith_lang_replay_run(lang, host, replay);
}

// At the end of the file: the code of each instance asked for, and of those they ask for in turn.
static void smith_lang_generic_instances(SmithLang *lang, const SmithHost *host) {
    for (size_t guard = 0; lang->instances_emitted < lang->instances_count && guard < smith_lang_instances_max;
         guard++) {
        smith_lang_instance_emit(lang, host, lang->instances_emitted++);
    }
    lang->instance_depth = 0;
    lang->instance_current = 0;
}

// ---- Reading a call
// ---------------------------------------------------------------------------------------------------------

// A call is read by the feed loop, not by a function that calls the lowering again (the lowering does not recurse): the
// call pushes a frame, and each token the loop then lowers is shown to `smith_lang_generic_advance`, which counts the
// brackets and, at the `)` that closes the call, finishes it. A call in the arguments of another is a frame above it.

static bool smith_lang_generic_push_frame(SmithLang *lang, const SmithHost *host, bool record, uint32_t generic_index,
                                          uint32_t location, uint32_t member_location) {
    if (lang->call_top >= smith_lang_call_frames_max) {
        if (!lang->call_failed) { // once: the outermost call is dropped, and with it the garbage inside
            smith_lang_error(lang, host, location, "calls to generic functions are nested more than %zu deep",
                             smith_lang_call_frames_max);
        }
        lang->call_failed = true;
        return false;
    }
    SmithLangCallFrame *frame = &lang->calls[lang->call_top++];
    *frame = (SmithLangCallFrame){.record = record,
                                  .skip = true,
                                  .generic_index = generic_index,
                                  .location = location,
                                  .member_location = member_location,
                                  .count = smith_lang_gt.count,
                                  .text = smith_lang_gt.text_used};
    return true;
}

// Pushes a token of the lowering's own into the pool (a group's start or end).
static void smith_lang_gen_mark(SmithLang *lang, SmithLangGenSpecial special, const char *text, size_t length,
                                uint32_t location) {
    SmithLangGenToken token = {};
    token.kind = SmithTokenKind_Identifier;
    token.special = (uint8_t)special;
    token.location = location;
    if (smith_lang_pool_add(&smith_lang_gt, token, text, length) == UINT32_MAX) lang->broken = true;
}

// After a call: the tokens before the next one are those of an expression that ended.
static void smith_lang_generic_call_state(SmithLang *lang) {
    lang->prev_char = ')';
    lang->prev_member = false;
    lang->prev_tag = false;
    lang->typeish = false;
    lang->prev_name_length = 0;
}

// The same, for a call the caller writes at once: it returns on this flag.
static void smith_lang_generic_call_done(SmithLang *lang) {
    smith_lang_generic_call_state(lang);
    lang->namespace_done = true;
}

// In a generic's body: the call, kept whole between a start and an end, to be understood where the generic is written.
static bool smith_lang_generic_record_call(SmithLang *lang, const SmithHost *host, const char *module,
                                           size_t module_length, const char *member, size_t member_length,
                                           uint32_t location) {
    char text[smith_lang_name_max];
    int length = snprintf(text, sizeof text, "%.*s::%.*s", (int)module_length, module, (int)member_length, member);
    if (!smith_lang_generic_push_frame(lang, host, true, 0, location, location)) return false;
    smith_lang_gen_mark(lang, SmithLangGen_CallBegin, text, (size_t)length, location);
    lang->namespace_done = true; // the call, with its arguments, is written
    return true;
}

// Moves the tokens [from, top) of the pool down to `to`: a call nested in the arguments of another is written where its
// own tokens were.
static void smith_lang_gt_slide(uint32_t to_count, uint32_t to_text, uint32_t from_count, uint32_t from_text) {
    uint32_t moved = smith_lang_gt.count - from_count;
    uint32_t bytes = smith_lang_gt.text_used - from_text;
    memmove(smith_lang_gt.text + to_text, smith_lang_gt.text + from_text, bytes);
    memmove(smith_lang_gt.tokens + to_count, smith_lang_gt.tokens + from_count, moved * sizeof *smith_lang_gt.tokens);
    for (uint32_t i = 0; i < moved; i++)
        smith_lang_gt.tokens[to_count + i].text -= from_text - to_text;
    smith_lang_gt.count = to_count + moved;
    smith_lang_gt.text_used = to_text + bytes;
}

// A call to a generic, outside any generic: read whole, then written as the call of its instance.
static bool smith_lang_generic_direct_call(SmithLang *lang, const SmithHost *host, uint32_t generic_index,
                                           uint32_t location, uint32_t member_location) {
    if (!smith_lang_generic_push_frame(lang, host, false, generic_index, location, member_location)) return false;
    lang->cap_frames += 1;
    if (lang->cap_frames == 1) smith_lang_written_used = 0; // the outermost call: its nested calls are kept anew
    lang->namespace_done = true;
    return true;
}

// What stands where a call that could not be written stood: a name the parser cannot find, whose error is not shown, so
// that the statement around it is not reported a second time.
static void smith_lang_generic_poison(SmithLang *lang, const SmithHost *host, uint32_t location) {
    if (host->suppress != nullptr) host->suppress(host->context, location);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "smith_call_failed", location, 0);
}

// Keeps the call of `frame` as the source wrote it (`module::member`, then its tokens from `frame->count` up to
// `top`), for an outer call's messages. Returns 1 + its offset, or 0 when there is no room.
static uint32_t smith_lang_written_store(const SmithLangCallFrame *frame, uint32_t top) {
    const SmithLangGeneric *generic = &smith_lang_generics[frame->generic_index];
    char text[512];
    int used = snprintf(text, sizeof text, "%s::%s", smith_lang_str(generic->module), smith_lang_str(generic->member));
    if (used < 0 || (size_t)used + 2 >= sizeof text) return 0;
    smith_lang_infer_written((SmithLangSpan){frame->count, top}, text + used, sizeof text - (size_t)used);
    size_t length = strlen(text);
    if (smith_lang_written_used + length + 1 > sizeof smith_lang_written_text) return 0;
    uint32_t offset = smith_lang_written_used;
    memcpy(smith_lang_written_text + offset, text, length + 1);
    smith_lang_written_used += (uint32_t)length + 1;
    return offset + 1;
}

// The call of `frame` has its `)`: it is written.
static void smith_lang_generic_finish_call(SmithLang *lang, const SmithHost *host, const SmithLangCallFrame *frame) {
    if (frame->record) {
        smith_lang_gen_mark(lang, SmithLangGen_CallEnd, "", 0, frame->location);
        smith_lang_generic_call_state(lang);
        return;
    }
    lang->cap_frames -= 1;
    uint32_t top_count = smith_lang_gt.count;
    uint32_t top_text = smith_lang_gt.text_used;
    SmithLangReplay *replay = &smith_lang_replay;
    *replay = (SmithLangReplay){.instance = nullptr, .instance_id = 0, .generic = nullptr, .location = frame->location};
    bool written = !lang->call_failed && smith_lang_call_finish(lang, host, replay, frame->generic_index, frame->count,
                                                                top_count, frame->location, frame->member_location);
    if (!written) { // nothing of the call is kept; the outermost one leaves the poison where it stood
        smith_lang_gt.count = frame->count;
        smith_lang_gt.text_used = frame->text;
        if (lang->call_top == 0) {
            lang->call_failed = false;
            smith_lang_generic_poison(lang, host, frame->location);
        }
        smith_lang_generic_call_state(lang);
        return;
    }
    uint32_t kept = lang->cap_frames > 0 ? smith_lang_written_store(frame, top_count) : 0;
    smith_lang_replay_run(lang, host, replay);
    if (lang->cap_frames > 0) {
        uint32_t covers = smith_lang_gt.count - top_count;
        smith_lang_gt_slide(frame->count, frame->text, top_count, top_text); // inside another call: where this one was
        if (kept != 0 && covers > 0) {
            smith_lang_gt.tokens[frame->count].written = kept;
            smith_lang_gt.tokens[frame->count].covers = covers;
        }
    } else {
        smith_lang_gt.count = frame->count;
        smith_lang_gt.text_used = frame->text;
    }
    smith_lang_generic_call_state(lang);
}

static void smith_lang_generic_abandon(SmithLang *lang, const SmithHost *host, const SmithToken *after);

// Called by the feed loop after each token it has lowered: the token counts toward the innermost call being read.
static void smith_lang_generic_advance(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (lang->call_top == 0) return;
    SmithLangCallFrame *frame = &lang->calls[lang->call_top - 1];
    if (frame->skip) { // the token the call began at
        frame->skip = false;
        return;
    }
    bool punct = token->kind == SmithTokenKind_Punctuator && token->length == 1;
    if (punct && token->text[0] == ';' && frame->depth > 0 && frame->depth <= 64 &&
        ((frame->braces >> (uint32_t)(frame->depth - 1)) & 1U) == 0) { // a statement ended inside the parentheses
        smith_lang_generic_abandon(lang, host, token);
        return;
    }
    if (punct && strchr("([{", token->text[0]) != nullptr) {
        if (frame->depth < 64)
            frame->braces = (frame->braces & ~(1ULL << (uint32_t)frame->depth)) |
                            ((uint64_t)(token->text[0] == '{') << (uint32_t)frame->depth);
        frame->depth += 1;
    }
    if (punct && strchr(")]}", token->text[0]) != nullptr) frame->depth -= 1;
    if (frame->depth > 0) return;
    SmithLangCallFrame done = *frame;
    lang->call_top -= 1;
    smith_lang_generic_finish_call(lang, host, &done);
}

// A call is not closed: a statement ended inside its parentheses (`after` is that `;`, written again after the call's
// place) or the file did (then a `;` follows the poison, so that a call at file scope is one error). It is reported
// once, at the innermost call; the calls being read, the outermost with the garbage in it, are dropped, and the poison
// stands in their place.
static void smith_lang_generic_abandon(SmithLang *lang, const SmithHost *host, const SmithToken *after) {
    const SmithLangCallFrame *outer = &lang->calls[0];
    smith_lang_error(lang, host, lang->calls[lang->call_top - 1].location, "this call is never closed with `)`");
    uint32_t location = outer->location;
    smith_lang_gt.count = outer->count;
    smith_lang_gt.text_used = outer->text;
    for (size_t i = 0; i < lang->call_top; i++) { // the calls that held tokens aside
        if (!lang->calls[i].record) lang->cap_frames -= 1;
    }
    lang->call_top = 0;
    lang->call_failed = false;
    smith_lang_generic_poison(lang, host, location);
    smith_lang_generic_call_state(lang);
    if (after != nullptr) {
        smith_lang_emit(lang, host, *after);
        return;
    }
    SmithToken semicolon = smith_lang_synth(SmithTokenKind_Punctuator, ";", 1, &(SmithToken){.location = location});
    smith_lang_emit(lang, host, semicolon);
}

// The file ends inside a call.
static void smith_lang_generic_unclosed(SmithLang *lang, const SmithHost *host) {
    if (lang->call_top > 0) smith_lang_generic_abandon(lang, host, nullptr);
}

// The call that `name` begins, when it is to a generic (or, in a generic's body, to anything of a module). Returns
// false when it is not one and the name is to be written as any other.
static bool smith_lang_generic_call_core(SmithLang *lang, const SmithHost *host, const char *module,
                                         size_t module_length, const char *member, size_t member_length,
                                         uint32_t location, int generic_index) {
    if (generic_index >= 0 && smith_lang_generics[generic_index].plain) generic_index = -1; // an ordinary call
    if (generic_index < 0 && !lang->recording) return false;
    const SmithToken *open = smith_lang_peek(lang, host, 0, false);
    if (!smith_lang_punct(open, "(")) {
        if (generic_index < 0) return false;
        smith_lang_error(lang, host, location, "`%.*s` is a generic function: it can only be called, with its type",
                         (int)member_length, member);
        smith_lang_generic_call_done(lang);
        return true;
    }
    if (generic_index >= 0 && smith_lang_generics[generic_index].hidden && !lang->recording) {
        smith_lang_error(lang, host, location,
                         "`%.*s` is private to its module (an exported generic calls it); export it to call it",
                         (int)member_length, member);
        smith_lang_generic_call_done(lang);
        return true;
    }
    smith_lang_generic_use_member(lang, module, module_length, member, member_length);
    bool where_ok = !lang->is_module || lang->state == SmithLangState_Stream || lang->body_sparse;
    if (!where_ok || lang->export_active) {
        smith_lang_error(
            lang, host, location,
            "a generic cannot be called here: in a declaration's head, or in the body of an exported inline function");
        return false;
    }
    if (lang->recording)
        return smith_lang_generic_record_call(lang, host, module, module_length, member, member_length, location);
    return smith_lang_generic_direct_call(lang, host, (uint32_t)generic_index, location, location);
}

// `x::name(` where x is an import: a generic of that module, or in a generic's body any call.
static bool smith_lang_generic_call(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                    const SmithLangBinding *binding, const SmithToken *member) {
    (void)name;
    if (binding->is_c || binding->missing || (lang->generics_count == 0 && !lang->recording)) return false;
    const char *module = smith_lang_str(binding->module);
    int found = smith_lang_generic_find(lang, module, binding->module.length, member->text, member->length);
    return smith_lang_generic_call_core(lang, host, module, binding->module.length, member->text, member->length,
                                        member->location, found);
}

// A call to a generic of the module itself, by its name.
static bool smith_lang_generic_call_here(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                         const SmithLangName *known) {
    bool handled = smith_lang_generic_call_core(lang, host, lang->module, strlen(lang->module), name->text,
                                                name->length, name->location, (int)known->generic - 1);
    if (handled) lang->namespace_done = false; // the caller returns on the result, not on this flag
    return handled;
}

// A name of the module that a generic's exported body uses must be public: the body is compiled where the generic is
// called.
static void smith_lang_generic_private_name(SmithLang *lang, const SmithHost *host, SmithLangName *known,
                                            const char *name, size_t length, uint32_t location) {
    (void)host;
    (void)name;
    (void)length;
    (void)location;
    smith_lang_generic_use(lang, known); // if an exported generic reaches it, the header's instance section has it
}

// ---- Beside the nested interface reads
// ----------------------------------------------------------------------------------------

static SmithLangGenToken smith_lang_saved_gt_tokens[smith_lang_gt_max];
static char smith_lang_saved_gt_chars[smith_lang_gt_text_max];
static SmithLangGenToken smith_lang_saved_it_tokens[smith_lang_it_max];
static char smith_lang_saved_it_chars[smith_lang_it_text_max];
static SmithLangGeneric smith_lang_saved_generics[smith_lang_generics_max];
static SmithLangInstance smith_lang_saved_instances[smith_lang_instances_max];
static SmithLangPool smith_lang_saved_gt;
static SmithLangPool smith_lang_saved_it;

// The tables of generics are static, like the lowering's: a nested read (smith_lang_interface) keeps the caller's aside
// and puts them back.
static void smith_lang_generics_aside(bool save, const SmithLang *lang) {
    if (save) {
        smith_lang_saved_gt = smith_lang_gt;
        smith_lang_saved_it = smith_lang_it;
        memcpy(smith_lang_saved_gt_tokens, smith_lang_gt_tokens, smith_lang_gt.count * sizeof *smith_lang_gt_tokens);
        memcpy(smith_lang_saved_gt_chars, smith_lang_gt_chars, smith_lang_gt.text_used);
        memcpy(smith_lang_saved_it_tokens, smith_lang_it_tokens, smith_lang_it.count * sizeof *smith_lang_it_tokens);
        memcpy(smith_lang_saved_it_chars, smith_lang_it_chars, smith_lang_it.text_used);
        memcpy(smith_lang_saved_generics, smith_lang_generics, lang->generics_count * sizeof *smith_lang_generics);
        memcpy(smith_lang_saved_instances, smith_lang_instances, lang->instances_count * sizeof *smith_lang_instances);
        return;
    }
    smith_lang_gt = smith_lang_saved_gt;
    smith_lang_it = smith_lang_saved_it;
    memcpy(smith_lang_gt_tokens, smith_lang_saved_gt_tokens, smith_lang_gt.count * sizeof *smith_lang_gt_tokens);
    memcpy(smith_lang_gt_chars, smith_lang_saved_gt_chars, smith_lang_gt.text_used);
    memcpy(smith_lang_it_tokens, smith_lang_saved_it_tokens, smith_lang_it.count * sizeof *smith_lang_it_tokens);
    memcpy(smith_lang_it_chars, smith_lang_saved_it_chars, smith_lang_it.text_used);
    memcpy(smith_lang_generics, smith_lang_saved_generics, lang->generics_count * sizeof *smith_lang_generics);
    memcpy(smith_lang_instances, smith_lang_saved_instances, lang->instances_count * sizeof *smith_lang_instances);
}

static void smith_lang_generics_reset(void) {
    smith_lang_gt.count = 0;
    smith_lang_gt.text_used = 0;
    smith_lang_it.count = 0;
    smith_lang_it.text_used = 0;
    smith_lang_it.keep = 0;
    smith_lang_it.keep_text = 0;
}

// ---- The calls, for the editor
// ---------------------------------------------------------------------------------------------------

// The text of a spelling as this file writes it: `arena::Arena` for the compiler's `arena_Arena`, `u32` for its own
// name.
static const char *smith_lang_written_spelling(const SmithLang *lang, const char *text, size_t *length) {
    const SmithLangWritten *written = smith_lang_written_lookup(lang, text, *length);
    if (written != nullptr) {
        *length = written->written.length;
        return smith_lang_str(written->written);
    }
    for (size_t k = 0; k < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; k++) { // the sized types
        if (smith_lang_equals(text, *length, smith_lang_sized[k].standard)) {
            *length = strlen(smith_lang_sized[k].name);
            return smith_lang_sized[k].name;
        }
    }
    return text;
}

// The head of the generic as its module wrote it: `T *push(arena::Arena *a, constexpr type T)`, with the name whole.
static size_t smith_lang_generic_signature(const SmithLang *lang, const SmithLangGeneric *generic, char *out,
                                           size_t capacity) {
    size_t used = 0;
    uint32_t start = smith_lang_generic_skip_attributes(generic);
    for (uint32_t at = start; at <= generic->close_index && used + 2 < capacity; at++) { // the head, to its `)`
        const SmithLangGenToken *token = &smith_lang_gt.tokens[generic->first + at];
        if (at > start && ((token->flags & smith_lang_gen_flag_space) != 0)) out[used++] = ' ';
        const char *text = smith_lang_gt_str(token);
        size_t length = token->length;
        if (at == generic->name_index) {
            used += (size_t)snprintf(out + used, capacity - used, "%s::%s", smith_lang_str(generic->module),
                                     smith_lang_str(generic->member));
            continue;
        }
        if (token->kind == SmithTokenKind_Identifier && token->param == 0)
            text = smith_lang_written_spelling(lang, text, &length);
        if (used + length + 1 >= capacity) break;
        memcpy(out + used, text, length);
        used += length;
    }
    out[used] = 0;
    return used;
}

size_t smith_lang_instance_count(const SmithLang *lang) {
    return lang->instances_count;
}

bool smith_lang_instance_at(const SmithLang *lang, size_t index, SmithInstanceOrigin *out) {
    if (index >= lang->instances_count) return false;
    const SmithLangInstance *instance = &smith_lang_instances[index];
    snprintf(out->name, sizeof out->name, "%s", smith_lang_str(instance->cname));
    snprintf(out->source, sizeof out->source, "%s", smith_lang_str(smith_lang_generics[instance->generic].source));
    out->exported = smith_lang_generics[instance->generic].exported;
    return true;
}

size_t smith_lang_call_site_count(const SmithLang *lang) {
    return lang->call_site_count;
}

bool smith_lang_call_site_at(const SmithLang *lang, size_t index, SmithCallSite *out) {
    if (index >= lang->call_site_count) return false;
    const SmithLangCallSite *site = &lang->call_sites[index];
    const SmithLangGeneric *generic = &smith_lang_generics[site->generic];
    const SmithLangInstance *instance = &smith_lang_instances[site->instance];
    char signature[512];
    smith_lang_generic_signature(lang, generic, signature, sizeof signature);
    *out = (SmithCallSite){.location = site->location, .length = generic->member.length};
    snprintf(out->hover, sizeof out->hover, "generic function %s::%s\n\n%s\n\ninstance %s",
             smith_lang_str(generic->module), smith_lang_str(generic->member), signature,
             smith_lang_str(instance->description));
    snprintf(out->file, sizeof out->file, "%s", smith_lang_str(generic->source));
    const SmithLangGenToken *name = &smith_lang_gt.tokens[generic->first + generic->name_index];
    out->definition_location = name->line == 0 ? name->location : UINT32_MAX;
    out->line = name->line;
    out->column = name->column;
    return true;
}
