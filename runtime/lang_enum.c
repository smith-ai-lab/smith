// An enum with payloads (issue #198): `typedef enum { Circle circle; Rect rect; empty; } Shape;`, its variants written
// like the members of a union, lowered to plain C: a struct that holds a tag and a union,
//
//     struct Shape { enum Shape_tag { Shape_tag_circle, Shape_tag_rect, Shape_tag_empty } tag;
//                    union { Circle circle; Rect rect; }; };
//
// (`Shape_tag_empty` and the others are module names like any: `main_Shape_tag_empty`). A variant is written with a dot
// and looked up in the type the context gives, as a designated initializer's member is (`.circle`, `{.circle = {...}}`,
// `s == .empty`, `case .rect:`): the lowering asks the compiler for that type and writes the tag, or a compound
// literal. The generated struct carries `__attribute__((annotate("smith::enum")))` so a tool can tell it from other
// structs. Included by runtime/lang.c after runtime/lang_slice.c.

constexpr size_t smith_lang_enum_variants_max = 64;

// A payload enum's variants as the declaration wrote them: the tokens [start, end) of each member and its name.
typedef struct {
    size_t starts[smith_lang_enum_variants_max];
    size_t ends[smith_lang_enum_variants_max];
    size_t count;
} SmithLangEnumVariants;

// The extent of the braces of `enum {` or `enum Tag {` that hold a `;` at their own depth (a payload enum; a C enum has
// none): the queue index of the `{` and of its `}`, or false.
static bool smith_lang_enum_extent(SmithLang *lang, const SmithHost *host, size_t *open, size_t *close) {
    const SmithToken *first = smith_lang_peek(lang, host, 0, false);
    *open = smith_lang_punct(first, "{") ? 0 : 1;
    if (*open == 1 && first->kind != SmithTokenKind_Identifier) return false;
    if (!smith_lang_punct(smith_lang_peek(lang, host, *open, false), "{")) return false;
    int depth = 0;
    bool semicolon = false;
    for (size_t at = *open; at < smith_lang_queue_max - 2; at++) { // an enum is a few tokens
        const SmithToken *token = smith_lang_peek(lang, host, at, false);
        if (token->kind == SmithTokenKind_Eof || token->foreign) return false;
        depth += smith_lang_punct(token, "{");
        depth -= smith_lang_punct(token, "}");
        semicolon = semicolon || (depth == 1 && smith_lang_punct(token, ";"));
        if (depth == 0) {
            *close = at;
            return semicolon;
        }
    }
    return false;
}

// The members between the braces of `span` (the `{` at `open`, the `}` at `close`), split at the `;` of their own
// depth.
static void smith_lang_enum_split(const SmithToken *span, size_t open, size_t close, SmithLangEnumVariants *out) {
    out->count = 0;
    size_t start = open + 1;
    int depth = 0;
    for (size_t at = open + 1; at < close; at++) { // each token of the members
        depth += smith_lang_punct(&span[at], "{");
        depth -= smith_lang_punct(&span[at], "}");
        if (depth == 0 && smith_lang_punct(&span[at], ";") && out->count < smith_lang_enum_variants_max) {
            out->starts[out->count] = start;
            out->ends[out->count] = at;
            out->count += 1;
            start = at + 1;
        }
    }
}

// The tokens of a payload enum written as a struct, into `made`, for the name `name` (a token of the source). Returns
// the count, or 0 with an error reported.
static size_t smith_lang_enum_build(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                    const SmithToken *span, size_t open, size_t close, const SmithToken *name,
                                    SmithToken *made, size_t capacity) {
    SmithLangEnumVariants variants;
    smith_lang_enum_split(span, open, close, &variants);
    char text[smith_lang_name_max];
    size_t used = 0;
    bool payload = false;
    made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, "struct", 6, token, token->space_before);
    // The struct's tag is `smith_enum_<module>_<Name>`, which a debugger matches (`smith dbg` shows the active
    // variant); the typedef name the program and C callers use stays `<module>_<Name>`.
    const char *prefixed = smith_lang_prefixed(lang, name->text, name->length);
    if (name->length > 11 && memcmp(name->text, "smith_enum_", 11) == 0) { // a type function's enum: already the tag
        snprintf(text, sizeof text, "%.*s", (int)name->length, name->text);
    } else {
        snprintf(text, sizeof text, "smith_enum_%s", prefixed);
    }
    made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, text, strlen(text), token, true);
    made[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "{", 1, token, true);
    made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, "enum", 4, token, false);
    snprintf(text, sizeof text, "%.*s_tag", (int)name->length, name->text);
    made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, text, strlen(text), token, true);
    made[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "{", 1, token, true);
    for (size_t at = 0; at < variants.count; at++) { // the tag of each variant
        const SmithToken *variant = &span[variants.ends[at] - 1];
        if (variants.ends[at] - variants.starts[at] > 1) payload = true;
        if (variant->kind != SmithTokenKind_Identifier || used + 6 >= capacity) {
            smith_lang_error(lang, host, span[variants.starts[at]].location,
                             "a variant is a type and a name, `Circle circle;`, or a name alone, `empty;`");
            return 0;
        }
        snprintf(text, sizeof text, "%.*s_tag_%.*s", (int)name->length, name->text, (int)variant->length,
                 variant->text);
        if (at > 0) made[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ",", 1, token, false);
        // the constant stands where its variant is written, so the doc comment above the variant is the constant's
        // (clang finds it by place; the header pass writes it above the constant)
        made[used++] =
            smith_lang_tuple_made(SmithTokenKind_Identifier, text, strlen(text), &span[variants.starts[at]], at > 0);
    }
    made[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "}", 1, token, true);
    made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, "tag", 3, token, true);
    made[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ";", 1, token, false);
    if (payload) {
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, "union", 5, token, true);
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "{", 1, token, true);
        for (size_t at = 0; at < variants.count; at++) { // each variant that carries something
            size_t length = variants.ends[at] - variants.starts[at];
            if (length < 2 || used + length + 2 >= capacity) continue;
            for (size_t i = variants.starts[at]; i < variants.ends[at]; i++) {
                made[used] = span[i]; // moved after the tag's tokens: its place is out of order, so it is a copy
                made[used].synthetic = true;
                made[used++].copy = true;
            }
            made[used - length].space_before = true;
            made[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ";", 1, token, false);
        }
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "}", 1, token, true);
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ";", 1, token, false);
    }
    made[used] = span[close]; // the attribute that follows is placed before it in the source
    made[used++].synthetic = true;
    static const char *const attribute[] = {"__attribute__",   "(", "(", "annotate", "(",
                                            "\"smith::enum\"", ")", ")", ")"};
    for (size_t at = 0; at < sizeof attribute / sizeof attribute[0]; at++) { // the plugin knows the struct by it
        SmithTokenKind kind = at == 0 || at == 3 ? SmithTokenKind_Identifier
                              : at == 5          ? SmithTokenKind_String
                                                 : SmithTokenKind_Punctuator;
        made[used++] = smith_lang_tuple_made(kind, attribute[at], strlen(attribute[at]), token, at == 0);
    }
    return used;
}

// `enum Shape { Circle circle; empty; }`: the tag is the type's name, so its typedef (`m_Shape` for the struct
// `smith_enum_m_Shape`) is written before the next declaration, like a struct's.
static void smith_lang_enum_tag_name(SmithLang *lang, const SmithHost *host, const SmithToken *name) {
    if (!lang->is_module || lang->in_function || lang->recording ||
        smith_lang_name_find(lang, name->text, name->length)) {
        return;
    }
    if (name->length > 11 && memcmp(name->text, "smith_enum_", 11) == 0) return; // a type function's enum
    if (smith_lang_declare(lang, host, name->text, name->length, name->location, SmithLangName_Tag,
                           lang->pending_export) == nullptr) {
        return;
    }
    char spelling[smith_lang_name_max];
    snprintf(spelling, sizeof spelling, "smith_enum_%s", smith_lang_prefixed(lang, name->text, name->length));
    smith_lang_tag_due(lang, name->text, name->length, 's', spelling, true);
}

// `enum` is being processed: if its braces hold variants (a `;` among them), the tokens up to the `}` are written as
// the struct above, and the `enum` itself is dropped. True when it was.
static bool smith_lang_enum_begin(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    size_t open = 0;
    size_t close = 0;
    if (!smith_lang_enum_extent(lang, host, &open, &close)) return false;
    static SmithToken span[smith_lang_queue_max];
    for (size_t at = 0; at <= close; at++) // up to the `}`
        span[at] = *smith_lang_peek(lang, host, at, false);
    SmithToken name = open == 1 ? span[0] : *smith_lang_peek(lang, host, close + 1, false);
    if (name.kind != SmithTokenKind_Identifier || smith_lang_punct(&name, ";")) {
        smith_lang_error(lang, host, token->location,
                         "an enum with payloads needs a name: `typedef enum { Circle circle; empty; } Shape;`");
        return false;
    }
    static SmithToken made[smith_lang_queue_max];
    size_t used = smith_lang_enum_build(lang, host, token, span, open, close, &name, made, smith_lang_queue_max);
    if (used == 0 || !smith_lang_tuple_replace(lang, close + 1, made, used)) return false;
    if (open == 1) smith_lang_enum_tag_name(lang, host, &name);
    lang->tuples_seen = true;
    lang->slices_seen = true; // the file's header says so, and its importers read every token
    lang->enums_seen = true;
    return true;
}

// ---- Where a payload enum is used: the dot rule

constexpr size_t smith_lang_enum_name_max = 192;
constexpr size_t smith_lang_enum_switch_max = 8;

// A payload enum's struct, as the compiler spells it (`struct main_Shape`) and the name its tag and tags are made of.
typedef struct {
    char type[smith_lang_enum_name_max]; // struct main_Shape
    char name[smith_lang_enum_name_max]; // main_Shape
} SmithLangEnumType;

static bool smith_lang_eshape_queued(const char *tag);

// Whether the type is the language's own `option` or `result` struct that the lowering has queued in front of an
// instance and the compiler has not read yet (an instance requested from a function body is written at the end of the
// file, with its structs, in one batch): its variants follow from the shape.
static bool smith_lang_enum_unread(const char *name) {
    return (strncmp(name, "smith_enum_option_", 18) == 0 || strncmp(name, "smith_enum_result_", 18) == 0) &&
           smith_lang_eshape_queued(name);
}

// The variant of an unread struct: false when the struct has none of the name, `carries` says it has a payload.
static bool smith_lang_enum_unread_variant(const SmithLangEnumType *type, const char *variant, size_t length,
                                           bool *carries) {
    if (!smith_lang_enum_unread(type->name)) return false;
    bool is_option = strncmp(type->name, "smith_enum_option_", 18) == 0;
    static const char *const options[] = {"some", "none"};
    static const char *const results[] = {"ok", "error"};
    const char *const *names = is_option ? options : results;
    for (size_t i = 0; i < 2; i++) { // some, none or ok, error
        if (strlen(names[i]) == length && memcmp(names[i], variant, length) == 0) {
            *carries = i == 0 || !is_option;
            return true;
        }
    }
    return false;
}

// A canonical type that is a payload enum: `struct smith_enum_<name>` with a tag enum `<name>_tag`.
static bool smith_lang_enum_type(const SmithHost *host, const char *canonical, SmithLangEnumType *out) {
    if (host->lookup == nullptr) return false;
    if (strncmp(canonical, "const ", 6) == 0) canonical += 6;
    if (strncmp(canonical, "struct ", 7) != 0 || strchr(canonical + 7, ' ') != nullptr ||
        strchr(canonical + 7, '*') != nullptr || strlen(canonical + 7) + 8 > sizeof out->name) {
        return false;
    }
    const char *named = canonical + 7;
    if (strncmp(named, "smith_enum_", 11) != 0) return false; // the tag the lowering gives a payload enum
    snprintf(out->type, sizeof out->type, "%s", canonical);
    char tag[smith_lang_enum_name_max + 8];
    // A module's payload enum is `smith_enum_<name>` with the tag enum `<name>_tag`; a type function's is
    // `smith_enum_<function>_<arguments>` with `smith_enum_<function>_<arguments>_tag`.
    snprintf(out->name, sizeof out->name, "%s", named + 11);
    int length = snprintf(tag, sizeof tag, "%s_tag", out->name);
    if (host->lookup(host->context, tag, (uint32_t)length, SmithLookup_Tag).kind == SmithLookup_Tag) return true;
    snprintf(out->name, sizeof out->name, "%s", named);
    length = snprintf(tag, sizeof tag, "%s_tag", out->name);
    return host->lookup(host->context, tag, (uint32_t)length, SmithLookup_Tag).kind == SmithLookup_Tag ||
           smith_lang_enum_unread(out->name);
}

// The type of an expression written as tokens, when it is a payload enum.
static bool smith_lang_enum_of(SmithLang *lang, const SmithHost *host, const SmithToken *tokens, size_t count,
                               SmithLangEnumType *out) {
    char text[smith_lang_slice_text_max];
    SmithExprType found = {};
    return count > 0 && host->expr_type != nullptr && smith_lang_slice_text(lang, tokens, count, text, sizeof text) &&
           host->expr_type(host->context, text, (uint32_t)strlen(text), &found) &&
           smith_lang_enum_type(host, found.canonical, out);
}

// The constant of a variant (`main_Shape_tag_circle`), when the enum has one by that name.
static bool smith_lang_enum_constant(const SmithHost *host, const SmithLangEnumType *type, const SmithToken *variant,
                                     char *out, size_t capacity) {
    int length = snprintf(out, capacity, "%s_tag_%.*s", type->name, (int)variant->length, variant->text);
    bool carries = false;
    return length > 0 && (size_t)length < capacity &&
           (host->lookup(host->context, out, (uint32_t)length, SmithLookup_Value).kind == SmithLookup_Value ||
            smith_lang_enum_unread_variant(type, variant->text, variant->length, &carries));
}

// Whether the variant carries a payload: the struct has a member of its name.
static bool smith_lang_enum_carries(const SmithHost *host, const SmithLangEnumType *type, const SmithToken *variant) {
    char text[smith_lang_enum_name_max * 2 + 16];
    SmithExprType found = {};
    bool carries = false;
    if (smith_lang_enum_unread_variant(type, variant->text, variant->length, &carries)) return carries;
    snprintf(text, sizeof text, "(\x1f%s\x1f)\x1f{\x1f}\x1f.\x1f%.*s", type->type, (int)variant->length, variant->text);
    for (char *at = text; *at != 0; at++) { // the type's words are tokens
        if (*at == ' ') *at = '\x1f';
    }
    return host->expr_type(host->context, text, (uint32_t)strlen(text), &found);
}

// The type of a variant's payload, as the compiler prints it: false when the variant has none.
static bool smith_lang_enum_payload(const SmithHost *host, const SmithLangEnumType *type, const char *variant,
                                    SmithExprType *found) {
    char text[smith_lang_enum_name_max * 2 + 16];
    snprintf(text, sizeof text, "(\x1f%s\x1f)\x1f{\x1f}\x1f.\x1f%s", type->type, variant);
    for (char *at = text; *at != 0; at++) { // the type's words are tokens
        if (*at == ' ') *at = '\x1f';
    }
    return host->expr_type(host->context, text, (uint32_t)strlen(text), found);
}

// Whether `type` is the language's own `result(T, E)` (`*is_result`) or `option(T)`.
static bool smith_lang_enum_builtin(const SmithLangEnumType *type, bool *is_result) {
    *is_result = strncmp(type->type, "struct smith_enum_result_", 25) == 0;
    return *is_result || strncmp(type->type, "struct smith_enum_option_", 25) == 0;
}

// The variant a `.name` stands for in `type`, checked: false (an error said) when there is none or it needs a payload.
static bool smith_lang_enum_variant(SmithLang *lang, const SmithHost *host, const SmithLangEnumType *type,
                                    const SmithToken *dot, const SmithToken *variant, bool bare, char *constant,
                                    size_t capacity) {
    if (!smith_lang_enum_constant(host, type, variant, constant, capacity)) {
        smith_lang_error(lang, host, dot->location, "`%s` has no variant `%.*s`", type->name + 0, (int)variant->length,
                         variant->text);
        return false;
    }
    if (bare && smith_lang_enum_carries(host, type, variant)) {
        smith_lang_error(lang, host, dot->location,
                         "`.%.*s` carries a payload: write `{.%.*s = ...}`, as a designated initializer",
                         (int)variant->length, variant->text, (int)variant->length, variant->text);
        return false;
    }
    return true;
}

// Whether the tokens are `.name` (a variant by itself) and, with `braces`, `{ .name = ... }`.
static bool smith_lang_enum_is_bare(const SmithToken *tokens, size_t count) {
    return count == 2 && smith_lang_punct(&tokens[0], ".") && tokens[1].kind == SmithTokenKind_Identifier;
}

static bool smith_lang_enum_is_braced(const SmithToken *tokens, size_t count) {
    return count >= 5 && smith_lang_punct(&tokens[0], "{") && smith_lang_punct(&tokens[1], ".") &&
           tokens[2].kind == SmithTokenKind_Identifier && smith_lang_punct(&tokens[3], "=") &&
           smith_lang_punct(&tokens[count - 1], "}");
}

// A value of `type` with nothing in it, written where an error was said, so that nothing else is said.
static void smith_lang_enum_blank(const SmithLangEnumType *type, bool literal, const SmithToken *like,
                                  SmithLangSliceCur *cur) {
    char empty[smith_lang_enum_name_max + 16];
    snprintf(empty, sizeof empty, literal ? "( %s ) { }" : "{ }", type->type);
    SmithLangSliceCur piece = {};
    piece.count = smith_lang_slice_pattern(piece.tokens, 0, smith_lang_slice_cur_max, empty, nullptr, like);
    smith_lang_slice_add(cur, piece.tokens, piece.count);
    cur->changed = true;
}

// Why a value cannot become the success variant of `type` without being said: the error, or nothing.
static const char *smith_lang_enum_wrap_refusal(const SmithHost *host, const SmithLangEnumType *type, bool is_result,
                                                const SmithExprType *good, const SmithExprType *given,
                                                bool is_nullptr) {
    SmithLangEnumType inner;
    bool inner_result;
    if (smith_lang_enum_type(host, good->canonical, &inner) && smith_lang_enum_builtin(&inner, &inner_result) &&
        strcmp(good->canonical, given->canonical) != 0) {
        return "a value could be the payload of the inner `result` or `option` as well as of this one, so it does not "
               "convert: write the variants out, as `{.some = {.some = v}}` or `{.some = .none}`";
    }
    if (is_result) {
        SmithExprType bad = {};
        if (smith_lang_enum_payload(host, type, "error", &bad) && strcmp(bad.canonical, good->canonical) == 0) {
            return "the `.ok` and the `.error` of this `result` have the same type, so a value could be either and "
                   "does not convert: write `{.ok = v}` or `{.error = v}`";
        }
    }
    if (is_nullptr && !is_result && strchr(good->canonical, '*') != nullptr) {
        return "`nullptr` does not convert to an `option` of a pointer: write `.none` for no value, or `{.some = "
               "nullptr}` for a null one";
    }
    return nullptr;
}

// A value `v` where a `result(T, E)` or an `option(T)` is expected stands for `{.ok = v}` or `{.some = v}`; C's own
// conversions apply to it, as they do to `.ok = v`. False (the tokens are left alone) when the type is neither, when
// the tokens are a variant written out or an expression of the type itself, or when they have no type of their own.
static bool smith_lang_enum_wrap(SmithLang *lang, const SmithHost *host, const SmithLangEnumType *type,
                                 const SmithToken *tokens, size_t count, bool literal, SmithLangSliceCur *cur) {
    bool is_result;
    if (count == 0 || !smith_lang_enum_builtin(type, &is_result) || smith_lang_punct(&tokens[0], ".") ||
        smith_lang_punct(&tokens[0], "{") || smith_lang_ident(&tokens[0], "try") || host->expr_type == nullptr) {
        return false;
    }
    char text[smith_lang_slice_text_max];
    SmithExprType given = {};
    SmithLangSliceType cut = {};
    if (smith_lang_slice_cut_type(lang, host, tokens, count, &cut)) { // `xs[a:b]` has no C type until it is lowered
        snprintf(given.canonical, sizeof given.canonical, "struct %s", cut.tag);
    } else if (!smith_lang_slice_text(lang, tokens, count, text, sizeof text) ||
               !host->expr_type(host->context, text, (uint32_t)strlen(text), &given)) {
        return false;
    }
    SmithLangEnumType same;
    if (smith_lang_enum_type(host, given.canonical, &same) && strcmp(same.type, type->type) == 0) return false;
    const char *variant = is_result ? "ok" : "some";
    SmithExprType good = {};
    if (!smith_lang_enum_payload(host, type, variant, &good)) return false;
    bool is_nullptr = count == 1 && smith_lang_ident(&tokens[0], "nullptr");
    const char *refusal = smith_lang_enum_wrap_refusal(host, type, is_result, &good, &given, is_nullptr);
    if (refusal != nullptr) {
        smith_lang_error(lang, host, tokens[0].location, "%s", refusal);
        smith_lang_enum_blank(type, literal, &tokens[0], cur);
        return true;
    }
    SmithToken name = smith_lang_tuple_made(SmithTokenKind_Identifier, variant, strlen(variant), &tokens[0], false);
    char constant[smith_lang_enum_name_max + 64];
    if (!smith_lang_enum_constant(host, type, &name, constant, sizeof constant)) return false;
    char pattern[smith_lang_enum_name_max * 2 + 80];
    if (literal) {
        snprintf(pattern, sizeof pattern, "( %s ) { . tag = %s , . %s = ( @0 ) }", type->type, constant, variant);
    } else {
        snprintf(pattern, sizeof pattern, "{ . tag = %s , . %s = ( @0 ) }", constant, variant);
    }
    SmithLangSliceSpan spans[1] = {{tokens, count}};
    SmithLangSliceCur piece = {};
    piece.count = smith_lang_slice_pattern(piece.tokens, 0, smith_lang_slice_cur_max, pattern, spans, &tokens[0]);
    smith_lang_slice_add(cur, piece.tokens, piece.count);
    cur->changed = true;
    return true;
}

// A value for an enum of `type`: `.name` or `{.name = payload}` written as C's initializer (`literal` false, after a
// declaration's `=`) or compound literal (`literal` true). False when the tokens are neither (they are left alone).
static bool smith_lang_enum_value(SmithLang *lang, const SmithHost *host, const SmithLangEnumType *type,
                                  const SmithToken *tokens, size_t count, bool literal, SmithLangSliceCur *cur) {
    bool bare = smith_lang_enum_is_bare(tokens, count);
    if (!bare && !smith_lang_enum_is_braced(tokens, count))
        return smith_lang_enum_wrap(lang, host, type, tokens, count, literal, cur);
    const SmithToken *dot = bare ? &tokens[0] : &tokens[1];
    const SmithToken *variant = bare ? &tokens[1] : &tokens[2];
    char constant[smith_lang_enum_name_max + 64];
    if (!smith_lang_enum_variant(lang, host, type, dot, variant, bare, constant, sizeof constant)) {
        smith_lang_enum_blank(type, literal, &tokens[0], cur); // a value of the type, so that nothing else is said
        return true;
    }
    char pattern[smith_lang_enum_name_max * 2 + 80];
    if (literal) {
        snprintf(pattern, sizeof pattern, "( %s ) { . tag = %s%s @0 }", type->type, constant, bare ? "" : " ,");
    } else {
        snprintf(pattern, sizeof pattern, "{ . tag = %s%s @0 }", constant, bare ? "" : " ,");
    }
    SmithLangSliceSpan spans[1] = {{tokens, 0}};
    if (!bare) spans[0] = (SmithLangSliceSpan){tokens + 1, count - 2}; // `.name = payload`
    SmithLangSliceCur piece = {};
    piece.count = smith_lang_slice_pattern(piece.tokens, 0, smith_lang_slice_cur_max, pattern, spans, &tokens[0]);
    smith_lang_slice_add(cur, piece.tokens, piece.count);
    cur->changed = true;
    return true;
}

// ---- The contexts that give the type

// Whether the `=` being lowered gives a member in braces its value (`{.v = 5`, `, .v = 5`): the name is no variable.
static bool smith_lang_enum_designator(SmithLang *lang, const SmithHost *host) {
    SmithToken name;
    SmithToken dot;
    SmithToken open;
    return smith_lang_previous(lang, host, 1, &name) && name.kind == SmithTokenKind_Identifier &&
           smith_lang_previous(lang, host, 2, &dot) && smith_lang_punct(&dot, ".") &&
           smith_lang_previous(lang, host, 3, &open) && (smith_lang_punct(&open, "{") || smith_lang_punct(&open, ","));
}

// `T x = .v;`, `T x = {.v = ...};` (a declaration: the initializer is C's) and `x = .v;`, `x = {.v = ...};` (an
// assignment: a compound literal). Called at the `=`. True when the right side was written again.
static bool smith_lang_enum_equals(SmithLang *lang, const SmithHost *host) {
    const SmithToken *first = smith_lang_peek(lang, host, 0, false);
    bool dotted = smith_lang_punct(first, ".") || smith_lang_punct(first, "{");
    if (!dotted && (smith_lang_ident(first, "try") || smith_lang_enum_designator(lang, host))) return false;
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, 0, right, smith_lang_slice_chain_max);
    if (count == 0 || (dotted && !smith_lang_enum_is_bare(right, count) && !smith_lang_enum_is_braced(right, count))) {
        return false;
    }
    SmithLangEnumType type;
    char text[smith_lang_slice_text_max];
    bool pointer = false;
    SmithExprType found = {};
    bool declares = smith_lang_slice_declared_text(lang, host, text, sizeof text, &pointer);
    bool typed = declares ? !pointer && host->expr_type(host->context, text, (uint32_t)strlen(text), &found) &&
                                smith_lang_enum_type(host, found.canonical, &type)
                          : smith_lang_tuple_base(lang, host, text, sizeof text) &&
                                host->expr_type(host->context, text, (uint32_t)strlen(text), &found) &&
                                smith_lang_enum_type(host, found.canonical, &type);
    if (!typed) return false;
    const SmithToken *after = smith_lang_peek(lang, host, count, false);
    if (!dotted && !smith_lang_punct(after, ";") && !(declares && smith_lang_punct(after, ","))) return false;
    static SmithLangSliceCur converted;
    converted = (SmithLangSliceCur){};
    if (!smith_lang_enum_value(lang, host, &type, right, count, !declares, &converted) || converted.overflow)
        return false;
    return smith_lang_tuple_replace(lang, count, converted.tokens, converted.count);
}

// The value of a member of a struct's initializer (`{.v = 5, .w = .none}`) whose type is a payload enum: the tokens of
// the value, written as the member's initializer. False (nothing written) when the member is no such enum.
static bool smith_lang_enum_member(SmithLang *lang, const SmithHost *host, const char *spelling,
                                   const SmithToken *tokens, size_t count, SmithLangSliceCur *cur) {
    SmithExprType found = {};
    SmithLangEnumType type;
    if (count == 0 || !smith_lang_slice_spelled(host, spelling, &found) ||
        !smith_lang_enum_type(host, found.canonical, &type)) {
        return false;
    }
    return smith_lang_enum_value(lang, host, &type, tokens, count, false, cur);
}

// `return .v;` and `return {.v = ...};`: the type is the function's result.
static bool smith_lang_enum_return(SmithLang *lang, const SmithHost *host) {
    const SmithToken *first = smith_lang_peek(lang, host, 0, false);
    bool dotted = smith_lang_punct(first, ".") || smith_lang_punct(first, "{");
    if (host->return_type == nullptr || (!dotted && (smith_lang_ident(first, "try") || smith_lang_punct(first, ";")))) {
        return false;
    }
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, 0, right, smith_lang_slice_chain_max);
    if (count == 0 || (dotted && !smith_lang_enum_is_bare(right, count) && !smith_lang_enum_is_braced(right, count))) {
        return false;
    }
    if (!dotted && !smith_lang_punct(smith_lang_peek(lang, host, count, false), ";")) return false;
    char returned[256];
    SmithLangEnumType type;
    if (!host->return_type(host->context, returned, sizeof returned) || !smith_lang_enum_type(host, returned, &type)) {
        return false;
    }
    static SmithLangSliceCur converted;
    converted = (SmithLangSliceCur){};
    if (!smith_lang_enum_value(lang, host, &type, right, count, true, &converted) || converted.overflow) return false;
    return smith_lang_tuple_replace(lang, count, converted.tokens, converted.count);
}

// An argument `.v` or `{.v = ...}` where the callee's parameter number `index` is a payload enum: the parameter's text
// out of the function type the compiler printed (`int (struct main_Shape, int)`).
static bool smith_lang_enum_param(const SmithHost *host, const char *canonical, size_t index, SmithLangEnumType *out) {
    size_t length = strlen(canonical);
    if (length == 0 || canonical[length - 1] != ')') return false;
    size_t open = length - 1;
    int depth = 0;
    for (; open > 0; open--) { // back to the matching parenthesis
        depth += canonical[open] == ')';
        depth -= canonical[open] == '(';
        if (depth == 0) break;
    }
    size_t at = open + 1;
    depth = 0;
    size_t start = at;
    size_t number = 0;
    for (; at < length; at++) { // each parameter
        depth += canonical[at] == '(' || canonical[at] == '[';
        depth -= canonical[at] == ')' || canonical[at] == ']';
        if ((canonical[at] == ',' && depth == 0) || at == length - 1) {
            if (number++ == index) {
                char param[smith_lang_enum_name_max + 16];
                snprintf(param, sizeof param, "%.*s", (int)(at - start), canonical + start);
                const char *text = param;
                while (*text == ' ')
                    text += 1;
                return smith_lang_enum_type(host, text, out);
            }
            start = at + 1;
        }
    }
    return false;
}

// ---- A variant compared: `s == .v` and `.v == s`

// The tokens of an operand that begins at the token being lowered (`first`) and ends at a `==` or `!=` that is followed
// by `.name`: into `operand`, and how many tokens of the queue the whole comparison takes after `first`. False when
// it is no such comparison. An operand is a name, `.`, `->`, brackets and a leading `*` (an expression of one postfix
// chain).
static bool smith_lang_enum_comparison(SmithLang *lang, const SmithHost *host, const SmithToken *first,
                                       SmithToken *operand, size_t *length, size_t *taken, const SmithToken **op,
                                       const SmithToken **variant) {
    constexpr size_t limit = 40;
    operand[0] = *first;
    *length = 1;
    int depth = 0;
    for (size_t at = 0; at < limit; at++) { // each token after the first
        const SmithToken *token = smith_lang_peek(lang, host, at, false);
        if (token->kind == SmithTokenKind_Eof || token->foreign) return false;
        bool open = smith_lang_punct(token, "(") || smith_lang_punct(token, "[");
        bool close = smith_lang_punct(token, ")") || smith_lang_punct(token, "]");
        if (depth == 0 && (smith_lang_punct(token, "==") || smith_lang_punct(token, "!="))) {
            const SmithToken *dot = smith_lang_peek(lang, host, at + 1, false);
            const SmithToken *name = smith_lang_peek(lang, host, at + 2, false);
            if (!smith_lang_punct(dot, ".") || name->kind != SmithTokenKind_Identifier) return false;
            *op = token;
            *variant = name;
            *taken = at + 3;
            return true;
        }
        bool part = token->kind == SmithTokenKind_Identifier || token->kind == SmithTokenKind_Number || open || close ||
                    smith_lang_punct(token, ".") || smith_lang_punct(token, "->") || smith_lang_punct(token, "::") ||
                    depth > 0;
        if (!part || *length + 1 >= smith_lang_slice_chain_max) return false;
        depth += open;
        depth -= close;
        if (depth < 0) return false;
        operand[(*length)++] = *token;
    }
    return false;
}

static bool smith_lang_enum_generic_constant(SmithLang *lang, const SmithHost *host, const SmithToken *operand,
                                             size_t length, const SmithToken *variant, char *out, size_t capacity);

// At the first token of `operand == .v` (a name, `*` or `(` after an operator): written as `(operand).tag == C`.
static bool smith_lang_enum_compare(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    bool head =
        token->kind == SmithTokenKind_Identifier || smith_lang_punct(token, "*") || smith_lang_punct(token, "(");
    if (!head || token->foreign || token->synthetic || lang->prev_member || lang->prev_name_length > 0 ||
        host->expr_type == nullptr) {
        return false;
    }
    if (token->kind == SmithTokenKind_Identifier &&
        smith_lang_word(token->text, token->length) != SmithLangWord_Other) {
        return false;
    }
    // The token before this one is read from the queue, not from `prev_char`: a body held until a generic's instance
    // is written is lowered after the end of the file, where `prev_char` is the last token the file was tracked to.
    SmithToken before = {};
    if (smith_lang_tuple_previous(lang, host, 1, &before) &&
        (smith_lang_punct(&before, ")") || smith_lang_punct(&before, "]"))) {
        return false;
    }
    static SmithToken operand[smith_lang_slice_chain_max];
    size_t length = 0;
    size_t taken = 0;
    const SmithToken *op = nullptr;
    const SmithToken *variant = nullptr;
    if (!smith_lang_enum_comparison(lang, host, token, operand, &length, &taken, &op, &variant)) return false;
    SmithLangEnumType type;
    char constant[smith_lang_enum_name_max + 64];
    SmithToken dot = *smith_lang_peek(lang, host, taken - 2, false);
    SmithToken name = *variant;
    SmithToken oper = *op;
    if (!smith_lang_enum_of(lang, host, operand, length, &type)) { // a call of a generic: typed from its written result
        if (!smith_lang_enum_generic_constant(lang, host, operand, length, variant, constant, sizeof constant)) {
            return false;
        }
    } else if (!smith_lang_enum_variant(lang, host, &type, &dot, &name, false, constant, sizeof constant)) {
        return false;
    }
    char pattern[smith_lang_enum_name_max + 80];
    snprintf(pattern, sizeof pattern, "( @0 ) . tag @1 %s", constant);
    SmithLangSliceSpan spans[2] = {{operand, length}, {&oper, 1}};
    static SmithToken made[smith_lang_slice_chain_max + 16];
    size_t used = smith_lang_slice_pattern(made, 0, sizeof made / sizeof made[0], pattern, spans, token);
    return smith_lang_tuple_replace(lang, taken, made, used);
}

// At the `.` of `.v == operand` or `.v != operand`: the type is the operand's, and it is written as `C ==
// (operand).tag`.
static bool smith_lang_enum_compare_first(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    const SmithToken *variant = smith_lang_peek(lang, host, 0, false);
    const SmithToken *op = smith_lang_peek(lang, host, 1, false);
    if (variant->kind != SmithTokenKind_Identifier || !(smith_lang_punct(op, "==") || smith_lang_punct(op, "!="))) {
        return false;
    }
    static SmithToken operand[smith_lang_slice_chain_max];
    size_t length = 0;
    int depth = 0;
    for (size_t at = 2; at < 42; at++) { // the operand: a postfix chain after the operator
        const SmithToken *next = smith_lang_peek(lang, host, at, false);
        if (next->kind == SmithTokenKind_Eof || next->foreign) return false;
        bool open = smith_lang_punct(next, "(") || smith_lang_punct(next, "[");
        bool close = smith_lang_punct(next, ")") || smith_lang_punct(next, "]");
        bool part = next->kind == SmithTokenKind_Identifier || next->kind == SmithTokenKind_Number || open || close ||
                    smith_lang_punct(next, ".") || smith_lang_punct(next, "->") || smith_lang_punct(next, "::") ||
                    depth > 0 || (length == 0 && smith_lang_punct(next, "*"));
        depth += open;
        depth -= close;
        if (!part || depth < 0 || length + 1 >= smith_lang_slice_chain_max) break;
        operand[length++] = *next;
    }
    SmithLangEnumType type;
    char constant[smith_lang_enum_name_max + 64];
    if (length == 0 || !smith_lang_enum_of(lang, host, operand, length, &type)) return false;
    SmithToken dot = *token;
    SmithToken name = *variant;
    SmithToken oper = *op;
    if (!smith_lang_enum_variant(lang, host, &type, &dot, &name, false, constant, sizeof constant)) return false;
    char pattern[smith_lang_enum_name_max + 80];
    snprintf(pattern, sizeof pattern, "%s @1 ( @0 ) . tag", constant);
    SmithLangSliceSpan spans[2] = {{operand, length}, {&oper, 1}};
    static SmithToken made[smith_lang_slice_chain_max + 16];
    size_t used = smith_lang_slice_pattern(made, 0, sizeof made / sizeof made[0], pattern, spans, token);
    made[0].space_before = token->space_before;
    return smith_lang_tuple_replace(lang, 2 + length, made, used);
}

// ---- `switch (s) { case .v: ... }`

// The innermost switch over a payload enum that is open: the brace depth outside its body and the enum.
typedef struct {
    uint32_t depth;
    SmithLangEnumType type;
} SmithLangEnumSwitch;

static SmithLangEnumSwitch smith_lang_enum_switches[smith_lang_enum_switch_max];
static size_t smith_lang_enum_switch_count;
static uint32_t smith_lang_enum_braces; // `{` minus `}` of the tokens seen in slice mode

static void smith_lang_enum_shapes_reset(void);

static void smith_lang_enum_reset(void) {
    smith_lang_enum_switch_count = 0;
    smith_lang_enum_braces = 0;
    smith_lang_enum_shapes_reset();
}

// ---- The dot rule in a generic instance's body (issue #198)
//
// An instance's body is written at the end of the file from the tokens the generic recorded, so the compiler has read
// nothing of it and cannot be asked the type a `.name` or `{.name = ...}` stands for. In a `return` the type is the
// function's own: the generic's declared result, a payload enum (a typedef'd name, as the module spells it) that the
// compiler has read already, wherever the generic's module is. The replay writes the compound literal the lowering
// writes for a source's own `return`. Other contexts (a comparison, an assignment, an argument) are an error where the
// generic is recorded.

// The payload enum a generic's declared result type is, from the token before the function's name (`Opt`, `x::Opt` or
// `struct smith_enum_...`), or false when it is none.
static bool smith_lang_enum_result_shape(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                         const SmithLangInstance *instance, SmithLangEnumType *type, bool *is_shape);

static bool smith_lang_enum_result(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                   const SmithLangInstance *instance, SmithLangEnumType *type) {
    if (generic == nullptr || generic->name_index < 1) return false;
    bool is_shape = false;
    bool good = smith_lang_enum_result_shape(lang, host, generic, instance, type, &is_shape);
    if (is_shape) return good;
    const SmithLangGenToken *name = &smith_lang_gt.tokens[generic->first + generic->name_index];
    uint32_t at = generic->first + generic->name_index;
    SmithToken span[3];
    size_t count = 0;
    const SmithLangGenToken *before = &smith_lang_gt.tokens[at - 1];
    if (before->kind != SmithTokenKind_Identifier) return false;
    if (at >= generic->first + 3 && smith_lang_gt_punct(&smith_lang_gt.tokens[at - 2], "::")) {
        span[count++] = smith_lang_gt_token(&smith_lang_gt, &smith_lang_gt.tokens[at - 3]);
        span[count++] = smith_lang_gt_token(&smith_lang_gt, &smith_lang_gt.tokens[at - 2]);
    }
    span[count++] = smith_lang_gt_token(&smith_lang_gt, before);
    char canonical[smith_lang_enum_name_max + 16];
    if (count == 1 && before->length > 11 && memcmp(smith_lang_gt_str(before), "smith_enum_", 11) == 0) {
        snprintf(canonical, sizeof canonical, "struct %.*s", (int)before->length, smith_lang_gt_str(before));
    } else {
        char resolved[smith_lang_name_max];
        smith_lang_tuple_resolve(lang, span, 0, count, resolved, sizeof resolved);
        snprintf(canonical, sizeof canonical, "struct smith_enum_%s", resolved);
    }
    (void)name;
    if (smith_lang_enum_type(host, canonical, type)) return true;
    if (host->expr_type == nullptr) return false;
    // A name of a `typedef option(T) Name;` or `typedef result(T, E) Name;`: the compiler prints the struct under it.
    char resolved[smith_lang_name_max];
    char text[smith_lang_name_max + 16];
    SmithExprType found = {};
    smith_lang_tuple_resolve(lang, span, 0, count, resolved, sizeof resolved);
    int length = snprintf(text, sizeof text, "(\x1f%s\x1f)\x1f{\x1f}", resolved);
    return length > 0 && (size_t)length < sizeof text &&
           host->expr_type(host->context, text, (uint32_t)length, &found) &&
           smith_lang_enum_type(host, found.canonical, type);
}

// The words of `return .name ;` and `return { .name = payload }` at the `return` token `at`: `lead ( T ) { . tag = C
// ...`. `lead` is `return`, or the start of a declaration (`{ T smith_ret_1 =`) when an `errdefer` reads the value
// first. `next` is where the generic's own tokens go on (after `{ . name =`, or after the bare value), `braced` says
// that the compound literal's `{` is written here and its `}` is the generic's.
static bool smith_lang_enum_return_words(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                         uint32_t at, uint32_t end, const char *lead, char *words, size_t capacity,
                                         uint32_t *next, bool *braced_out) {
    if (replay->instance == nullptr || replay->typing || at + 3 >= end) return false;
    SmithLangGenToken *keyword = &smith_lang_gt.tokens[at];
    bool braced = smith_lang_gt_punct(&smith_lang_gt.tokens[at + 1], "{");
    uint32_t dot = at + (braced ? 2 : 1);
    const SmithLangGenToken *variant = &smith_lang_gt.tokens[dot + 1];
    bool bare = !braced && smith_lang_gt_punct(&smith_lang_gt.tokens[at + 3], ";");
    if (!smith_lang_gt_punct(&smith_lang_gt.tokens[dot], ".") || variant->kind != SmithTokenKind_Identifier ||
        (braced && !smith_lang_gt_punct(&smith_lang_gt.tokens[dot + 2], "=")) || (!braced && !bare)) {
        return false;
    }
    SmithLangEnumType type;
    if (!smith_lang_enum_result(lang, host, replay->generic, replay->instance, &type)) return false;
    SmithToken dot_token = smith_lang_gt_token(&smith_lang_gt, &smith_lang_gt.tokens[dot]);
    SmithToken name_token = smith_lang_gt_token(&smith_lang_gt, variant);
    char constant[smith_lang_enum_name_max + 64];
    (void)keyword;
    if (!smith_lang_enum_variant(lang, host, &type, &dot_token, &name_token, bare, constant, sizeof constant)) {
        snprintf(words, capacity, "%s ( %s ) { }", lead, type.type); // a value of the type: nothing else is said
    } else if (bare) {
        snprintf(words, capacity, "%s ( %s ) { . tag = %s }", lead, type.type, constant);
    } else {
        snprintf(words, capacity, "%s ( %s ) { . tag = %s , . %.*s =", lead, type.type, constant, (int)variant->length,
                 smith_lang_gt_str(variant));
    }
    *next = dot + (braced ? 3 : 2);
    *braced_out = braced;
    return true;
}

// The `;` that ends the statement of the generic's tokens that begins after `at`, or UINT32_MAX when there is none.
static uint32_t smith_lang_enum_statement_end(uint32_t at, uint32_t end) {
    int depth = 0;
    for (uint32_t i = at + 1; i < end; i++) {
        const SmithLangGenToken *token = &smith_lang_gt.tokens[i];
        if (token->special != SmithLangGen_Plain) continue;
        depth += smith_lang_gt_punct(token, "(") || smith_lang_gt_punct(token, "[") || smith_lang_gt_punct(token, "{");
        depth -= smith_lang_gt_punct(token, ")") || smith_lang_gt_punct(token, "]") || smith_lang_gt_punct(token, "}");
        if (depth == 0 && smith_lang_gt_punct(token, ";")) return i;
        if (depth < 0) return UINT32_MAX;
    }
    return UINT32_MAX;
}

// `return v;` in an instance whose result is an `option(T)` or a `result(T, E)`: `v` is the `.some` or the `.ok`,
// unless it is a value of the result type itself. The instance's body has no types the lowering can ask the compiler
// for, so the compiler picks: the value is held once, and a `_Generic` on its type returns it as it is or wraps it. The
// branch not taken is never a conversion that fails, because its payload is a zero of the payload's type there.
static bool smith_lang_enum_replay_wrap(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                        SmithLangFrame *frame) {
    uint32_t at = frame->pos;
    const SmithLangGenToken *first = &smith_lang_gt.tokens[at + 1];
    if (first->special != SmithLangGen_Plain || smith_lang_gt_punct(first, ".") || smith_lang_gt_punct(first, "{") ||
        smith_lang_gt_punct(first, ";") || smith_lang_gt_is(first, SmithTokenKind_Identifier, "try") ||
        host->type_query == nullptr) {
        return false;
    }
    SmithLangEnumType type;
    bool is_result;
    if (!smith_lang_enum_result(lang, host, replay->generic, replay->instance, &type) ||
        !smith_lang_enum_builtin(&type, &is_result)) {
        return false;
    }
    uint32_t stop = smith_lang_enum_statement_end(at, frame->end);
    if (stop == UINT32_MAX) return false;
    uint32_t place = smith_lang_gen_place(host, replay->generic, &smith_lang_gt.tokens[at], replay->location);
    const char *variant = is_result ? "ok" : "some";
    uint32_t id = ++replay->try_next;
    char head[smith_lang_enum_name_max * 2 + 400];
    int used = snprintf(head, sizeof head, "return __extension__ ( { ");
    if (is_result) {
        used += snprintf(head + used, sizeof head - (size_t)used,
                         "_Static_assert ( ! __builtin_types_compatible_p ( __typeof__ ( ( ( %s * ) 0 ) -> ok ) , "
                         "__typeof__ ( ( ( %s * ) 0 ) -> error ) ) , \"`ok` and `error` have one type: write the "
                         "variant out\" ) ; ",
                         type.type, type.type);
    }
    snprintf(head + used, sizeof head - (size_t)used, "__auto_type smith_wrap_%u =", id);
    char tail[smith_lang_enum_name_max * 5 + 300];
    snprintf(
        tail, sizeof tail,
        "; _Generic ( smith_wrap_%u , %s : smith_wrap_%u , default : ( %s ) { . tag = %s_tag_%s , . %s = _Generic ( "
        "smith_wrap_%u , %s : ( ( %s * ) 0 ) -> %s , default : smith_wrap_%u ) } ) ; } )",
        id, type.type, id, type.type, type.type + 7, variant, variant, id, type.type, type.type, variant, id);
    frame->pos = stop; // the `;` goes on after the statement expression
    SmithLangSlicePart parts[] = {{head, 0, 0}, {nullptr, at + 1, stop}, {tail, 0, 0}};
    smith_lang_slice_parts(replay, place, parts, 3);
    return true;
}

// At `return` in an instance's body: `return .name ;` and `return { .name = payload }` are written as the compound
// literal of the function's payload enum. True when the frame's tokens were taken.
static bool smith_lang_enum_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                   SmithLangFrame *frame) {
    uint32_t at = frame->pos;
    if (replay->instance == nullptr || replay->typing || at + 3 >= frame->end) return false;
    SmithLangGenToken *keyword = &smith_lang_gt.tokens[at];
    if (keyword->special != SmithLangGen_Plain || keyword->param != 0 ||
        !smith_lang_gt_is(keyword, SmithTokenKind_Identifier, "return")) {
        return false;
    }
    char words[smith_lang_enum_name_max * 3 + 96];
    uint32_t next = 0;
    bool braced = false;
    if (!smith_lang_enum_return_words(lang, host, replay, at, frame->end, "return", words, sizeof words, &next,
                                      &braced)) {
        return smith_lang_enum_replay_wrap(lang, host, replay, frame);
    }
    uint32_t place = smith_lang_gen_place(host, replay->generic, keyword, replay->location);
    frame->pos = next;
    replay->braces += braced ? 1 : 0; // the `{` is written here, and the generic's `}` closes it
    SmithLangSlicePart parts[] = {{words, 0, 0}};
    smith_lang_slice_parts(replay, place, parts, 1);
    replay->frames[replay->count - 1].line_start = (keyword->flags & smith_lang_gen_flag_line_start) != 0;
    return true;
}

// ---- `try` and `errdefer` in an instance's body (issue #357)
//
// The generic's body is recorded as written (`try`, `errdefer` and all), and the instance is where the function's
// kind is known: its result type is a `result(T, E)` or an `option(T)` by then, the same that `smith_lang_enum_result`
// finds for the dot rule. The replay writes what runtime/lang_try.c writes for a function of a source: the statement
// expression of a `try`, the flag and the `defer` of an `errdefer`, the value first of a `return` after one. The
// compiler checks the error type of each instance (the `_Static_assert`), and a call of the other kind has no member to
// read.

// The `;` (or, for a payload, the `,` or `}`) that ends the `try` at `at`; UINT32_MAX when a larger expression is
// around it.
static uint32_t smith_lang_try_replay_end(uint32_t at, uint32_t end, bool payload) {
    int depth = 0;
    for (uint32_t i = at + 1; i < end; i++) { // bounded by the frame
        const SmithLangGenToken *token = &smith_lang_gt.tokens[i];
        if (token->special != SmithLangGen_Plain) continue;
        if (smith_lang_gt_is(token, SmithTokenKind_Identifier, "try")) return UINT32_MAX;
        if (smith_lang_gt_punct(token, "(") || smith_lang_gt_punct(token, "[") || smith_lang_gt_punct(token, "{")) {
            depth += 1;
        } else if (smith_lang_gt_punct(token, ")") || smith_lang_gt_punct(token, "]") ||
                   smith_lang_gt_punct(token, "}")) {
            if (depth == 0) return payload && smith_lang_gt_punct(token, "}") ? i : UINT32_MAX;
            depth -= 1;
        } else if (depth == 0 && smith_lang_gt_punct(token, ";")) {
            return payload ? UINT32_MAX : i;
        } else if (depth == 0 && smith_lang_gt_punct(token, ",")) {
            if (payload) return i;
            return UINT32_MAX;
        }
    }
    return UINT32_MAX;
}

static void smith_lang_try_replay_prune(SmithLangReplay *replay) {
    while (replay->flag_count > 0 && replay->flags[replay->flag_count - 1].depth > replay->braces)
        replay->flag_count -= 1;
}

static bool smith_lang_try_replay_designated(uint32_t at) {
    return at >= 4 && smith_lang_gt.tokens[at - 2].kind == SmithTokenKind_Identifier &&
           smith_lang_gt_punct(&smith_lang_gt.tokens[at - 3], ".") &&
           (smith_lang_gt_punct(&smith_lang_gt.tokens[at - 4], "{") ||
            smith_lang_gt_punct(&smith_lang_gt.tokens[at - 4], ","));
}

// The words that end a `try`: the return of the failure and the value, as smith_lang_try_close writes them.
static void smith_lang_try_replay_close(const SmithLangReplay *replay, const char *type, uint32_t id, bool wrap,
                                        char *out, size_t capacity) {
    bool result = strncmp(type, "struct smith_enum_result_", 25) == 0;
    const char *name = type + 7;
    const char *failure = result ? "error" : "none";
    const char *good = result ? "ok" : "some";
    char flags[256] = "";
    size_t used = 0;
    for (uint32_t i = 0; i < replay->flag_count && used < sizeof flags; i++)
        used += (size_t)snprintf(flags + used, sizeof flags - used, "smith_failing_%u = true ; ", replay->flags[i].id);
    char same[400] = "";
    char early[300];
    if (result) {
        snprintf(same, sizeof same,
                 "_Static_assert ( __builtin_types_compatible_p ( __typeof__ ( smith_try_%u . error ) , __typeof__ ( "
                 "( ( %s * ) 0 ) -> error ) ) , \"`try` returns the error as it is: the function's error type must "
                 "be the call's; to change it, write it out\" ) ;",
                 id, type);
        snprintf(early, sizeof early, "return ( %s ) { . tag = %s_tag_%s , . error = smith_try_%u . error } ;", type,
                 name, failure, id);
    } else {
        snprintf(early, sizeof early, "return ( %s ) { . tag = %s_tag_%s } ;", type, name, failure);
    }
    char value[400];
    if (wrap) { // `return try f();` returns the success value as the variant that holds it
        snprintf(value, sizeof value, "( %s ) { . tag = %s_tag_%s , . %s = smith_try_%u . %s }", type, name, good, good,
                 id, good);
    } else {
        snprintf(value, sizeof value, "smith_try_%u . %s", id, good);
    }
    snprintf(out, capacity,
             "; %s if ( smith_try_%u . tag == %s_tag_%s ) { %s%s } if ( smith_try_%u . tag != %s_tag_%s ) "
             "__builtin_unreachable ( ) ; %s ; } )",
             same, id, name, failure, flags, early, id, name, good, value);
}

static void smith_lang_try_replay_larger(SmithLang *lang, const SmithHost *host, uint32_t place) {
    smith_lang_error(lang, host, place,
                     "`try` is a statement of its own, the whole value of a declaration, an assignment or a `return`: "
                     "inside a larger expression the order of evaluation would be unclear when it fails; give it "
                     "its own declaration (`T x = try f();`) and use `x`");
}

// At `try`: its statement expression, around the generic's own tokens up to the end of the statement.
static bool smith_lang_try_replay_try(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                      SmithLangFrame *frame) {
    uint32_t at = frame->pos;
    uint32_t place = smith_lang_gen_place(host, replay->generic, &smith_lang_gt.tokens[at], replay->location);
    SmithLangEnumType type;
    if (host->type_query == nullptr) return false; // the raw lowering has no compiler: the tokens stay as written
    if (!smith_lang_enum_result(lang, host, replay->generic, replay->instance, &type) ||
        smith_lang_try_kind(type.type) == SmithLangTryKind_None) {
        smith_lang_error(lang, host, place,
                         "`try` returns the failure from the function, so the function must return a `result(T, E)` "
                         "or an `option(T)`; this one returns something else. Test the value by hand: `if (r == "
                         ".error) return ...;`");
        frame->pos += 1;
        return true;
    }
    const SmithLangGenToken *before = at > frame->begin ? &smith_lang_gt.tokens[at - 1] : nullptr;
    bool payload = before != nullptr && smith_lang_gt_punct(before, "=") && smith_lang_try_replay_designated(at);
    bool value = payload || before == nullptr ? false
                                              : smith_lang_gt_punct(before, "=") ||
                                                    smith_lang_gt_is(before, SmithTokenKind_Identifier, "return");
    bool wrap = value && smith_lang_gt_is(before, SmithTokenKind_Identifier, "return");
    bool statement = !payload && !value;
    if (statement && before != nullptr) {
        statement = smith_lang_gt_is(before, SmithTokenKind_Identifier, "else") || smith_lang_gt_punct(before, ";") ||
                    smith_lang_gt_punct(before, "{") || smith_lang_gt_punct(before, "}") ||
                    smith_lang_gt_punct(before, ")") || smith_lang_gt_punct(before, ":");
    }
    uint32_t stop = (statement || value || payload) ? smith_lang_try_replay_end(at, frame->end, payload) : UINT32_MAX;
    if (stop == UINT32_MAX) {
        smith_lang_try_replay_larger(lang, host, place);
        frame->pos += 1;
        return true;
    }
    if (host->hint != nullptr) host->hint(host->context, place, smith_lang_try_hint);
    smith_lang_try_replay_prune(replay);
    uint32_t id = ++replay->try_next;
    char head[120];
    snprintf(head, sizeof head, "%s __extension__ ( { __auto_type smith_try_%u =", statement ? "( void )" : "", id);
    char tail[1400];
    smith_lang_try_replay_close(replay, type.type, id, wrap, tail, sizeof tail);
    frame->pos = stop; // the end token goes on after the statement expression
    SmithLangSlicePart parts[] = {{head, 0, 0}, {nullptr, at + 1, stop}, {tail, 0, 0}};
    smith_lang_slice_parts(replay, place, parts, 3);
    return true;
}

// At `errdefer`: a flag of the block, and a `defer` that tests it; the statement is the generic's own tokens.
static bool smith_lang_try_replay_errdefer(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                           SmithLangFrame *frame) {
    uint32_t at = frame->pos;
    uint32_t place = smith_lang_gen_place(host, replay->generic, &smith_lang_gt.tokens[at], replay->location);
    SmithLangEnumType type;
    if (host->type_query == nullptr) return false;
    frame->pos += 1;
    if (!smith_lang_enum_result(lang, host, replay->generic, replay->instance, &type) ||
        smith_lang_try_kind(type.type) == SmithLangTryKind_None) {
        smith_lang_error(lang, host, place,
                         "`errdefer` runs when the function fails, and only a function that returns a `result` or "
                         "an `option` has a failure to mark; use `defer` here");
        return true;
    }
    smith_lang_try_replay_prune(replay);
    if (replay->flag_count == sizeof replay->flags / sizeof replay->flags[0]) {
        smith_lang_error(lang, host, place, "a function has at most %zu `errdefer`s open at once",
                         sizeof replay->flags / sizeof replay->flags[0]);
        return true;
    }
    uint32_t id = ++replay->try_next;
    replay->flags[replay->flag_count].id = id;
    replay->flags[replay->flag_count].depth = replay->braces;
    replay->flag_count += 1;
    char words[120];
    snprintf(words, sizeof words, "bool smith_failing_%u = false ; _Defer if ( smith_failing_%u )", id, id);
    SmithLangSlicePart parts[] = {{words, 0, 0}};
    smith_lang_slice_parts(replay, place, parts, 1);
    return true;
}

// At `return` while an `errdefer` is open: the value is computed first, the flags read its variant, then it returns.
static bool smith_lang_try_replay_return(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                         SmithLangFrame *frame) {
    uint32_t at = frame->pos;
    smith_lang_try_replay_prune(replay);
    SmithLangEnumType type;
    if (replay->flag_count == 0 || !smith_lang_enum_result(lang, host, replay->generic, replay->instance, &type) ||
        smith_lang_try_kind(type.type) == SmithLangTryKind_None) {
        return false;
    }
    uint32_t stop = smith_lang_enum_statement_end(at, frame->end);
    if (stop == UINT32_MAX) return false;
    uint32_t place = smith_lang_gen_place(host, replay->generic, &smith_lang_gt.tokens[at], replay->location);
    uint32_t id = ++replay->try_next;
    char lead[smith_lang_enum_name_max + 64];
    snprintf(lead, sizeof lead, "{ %s smith_ret_%u =", type.type, id);
    char head[smith_lang_enum_name_max * 3 + 96];
    uint32_t next = at + 1;
    bool braced = false;
    if (smith_lang_enum_return_words(lang, host, replay, at, stop + 1, lead, head, sizeof head, &next, &braced)) {
        replay->braces += braced ? 1 : 0;
    } else {
        snprintf(head, sizeof head, "%s", lead);
    }
    char tail[800];
    size_t used = (size_t)snprintf(tail, sizeof tail, "; ");
    const char *failure = strncmp(type.type, "struct smith_enum_result_", 25) == 0 ? "error" : "none";
    for (uint32_t i = 0; i < replay->flag_count && used < sizeof tail; i++) {
        used +=
            (size_t)snprintf(tail + used, sizeof tail - used, "smith_failing_%u = smith_ret_%u . tag == %s_tag_%s ; ",
                             replay->flags[i].id, id, type.type + 7, failure);
    }
    snprintf(tail + used, sizeof tail - used, "return smith_ret_%u ; }", id);
    frame->pos = stop + 1; // the `;` is the tail's
    SmithLangSlicePart parts[] = {{head, 0, 0}, {nullptr, next, stop}, {tail, 0, 0}};
    smith_lang_slice_parts(replay, place, parts, 3);
    return true;
}

// What an instance's body does with `try`, `errdefer` and a `return` after an `errdefer`.
static bool smith_lang_try_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                  SmithLangFrame *frame) {
    if (replay->instance == nullptr || replay->typing || frame->pos >= frame->end) return false;
    const SmithLangGenToken *token = &smith_lang_gt.tokens[frame->pos];
    if (token->special != SmithLangGen_Plain || token->param != 0 || token->kind != SmithTokenKind_Identifier ||
        smith_lang_member_before(frame->pos)) {
        return false;
    }
    if (smith_lang_gt_is(token, SmithTokenKind_Identifier, "try"))
        return smith_lang_try_replay_try(lang, host, replay, frame);
    if (smith_lang_gt_is(token, SmithTokenKind_Identifier, "errdefer")) {
        return smith_lang_try_replay_errdefer(lang, host, replay, frame);
    }
    if (replay->flag_count > 0 && smith_lang_gt_is(token, SmithTokenKind_Identifier, "return")) {
        return smith_lang_try_replay_return(lang, host, replay, frame);
    }
    return false;
}

// ---- A type function that returns an enum (issue #198, PR 3b)
//
// `type option(type T) { return enum { T some; none; }; }` is read by the lowering, never run: its body is the shape of
// the enum (the tokens between the braces, with the parameters in them), recorded here, and the declaration is dropped.
// A use, `option(usize)`, is `struct smith_enum_option_usize`: the name follows the tuples' (`smith_lang_tuple_spell`
// of the arguments, cut and hashed when long). The struct is defined, as `smith_lang_enum_build` writes a payload enum,
// at the first declaration that names it (before its first token), in the scope that declaration is in; a use where no
// definition is in scope is an error naming the fix. The limits are the implementation's, and an error says so.

constexpr size_t smith_lang_eshape_max = 16;
constexpr size_t smith_lang_eshape_params_max = 4;
constexpr size_t smith_lang_eshape_elem_max = 12; // the tokens of a slice argument's element
constexpr size_t smith_lang_eshape_tokens_max = 96;
constexpr size_t smith_lang_eshape_defined_max = 48;
constexpr size_t smith_lang_eshape_scan_max = 160;

typedef struct {
    char name[64];
    size_t param_count;
    char params[smith_lang_eshape_params_max][48];
    SmithToken tokens[smith_lang_eshape_tokens_max]; // between the braces of the enum
    size_t token_count;
    char module[64]; // the import's name that an importer writes before it (`opt::option(...)`); empty: this file's own
    bool exported;   // `export type option(...)`: the header carries the shape
    bool builtin;    // `option` and `result`: in scope in every module, from the library's own headers
} SmithLangEnumShape;

typedef struct {
    char tag[smith_lang_tuple_name_max];
    uint32_t depth; // the brace depth of the definition: it ends with that block
    bool header;    // the module's header holds the definition too (an exported declaration names it)
    bool made;      // defined here, in a block (issue #425)
    bool closed;    // ... and the block has ended: the compiler may still find the tag until it leaves the block
} SmithLangEnumDefined;

static SmithLangEnumShape smith_lang_eshapes[smith_lang_eshape_max];
static size_t smith_lang_eshape_count;
static char smith_lang_eshape_pool[8192];
static size_t smith_lang_eshape_pool_used;
static SmithLangEnumDefined smith_lang_edefined[smith_lang_eshape_defined_max];
static size_t smith_lang_edefined_count;
static uint32_t smith_lang_eshape_depth; // `{` minus `}` of the source's tokens
static bool smith_lang_eshape_start;     // the next token begins a statement or a declaration
static int
    smith_lang_eshape_head; // inside the head of an `if`, `for`, `while` or `switch`: 1 before its `(`, then 1 + depth

// The tables are static like the lowering's, and a module's interface read in the middle of a file
// (smith_lang_keep_aside) would end with its own in them: they are set aside and put back with the lowering's.
static bool smith_lang_eshape_builtins_done; // `option` and `result` were put in scope for this file
static bool smith_lang_saved_builtins_done;
static SmithLangEnumShape smith_lang_saved_eshapes[smith_lang_eshape_max];
static size_t smith_lang_saved_eshape_count;
static char smith_lang_saved_eshape_pool[sizeof smith_lang_eshape_pool];
static size_t smith_lang_saved_eshape_pool_used;
static SmithLangEnumDefined smith_lang_saved_edefined[smith_lang_eshape_defined_max];
static size_t smith_lang_saved_edefined_count;
static uint32_t smith_lang_saved_eshape_depth;
static bool smith_lang_saved_eshape_start;
static SmithLangEnumSwitch smith_lang_saved_enum_switches[smith_lang_enum_switch_max];
static size_t smith_lang_saved_enum_switch_count;
static uint32_t smith_lang_saved_enum_braces;

static void smith_lang_enum_aside(bool save) {
    if (save) {
        memcpy(smith_lang_saved_eshapes, smith_lang_eshapes, sizeof smith_lang_eshapes);
        memcpy(smith_lang_saved_eshape_pool, smith_lang_eshape_pool, sizeof smith_lang_eshape_pool);
        memcpy(smith_lang_saved_edefined, smith_lang_edefined, sizeof smith_lang_edefined);
        memcpy(smith_lang_saved_enum_switches, smith_lang_enum_switches, sizeof smith_lang_enum_switches);
        smith_lang_saved_eshape_count = smith_lang_eshape_count;
        smith_lang_saved_builtins_done = smith_lang_eshape_builtins_done;
        smith_lang_saved_eshape_pool_used = smith_lang_eshape_pool_used;
        smith_lang_saved_edefined_count = smith_lang_edefined_count;
        smith_lang_saved_eshape_depth = smith_lang_eshape_depth;
        smith_lang_saved_eshape_start = smith_lang_eshape_start;
        smith_lang_saved_enum_switch_count = smith_lang_enum_switch_count;
        smith_lang_saved_enum_braces = smith_lang_enum_braces;
    } else {
        memcpy(smith_lang_eshapes, smith_lang_saved_eshapes, sizeof smith_lang_eshapes);
        memcpy(smith_lang_eshape_pool, smith_lang_saved_eshape_pool, sizeof smith_lang_eshape_pool);
        memcpy(smith_lang_edefined, smith_lang_saved_edefined, sizeof smith_lang_edefined);
        memcpy(smith_lang_enum_switches, smith_lang_saved_enum_switches, sizeof smith_lang_enum_switches);
        smith_lang_eshape_count = smith_lang_saved_eshape_count;
        smith_lang_eshape_builtins_done = smith_lang_saved_builtins_done;
        smith_lang_eshape_pool_used = smith_lang_saved_eshape_pool_used;
        smith_lang_edefined_count = smith_lang_saved_edefined_count;
        smith_lang_eshape_depth = smith_lang_saved_eshape_depth;
        smith_lang_eshape_start = smith_lang_saved_eshape_start;
        smith_lang_enum_switch_count = smith_lang_saved_enum_switch_count;
        smith_lang_enum_braces = smith_lang_saved_enum_braces;
    }
}

static void smith_lang_enum_shapes_reset(void) {
    smith_lang_eshape_builtins_done = false;
    smith_lang_eshape_count = 0;
    smith_lang_eshape_pool_used = 0;
    smith_lang_edefined_count = 0;
    smith_lang_eshape_depth = 0;
    smith_lang_eshape_start = true;
    smith_lang_eshape_head = 0;
}

static const char *smith_lang_eshape_keep(const char *text, size_t length) {
    if (smith_lang_eshape_pool_used + length + 1 > sizeof smith_lang_eshape_pool) return nullptr;
    char *at = smith_lang_eshape_pool + smith_lang_eshape_pool_used;
    memcpy(at, text, length);
    at[length] = 0;
    smith_lang_eshape_pool_used += length + 1;
    return at;
}

// The token `index` places after the one being lowered (0 is the one itself).
static const SmithToken *smith_lang_eshape_at(SmithLang *lang, const SmithHost *host, const SmithToken *first,
                                              size_t index) {
    return index == 0 ? first : smith_lang_peek(lang, host, index - 1, false);
}

static const SmithLangEnumShape *smith_lang_eshape_find(const SmithToken *token) {
    if (token->kind != SmithTokenKind_Identifier) return nullptr;
    for (size_t i = 0; i < smith_lang_eshape_count; i++) { // at most 16
        if (smith_lang_equals(token->text, token->length, smith_lang_eshapes[i].name)) return &smith_lang_eshapes[i];
    }
    return nullptr;
}

constexpr char smith_lang_eshape_gap[] = "a type function that returns an enum must be exactly `return enum {...};` "
                                         "with only `type` parameters for now (known gap)";

// The index of the `)` that closes the `(` at `open`, within the scan limit, or 0.
static size_t smith_lang_eshape_close(SmithLang *lang, const SmithHost *host, const SmithToken *first, size_t open,
                                      const char *left, const char *right) {
    int depth = 0;
    for (size_t at = open; at < smith_lang_eshape_scan_max * 4 && at < smith_lang_queue_max - 2; at++) {
        const SmithToken *token = smith_lang_eshape_at(lang, host, first, at);
        if (token->kind == SmithTokenKind_Eof || token->foreign) return 0;
        depth += smith_lang_punct(token, left);
        depth -= smith_lang_punct(token, right);
        if (depth == 0) return at;
    }
    return 0;
}

// Another body that returns an enum somewhere (`if (...) return enum {...};`) is the known gap, said here instead of by
// the compiler's "cannot read the type `enum`" three errors later: the function is dropped. False for any other
// function.
static bool smith_lang_eshape_other(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                    const SmithToken *name, size_t right) {
    size_t end = smith_lang_eshape_close(lang, host, token, right + 1, "{", "}");
    for (size_t at = right + 3; end != 0 && at < end; at++) {
        if (smith_lang_ident(smith_lang_eshape_at(lang, host, token, at), "enum") &&
            smith_lang_ident(smith_lang_eshape_at(lang, host, token, at - 1), "return")) {
            smith_lang_error(lang, host, name->location, "%s", smith_lang_eshape_gap);
            smith_lang_eshape_start = true;
            static SmithToken nothing[1];
            return smith_lang_tuple_replace(lang, end, nothing, 0);
        }
    }
    return false;
}

// A `.name` (a variant by itself) in a generic's body other than after `return`: the instance's body is written without
// the compiler, so the type it stands for is unknown there. Said where the generic is recorded, with the fix.
static uint32_t smith_lang_enum_return_dot; // where the `.` after the last `return` of a generic's body is

static void smith_lang_enum_generic_dot(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (smith_lang_ident(token, "return")) {
        const SmithToken *next = smith_lang_peek(lang, host, 0, false);
        smith_lang_enum_return_dot = smith_lang_punct(next, ".") ? next->location : 0;
        return;
    }
    if (!smith_lang_punct(token, ".") || !token->space_before || token->location == smith_lang_enum_return_dot) return;
    const SmithToken *name = smith_lang_peek(lang, host, 0, false);
    const SmithToken *after = smith_lang_peek(lang, host, 1, false);
    if (name->kind != SmithTokenKind_Identifier || smith_lang_punct(after, "=")) return; // `{.x = 1}` is C's own
    smith_lang_error(
        lang, host, token->location,
        "`.%.*s` in a generic's body needs the type it stands for, and a generic's body is written without "
        "the compiler, so only `return .variant;` and `return {.variant = payload};` are built for now "
        "(known gap)",
        (int)name->length, name->text);
}

// At `type` where a declaration begins: `type f(type T, ...) { return enum { ... }; }` is recorded and dropped. False
// for any other function (the type values read it), true when this one was consumed (or reported).
// A module that declares `option` or `result` (the library's own two are the exceptions): an error naming the built-in.
static bool smith_lang_eshape_builtin_clash(SmithLang *lang, const SmithHost *host, const SmithToken *name) {
    bool library = strcmp(lang->module, "smith::option") == 0 || strcmp(lang->module, "smith::result") == 0;
    if (library || !(smith_lang_equals(name->text, name->length, "option") ||
                     smith_lang_equals(name->text, name->length, "result"))) {
        return false;
    }
    smith_lang_error(lang, host, name->location,
                     "`%.*s` is a built-in type function (smith::%.*s), in scope in every module: a module cannot "
                     "declare its own; pick another name",
                     (int)name->length, name->text, (int)name->length, name->text);
    return true;
}

static bool smith_lang_eshape_declare(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    const SmithToken *name = smith_lang_eshape_at(lang, host, token, 1);
    if (name->kind != SmithTokenKind_Identifier || !smith_lang_punct(smith_lang_eshape_at(lang, host, token, 2), "(")) {
        return false;
    }
    size_t right = smith_lang_eshape_close(lang, host, token, 2, "(", ")");
    if (right == 0 || !smith_lang_punct(smith_lang_eshape_at(lang, host, token, right + 1), "{")) return false;
    if (!smith_lang_ident(smith_lang_eshape_at(lang, host, token, right + 2), "return") ||
        !smith_lang_ident(smith_lang_eshape_at(lang, host, token, right + 3), "enum") ||
        !smith_lang_punct(smith_lang_eshape_at(lang, host, token, right + 4), "{")) {
        return smith_lang_eshape_other(lang, host, token, name, right);
    }
    size_t open = right + 4;
    size_t close = smith_lang_eshape_close(lang, host, token, open, "{", "}");
    size_t count = close + 2; // tokens after `type`, through the function's `}`
    if (close == 0 || !smith_lang_punct(smith_lang_eshape_at(lang, host, token, close + 1), ";") ||
        !smith_lang_punct(smith_lang_eshape_at(lang, host, token, close + 2), "}") || count >= smith_lang_queue_max) {
        smith_lang_error(lang, host, name->location, "%s", smith_lang_eshape_gap);
        return false;
    }
    if (smith_lang_eshape_builtin_clash(lang, host, name)) {
        smith_lang_eshape_start = true;
        static SmithToken nothing[1]; // the function is dropped, so that nothing else is said of it
        return smith_lang_tuple_replace(lang, count, nothing, 0);
    }
    SmithLangEnumShape *shape = &smith_lang_eshapes[smith_lang_eshape_count];
    memset(shape, 0, sizeof *shape);
    snprintf(shape->name, sizeof shape->name, "%.*s", (int)name->length, name->text);
    for (size_t at = 3; at < right; at += 3) { // `type T` then `,` or `)`
        const SmithToken *word = smith_lang_eshape_at(lang, host, token, at);
        const SmithToken *param = smith_lang_eshape_at(lang, host, token, at + 1);
        const SmithToken *after = smith_lang_eshape_at(lang, host, token, at + 2);
        bool good = smith_lang_ident(word, "type") && param->kind == SmithTokenKind_Identifier &&
                    (at + 2 == right ? smith_lang_punct(after, ")") : smith_lang_punct(after, ",")) &&
                    shape->param_count < smith_lang_eshape_params_max;
        if (!good) {
            smith_lang_error(lang, host, word->location, "%s", smith_lang_eshape_gap);
            smith_lang_eshape_start = true;
            static SmithToken nothing[1]; // the function is dropped, so that nothing else is said of it
            return smith_lang_tuple_replace(lang, count, nothing, 0);
        }
        snprintf(shape->params[shape->param_count++], sizeof shape->params[0], "%.*s", (int)param->length, param->text);
    }
    if (smith_lang_eshape_count >= smith_lang_eshape_max || close - open - 1 > smith_lang_eshape_tokens_max) {
        smith_lang_error(lang, host, name->location, "this type function is too long, or there are too many of them");
        return false;
    }
    for (size_t at = open + 1; at < close; at++) { // the variants, with their text kept
        SmithToken copy = *smith_lang_eshape_at(lang, host, token, at);
        copy.text = smith_lang_eshape_keep(copy.text, copy.length);
        copy.handle = smith_token_no_handle; // the host's pool of tokens is not kept from one token to the next
        if (copy.text == nullptr) {
            smith_lang_error(lang, host, name->location, "this type function is too long");
            return false;
        }
        shape->tokens[shape->token_count++] = copy;
    }
    if (lang->pending_export) { // `export type option(...)`: the header carries the shape, nothing else is exported
        shape->exported = true;
        lang->pending_export = false;
    }
    smith_lang_eshape_count += 1;
    lang->tuples_seen = true;
    lang->slices_seen = true;
    lang->enums_seen = true;
    static SmithToken none[1];
    smith_lang_eshape_start = true; // what follows is a declaration
    return smith_lang_tuple_replace(lang, count, none, 0);
}

// The arguments of the call whose `(` is at `open`: each a run of tokens, `args` holding all of them, the index of the
// `)` in `close`. False (an error said) when the call is not one of types the shape takes.
typedef struct {
    SmithToken tokens[smith_lang_eshape_tokens_max];
    size_t begin[smith_lang_eshape_params_max]; // each argument's tokens, [begin, end), commas apart
    size_t end[smith_lang_eshape_params_max];
    size_t count;
    size_t close;
    char slice_tag[smith_lang_eshape_params_max][smith_lang_tuple_name_max]; // an argument written `T[:]`: its struct
    SmithToken slice_elem[smith_lang_eshape_params_max][smith_lang_eshape_elem_max]; // and its element's tokens
    size_t slice_elems[smith_lang_eshape_params_max];                                // (0: the argument is no slice)
} SmithLangEnumCall;

// An argument written as a slice, `option(f32[:])`, is the slice's struct, as `option(typeof(f32[:]))` would write it:
// the argument becomes `struct smith_array_f32_n` and `smith_lang_eshape_define` defines that struct in front of the
// option's when no earlier declaration did.
static void smith_lang_eshape_slice_args(SmithLang *lang, const SmithLangEnumShape *shape, SmithLangEnumCall *call) {
    for (size_t p = shape->param_count; p > 0; p--) { // the last first: a shorter argument shifts the ones after it
        size_t begin = call->begin[p - 1];
        size_t end = call->end[p - 1];
        call->slice_elems[p - 1] = 0;
        if (end >= begin + 7 && smith_lang_ident(&call->tokens[begin], "typeof") &&
            smith_lang_punct(&call->tokens[begin + 1], "(") && smith_lang_punct(&call->tokens[end - 1], ")") &&
            smith_lang_punct(&call->tokens[end - 2], "]") && smith_lang_punct(&call->tokens[end - 3], ":") &&
            smith_lang_punct(&call->tokens[end - 4], "[")) { // `typeof(T[:])` is the slice type `T[:]`
            memmove(&call->tokens[end - 1], &call->tokens[end], (call->count - end) * sizeof call->tokens[0]);
            call->count -= 1; // the `)`
            memmove(&call->tokens[begin], &call->tokens[begin + 2], (call->count - begin - 2) * sizeof call->tokens[0]);
            call->count -= 2; // the `typeof (`
            end -= 3;
            call->end[p - 1] = end;
            for (size_t q = p; q < shape->param_count; q++) {
                call->begin[q] -= 3;
                call->end[q] -= 3;
            }
        }
        if (end < begin + 4 || !smith_lang_punct(&call->tokens[end - 3], "[") ||
            !smith_lang_punct(&call->tokens[end - 2], ":") || !smith_lang_punct(&call->tokens[end - 1], "]") ||
            end - begin - 3 > smith_lang_eshape_elem_max) {
            continue;
        }
        size_t elems = end - begin - 3;
        SmithLangSliceDims dims = smith_lang_slice_dims_one();
        const char *problem = nullptr;
        uint32_t where = 0;
        if (!smith_lang_slice_tag_of(lang, &call->tokens[begin], elems, &dims, call->slice_tag[p - 1],
                                     sizeof call->slice_tag[p - 1], &problem, &where)) {
            continue; // the tag's own error is said when the argument is spelled
        }
        memcpy(call->slice_elem[p - 1], &call->tokens[begin], elems * sizeof call->tokens[0]);
        call->slice_elems[p - 1] = elems;
        SmithToken like = call->tokens[begin];
        SmithToken made[2];
        made[0] = smith_lang_tuple_made(SmithTokenKind_Identifier, "struct", 6, &like, like.space_before);
        made[1] = smith_lang_tuple_made(SmithTokenKind_Identifier,
                                        smith_lang_tuple_keep(call->slice_tag[p - 1], strlen(call->slice_tag[p - 1])),
                                        strlen(call->slice_tag[p - 1]), &like, true);
        size_t shift = end - begin - 2; // the argument's tokens leave, two come
        memmove(&call->tokens[begin + 2], &call->tokens[end], (call->count - end) * sizeof call->tokens[0]);
        call->tokens[begin] = made[0];
        call->tokens[begin + 1] = made[1];
        call->count -= shift;
        call->end[p - 1] = begin + 2;
        for (size_t q = p; q < shape->param_count; q++) {
            call->begin[q] -= shift;
            call->end[q] -= shift;
        }
    }
}

// An argument that is a typedef of a slice (`option(Text)`, `typedef const char Text[:]`) is the slice's struct, as
// `option(struct smith_array_const_char_n)` writes it, so the struct is the one a generic's `option(typeof(xs[0:0]))`
// result has: a caller names that result through the typedef.
static void smith_lang_eshape_slice_names(SmithLang *lang, const SmithHost *host, const SmithLangEnumShape *shape,
                                          SmithLangEnumCall *call) {
    static const char tag[] = "struct smith_array_";
    if (host->type_query == nullptr) return;
    for (size_t p = shape->param_count; p > 0; p--) { // the last first: a longer argument shifts the ones after it
        size_t begin = call->begin[p - 1];
        const SmithToken *name = &call->tokens[begin];
        SmithTypeQuery query = {};
        char written[smith_lang_name_max]; // the name as the module's file spells it (`main_Text`)
        written[0] = 0;
        if (call->end[p - 1] == begin + 1 && name->kind == SmithTokenKind_Identifier)
            (void)smith_lang_tuple_resolve(lang, call->tokens, begin, begin + 1, written, sizeof written);
        if (call->end[p - 1] != begin + 1 || name->kind != SmithTokenKind_Identifier ||
            smith_lang_word(name->text, name->length) != SmithLangWord_Other ||
            smith_lang_find_sized(name->text, name->length) != nullptr ||
            call->count + 1 >= smith_lang_eshape_tokens_max || written[0] == 0 ||
            !host->type_query(host->context, written, (uint32_t)strlen(written), &query) ||
            strncmp(query.canonical, tag, sizeof tag - 1) != 0 || strchr(query.canonical, '*') != nullptr) {
            continue;
        }
        size_t length = strcspn(query.canonical + 7, " ");
        SmithToken like = *name;
        SmithToken made[2];
        made[0] = smith_lang_tuple_made(SmithTokenKind_Identifier, "struct", 6, &like, name->space_before);
        made[1] = smith_lang_tuple_made(SmithTokenKind_Identifier, query.canonical + 7, length, &like, true);
        memmove(&call->tokens[begin + 2], &call->tokens[begin + 1], (call->count - begin - 1) * sizeof call->tokens[0]);
        call->tokens[begin] = made[0];
        call->tokens[begin + 1] = made[1];
        call->count += 1;
        call->end[p - 1] += 1;
        for (size_t q = p; q < shape->param_count; q++) {
            call->begin[q] += 1;
            call->end[q] += 1;
        }
    }
}

static bool smith_lang_eshape_call(SmithLang *lang, const SmithHost *host, const SmithToken *first, size_t open,
                                   const SmithLangEnumShape *shape, SmithLangEnumCall *call, bool report) {
    call->count = 0;
    call->close = smith_lang_eshape_close(lang, host, first, open, "(", ")");
    const SmithToken *name = smith_lang_eshape_at(lang, host, first, open - 1);
    if (call->close == 0 || call->close - open - 1 > smith_lang_eshape_tokens_max) {
        if (report) {
            smith_lang_error(lang, host, name->location, "the arguments of `%s` are too long, or never end",
                             shape->name);
        }
        return false;
    }
    size_t argument = 0;
    int depth = 0;
    call->begin[0] = 0;
    for (size_t at = open + 1; at < call->close; at++) {
        const SmithToken *token = smith_lang_eshape_at(lang, host, first, at);
        depth += smith_lang_punct(token, "(") || smith_lang_punct(token, "[");
        depth -= smith_lang_punct(token, ")") || smith_lang_punct(token, "]");
        bool comma = depth == 0 && smith_lang_punct(token, ",");
        if (comma && argument + 1 >= shape->param_count) {
            argument += 1; // one more than the function takes: said below
            break;
        }
        if (comma) call->end[argument] = call->count;
        call->tokens[call->count] = *token;
        call->tokens[call->count].text = smith_lang_tuple_keep(token->text, token->length); // the host reuses its own
        call->tokens[call->count].handle = smith_token_no_handle;
        call->count += 1;
        if (comma) call->begin[++argument] = call->count;
    }
    if (argument < shape->param_count) call->end[argument] = call->count;
    if (argument + 1 == shape->param_count) {
        smith_lang_eshape_slice_args(lang, shape, call);
        smith_lang_eshape_slice_names(lang, host, shape, call);
    }
    if (argument + 1 != shape->param_count) {
        if (report)
            smith_lang_error(
                lang, host, name->location,
                "`%s` takes %zu type argument%s: the arguments of a type function that returns an enum are "
                "written types for now (known gap)",
                shape->name, shape->param_count, shape->param_count == 1 ? "" : "s");
        return false;
    }
    return true;
}

static SmithLangEnumDefined *smith_lang_eshape_entry(const char *tag) {
    for (size_t i = 0; i < smith_lang_edefined_count; i++) { // at most 48
        if (strcmp(smith_lang_edefined[i].tag, tag) == 0 && !smith_lang_edefined[i].closed) {
            return &smith_lang_edefined[i];
        }
    }
    return nullptr;
}

static bool smith_lang_eshape_queued(const char *tag) {
    return smith_lang_eshape_entry(tag) != nullptr;
}

static SmithLangEnumDefined *smith_lang_eshape_note_defined(const char *tag);

// Whether a definition of the struct is in scope: this file's own, or one an import's header made (the compiler has
// read it, so a second definition would be an error: two modules that write `option(usize)` share one).
static bool smith_lang_eshape_defined(const SmithHost *host, const char *tag) {
    if (smith_lang_eshape_entry(tag) != nullptr) return true;
    for (size_t i = 0; i < smith_lang_edefined_count; i++) { // a block's definition that has ended: the parser, which
        if (smith_lang_edefined[i].closed && strcmp(smith_lang_edefined[i].tag, tag) == 0) return false; // lags the
    } // lowering by a token, may still hold it
    if (host->lookup == nullptr ||
        host->lookup(host->context, tag, (uint32_t)strlen(tag), SmithLookup_Tag).kind != SmithLookup_Tag) {
        return false;
    }
    smith_lang_eshape_note_defined(tag);
    return true;
}

// The struct's name for a call: `smith_enum_<function>_<arguments' spelling>`, cut and hashed when long.
static bool smith_lang_eshape_tag(SmithLang *lang, const SmithHost *host, const SmithLangEnumShape *shape,
                                  const SmithLangEnumCall *call, char *tag, size_t capacity, bool report) {
    char spelled[smith_lang_tuple_name_max];
    const char *problem = nullptr;
    uint32_t where = 0;
    if (!smith_lang_tuple_spell(lang, call->tokens, 0, call->count, spelled, sizeof spelled, &problem, &where)) {
        if (report)
            smith_lang_error(
                lang, host, where,
                "an argument of `%s` must be a type name, with stars and qualifiers, for now (known gap): %s",
                shape->name, problem);
        return false;
    }
    char joined[smith_lang_tuple_name_max];
    snprintf(joined, sizeof joined, "%s_%s", shape->name, spelled);
    char hashed[smith_lang_tuple_name_max];
    smith_lang_tuple_tag(joined, hashed, sizeof hashed);
    snprintf(tag, capacity, "smith_enum_%s", hashed + sizeof smith_lang_tuple_prefix - 1);
    return true;
}

// The constant of a variant of the result of a call of a generic whose result is built from its arguments
// (`option(typeof(xs[0]))`): the instance's struct has no name until the call is written, but a shape's variants are
// numbered in the order the shape writes them, so the variant is its number (`option`: `some` 0, `none` 1).
static bool smith_lang_enum_derived_constant(const SmithLangGeneric *generic, const SmithToken *variant, char *out,
                                             size_t capacity) {
    const SmithLangGenToken *head = &smith_lang_gt.tokens[generic->first + smith_lang_generic_skip_attributes(generic)];
    const SmithLangGenToken *open = head + 1;
    if (head->kind != SmithTokenKind_Identifier || !smith_lang_gt_punct(open, "(")) return false;
    for (size_t i = 0; i < smith_lang_eshape_count; i++) { // at most 16
        const SmithLangEnumShape *shape = &smith_lang_eshapes[i];
        if (!smith_lang_equals(smith_lang_gt_str(head), head->length, shape->name)) continue;
        size_t number = 0;
        for (size_t at = 1; at <= shape->token_count; at++) { // a variant ends at its `;`; its name is the token before
            if (at < shape->token_count && !smith_lang_punct(&shape->tokens[at], ";")) continue;
            const SmithToken *name = &shape->tokens[at - 1];
            if (!smith_lang_punct(name, ";") && smith_lang_equals(variant->text, variant->length, name->text) &&
                name->length == variant->length) {
                int written = snprintf(out, capacity, "%zu", number);
                return written > 0 && (size_t)written < capacity;
            }
            number += !smith_lang_punct(name, ";");
        }
        return false;
    }
    return false;
}

// The constant of a variant of the result of a call of a generic whose result is written `option(usize)` or
// `result(usize, i32)` with no `auto`, `typeof` or type parameter in it (`finder::find(s, c) == .some`): the compiler
// cannot type the call until its instance is written, but the struct's name follows from the written result. False
// when the operand is no such call, or the variant is not one of the language's own.
static bool smith_lang_enum_generic_constant(SmithLang *lang, const SmithHost *host, const SmithToken *operand,
                                             size_t length, const SmithToken *variant, char *out, size_t capacity) {
    if (length < 3 || !smith_lang_punct(&operand[length - 1], ")") || operand[0].kind != SmithTokenKind_Identifier) {
        return false;
    }
    bool qualified = length > 4 && smith_lang_punct(&operand[1], "::");
    size_t open = qualified ? 3 : 1;
    if (!smith_lang_punct(&operand[open], "(")) return false;
    const SmithToken *member = qualified ? &operand[2] : &operand[0];
    const char *module = lang->module;
    size_t module_length = strlen(lang->module);
    if (qualified) {
        const SmithLangBinding *binding = smith_lang_binding_find(lang, operand[0].text, operand[0].length);
        if (binding == nullptr || binding->is_c || binding->missing) return false;
        module = smith_lang_str(binding->module);
        module_length = binding->module.length;
    }
    int found = smith_lang_generic_resolve(lang, host, module, module_length, member->text, member->length);
    if (found < 0) return false;
    const SmithLangGeneric *generic = &smith_lang_generics[found];
    // The head is recorded as lowered: `struct smith_enum_option_usize`, then the name. A result that depends on the
    // call's arguments is not one token (it holds `auto`, `typeof` or a type parameter) and is not read here.
    const SmithLangGenToken *first = &smith_lang_gt.tokens[generic->first];
    if (generic->name_index != 2 || !smith_lang_gt_is(first, SmithTokenKind_Identifier, "struct") ||
        first[1].kind != SmithTokenKind_Identifier) {
        return smith_lang_enum_derived_constant(generic, variant, out, capacity);
    }
    const char *tag = smith_lang_gt_str(&first[1]);
    size_t tag_length = first[1].length;
    bool is_option = tag_length > 18 && strncmp(tag, "smith_enum_option_", 18) == 0;
    bool is_result = tag_length > 18 && strncmp(tag, "smith_enum_result_", 18) == 0;
    bool known = is_result ? smith_lang_equals(variant->text, variant->length, "ok") ||
                                 smith_lang_equals(variant->text, variant->length, "err")
                           : is_option && (smith_lang_equals(variant->text, variant->length, "some") ||
                                           smith_lang_equals(variant->text, variant->length, "none"));
    if (!known) return false;
    int written = snprintf(out, capacity, "%.*s_tag_%.*s", (int)tag_length, tag, (int)variant->length, variant->text);
    return written > 0 && (size_t)written < capacity;
}

// The definition of the struct of one call, ready for the queue (`struct tag {...} __attribute__(...);`).
static size_t smith_lang_eshape_define(SmithLang *lang, const SmithHost *host, const SmithToken *like,
                                       const SmithLangEnumShape *shape, const SmithLangEnumCall *call, const char *tag,
                                       SmithToken *made, size_t capacity) {
    static SmithToken span[smith_lang_queue_max];
    size_t used = 0;
    for (size_t p = 0; p < shape->param_count; p++) { // a slice argument's struct, when no declaration defined it
        if (call->slice_elems[p] == 0 || smith_lang_slice_defined(lang, call->slice_tag[p])) continue;
        SmithLangSliceDims dims = smith_lang_slice_dims_one();
        size_t before = used;
        used = smith_lang_slice_define(lang, host, made, used, capacity - 2, call->slice_tag[p], call->slice_elem[p],
                                       call->slice_elems[p], &dims, like);
        if (used == before || used + 2 >= capacity) return 0;
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ";", 1, like, false);
    }
    size_t defined_slices = used;
    used = 0;
    span[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, "enum", 4, like, false);
    span[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "{", 1, like, true);
    for (size_t at = 0; at < shape->token_count; at++) {
        const SmithToken *token = &shape->tokens[at];
        size_t param = shape->param_count;
        for (size_t p = 0; p < shape->param_count; p++) {
            if (smith_lang_equals(token->text, token->length, shape->params[p])) param = p;
        }
        if (param == shape->param_count) {
            if (used + 3 >= smith_lang_queue_max) return 0;
            span[used] = *token;
            span[used].synthetic = true;
            span[used++].copy = true;
            continue;
        }
        for (size_t i = call->begin[param]; i < call->end[param]; i++) { // the argument's tokens
            if (used + 3 >= smith_lang_queue_max) return 0;
            span[used] = call->tokens[i];
            span[used].synthetic = true;
            span[used++].copy = true;
        }
    }
    size_t close = used;
    span[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "}", 1, like, true);
    SmithToken name = smith_lang_tuple_made(SmithTokenKind_Identifier, tag, strlen(tag), like, true);
    size_t count = smith_lang_enum_build(lang, host, like, span, 1, close, &name, made + defined_slices,
                                         capacity - 2 - defined_slices);
    if (count == 0) return 0;
    count += defined_slices;
    made[count++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ";", 1, like, false);
    return count;
}

static SmithLangEnumDefined *smith_lang_eshape_note_defined(const char *tag) {
    SmithLangEnumDefined *entry = nullptr;
    for (size_t i = 0; i < smith_lang_edefined_count && entry == nullptr; i++) { // a closed one of the tag is reused
        if (smith_lang_edefined[i].closed && strcmp(smith_lang_edefined[i].tag, tag) == 0)
            entry = &smith_lang_edefined[i];
    }
    if (entry == nullptr) {
        if (smith_lang_edefined_count >= smith_lang_eshape_defined_max) return nullptr;
        entry = &smith_lang_edefined[smith_lang_edefined_count++];
    }
    snprintf(entry->tag, sizeof entry->tag, "%s", tag);
    entry->depth = smith_lang_eshape_depth;
    entry->header = false;
    entry->made = false;
    entry->closed = false;
    return entry;
}

// The types of a variant as a C header spells them: the sized names as the standard ones, a module's name with its
// prefix, the rest as written.
static bool smith_lang_eshape_words_header = true; // the words for a header (else for the compiler, no header needed)

static size_t smith_lang_eshape_words(SmithLang *lang, const SmithToken *tokens, size_t count, char *out,
                                      size_t capacity) {
    size_t used = 0;
    for (size_t at = 0; at < count && used + smith_lang_name_max + 2 < capacity; at++) {
        const SmithToken *token = &tokens[at];
        char word[smith_lang_name_max];
        size_t taken = 1;
        const SmithLangSized *sized =
            token->kind == SmithTokenKind_Identifier ? smith_lang_find_sized(token->text, token->length) : nullptr;
        if (sized != nullptr) {
            snprintf(word, sizeof word, "%s", smith_lang_eshape_words_header ? sized->header : sized->standard);
        } else if (token->kind == SmithTokenKind_Identifier &&
                   smith_lang_word(token->text, token->length) == SmithLangWord_Other) {
            taken = smith_lang_tuple_resolve(lang, tokens, at, count, word, sizeof word);
        } else {
            snprintf(word, sizeof word, "%.*s", (int)(token->length < sizeof word ? token->length : sizeof word - 1),
                     token->text);
        }
        used += (size_t)snprintf(out + used, capacity - used, " %s", word);
        at += taken - 1;
    }
    return used;
}

// The definition for the module's header, as C reads it, guarded by a macro of the struct's name so that two headers
// that define the same struct (`option(usize)` in two modules) are included together: one type.
static bool smith_lang_eshape_header_text(SmithLang *lang, const SmithLangEnumShape *shape,
                                          const SmithLangEnumCall *call, const char *tag, char *out, size_t capacity) {
    char guard[256]; // the guard is the tag in capitals: a macro's name is UPPER_CASE
    size_t guard_length = 0;
    for (; tag[guard_length] != '\0' && guard_length + 1 < sizeof guard; guard_length++) {
        char letter = tag[guard_length];
        guard[guard_length] = letter >= 'a' && letter <= 'z' ? (char)(letter - 'a' + 'A') : letter;
    }
    guard[guard_length] = '\0';
    size_t used =
        (size_t)snprintf(out, capacity, "#ifndef SMITH_ENUM_%s\n#define SMITH_ENUM_%s\nstruct %s { enum %s_tag {",
                         guard, guard, tag, tag);
    SmithToken variant[smith_lang_eshape_tokens_max + smith_lang_eshape_params_max * smith_lang_eshape_tokens_max];
    char members[2048] = "";
    size_t counted = 0;
    size_t length = 0;
    for (size_t at = 0; at <= shape->token_count; at++) { // each variant: its tokens up to the `;`
        bool end = at == shape->token_count || smith_lang_punct(&shape->tokens[at], ";");
        if (!end) {
            const SmithToken *token = &shape->tokens[at];
            size_t param = shape->param_count;
            for (size_t p = 0; p < shape->param_count; p++) {
                if (smith_lang_equals(token->text, token->length, shape->params[p])) param = p;
            }
            if (param == shape->param_count) {
                if (length + 1 < sizeof variant / sizeof variant[0]) variant[length++] = *token;
                continue;
            }
            for (size_t i = call->begin[param]; i < call->end[param]; i++)
                if (length + 1 < sizeof variant / sizeof variant[0]) variant[length++] = call->tokens[i];
            continue;
        }
        if (length == 0) continue;
        const SmithToken *name = &variant[length - 1];
        used += (size_t)snprintf(out + used, capacity - used, "%s %s_tag_%.*s", counted > 0 ? "," : "", tag,
                                 (int)name->length, name->text);
        if (length > 1) {
            size_t have = strlen(members);
            have += smith_lang_eshape_words(lang, variant, length - 1, members + have, sizeof members - have);
            snprintf(members + have, sizeof members - have, " %.*s;", (int)name->length, name->text);
        }
        counted += 1;
        length = 0;
    }
    used += (size_t)snprintf(out + used, capacity - used,
                             " } tag; union {%s }; } __attribute__((annotate(\"smith::enum\")));\n#endif\n", members);
    return used < capacity;
}

// The definition goes in the header too, in front of the exported declaration that names the struct.
static void smith_lang_eshape_to_header(SmithLang *lang, const SmithToken *like, const SmithLangEnumShape *shape,
                                        const SmithLangEnumCall *call, const char *tag) {
    static char text[4096];
    if (!smith_lang_eshape_header_text(lang, shape, call, tag, text, sizeof text)) return;
    SmithToken whole = smith_lang_tuple_made(SmithTokenKind_Identifier, smith_lang_tuple_keep(text, strlen(text)),
                                             strlen(text), like, false);
    whole.line_start = true;
    smith_lang_emit_header(lang, whole);
}

static bool smith_lang_eshape_start_now; // the token being read begins a statement or a declaration

// Whether the call whose `(` is at `open` has an argument that stands for a generic's type parameter (an identifier no
// scope knows, or a `typeof`), in a generic's head or body: it is recorded as written, and each instance writes
// `option(usize)` for it (smith_lang_eshape_instance_call).
static bool smith_lang_eshape_dependent(SmithLang *lang, const SmithHost *host, const SmithToken *first, size_t open) {
    // The head of a generic is not known to be one at its first token, so a declaration's start counts too.
    if (!lang->recording && !lang->decl.generic && !lang->generic_next && !smith_lang_eshape_start_now) return false;
    size_t close = smith_lang_eshape_close(lang, host, first, open, "(", ")");
    for (size_t at = open + 1; close != 0 && at < close; at++) {
        const SmithToken *token = smith_lang_eshape_at(lang, host, first, at);
        if (token->kind != SmithTokenKind_Identifier) continue;
        if (smith_lang_ident(token, "typeof") || smith_lang_ident(token, "__typeof__")) {
            size_t end = smith_lang_eshape_close(lang, host, first, at + 1, "(", ")");
            bool slice = end != 0 && end >= at + 6 &&
                         smith_lang_punct(smith_lang_eshape_at(lang, host, first, end - 1), "]") &&
                         smith_lang_punct(smith_lang_eshape_at(lang, host, first, end - 2), ":") &&
                         smith_lang_punct(smith_lang_eshape_at(lang, host, first, end - 3), "[");
            if (!slice) return true; // `typeof(T[:])` is a slice type: its parts decide, like `T[:]`
            continue;
        }
        const SmithToken *before = smith_lang_eshape_at(lang, host, first, at - 1);
        const SmithToken *after = smith_lang_eshape_at(lang, host, first, at + 1);
        if (smith_lang_word(token->text, token->length) != SmithLangWord_Other ||
            smith_lang_find_sized(token->text, token->length) != nullptr || smith_lang_punct(after, "::") ||
            smith_lang_punct(before, "::") || smith_lang_ident(before, "struct") || smith_lang_ident(before, "union") ||
            smith_lang_ident(before, "enum")) {
            continue;
        }
        char name[smith_lang_name_max];
        snprintf(name, sizeof name, "%.*s", (int)token->length, token->text);
        bool known = smith_lang_name_find(lang, name, token->length) != nullptr; // the module's own name
        if (!known &&
            (host->lookup == nullptr ||
             host->lookup(host->context, name, (uint32_t)token->length, SmithLookup_Type).kind == SmithLookup_None)) {
            return true;
        }
    }
    return false;
}

// ---- The call in a generic: `option(T)`, `option(typeof(x))`
//
// A generic records `option ( T )` as it is written. An instance reads the call from the generic's tokens, with its
// own types in place of T (and of `x` in `typeof(x)`, an `auto` parameter), and writes `struct
// smith_enum_option_usize`, the same struct a written `option(usize)` is.

// The instance's argument tokens for the type parameter `param` (or `x`'s type), as tokens of the lowering, `u8`
// being `u8` again.
static bool smith_lang_eshape_instance_type(SmithLangEnumCall *call, const SmithLangInstance *instance, size_t index) {
    uint32_t first = instance->arg_first[index];
    uint32_t count = instance->arg_count[index];
    for (uint32_t i = 0; i < count; i++) {
        if (call->count >= smith_lang_eshape_tokens_max) return false;
        SmithToken token = smith_lang_gt_token(&smith_lang_it, &smith_lang_it.tokens[first + i]);
        for (size_t k = 0; k < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; k++) { // `u8` was lowered
            if (smith_lang_equals(token.text, token.length, smith_lang_sized[k].standard)) {
                token.text = smith_lang_sized[k].name;
                token.length = (uint32_t)strlen(smith_lang_sized[k].name);
            }
        }
        token.text = smith_lang_tuple_keep(token.text, token.length);
        token.handle = smith_token_no_handle;
        call->tokens[call->count++] = token;
    }
    return true;
}

// Whether the tokens at `at` are `typeof ( xs [ a : b ] )` or `typeof ( ( * xs ) [ a : b ] )` with `xs` a parameter of
// the generic whose type is a slice (a pointer to one): the index of the closing `)`, else 0. `deref` says which.
static uint32_t smith_lang_eshape_cut_end(const SmithLangGenToken *token, uint32_t at, uint32_t end, bool *deref) {
    if (!(smith_lang_gt_is(token, SmithTokenKind_Identifier, "typeof") ||
          smith_lang_gt_is(token, SmithTokenKind_Identifier, "__typeof__")) ||
        at + 6 >= end || !smith_lang_gt_punct(token + 1, "(")) {
        return 0;
    }
    *deref = smith_lang_gt_punct(token + 2, "(") && smith_lang_gt_punct(token + 3, "*");
    uint32_t name = *deref ? 4U : 2U;
    uint32_t open = name + (*deref ? 2U : 1U);
    if (at + open >= end || token[name].kind != SmithTokenKind_Identifier || token[name].param == 0 ||
        (*deref && !smith_lang_gt_punct(token + name + 1, ")")) || !smith_lang_gt_punct(token + open, "[")) {
        return 0;
    }
    bool colon = false;
    int depth = 0;
    for (uint32_t i = open; at + i < end; i++) { // the subscript
        depth += smith_lang_gt_punct(token + i, "[") ? 1 : smith_lang_gt_punct(token + i, "]") ? -1 : 0;
        colon = colon || (depth == 1 && smith_lang_gt_punct(token + i, ":"));
        if (depth == 0) {
            return colon && smith_lang_gt_punct(token + i + 1, ")") ? at + i + 1 : 0;
        }
    }
    return 0;
}

// A token of an argument that names no parameter, as it is (`i32` was lowered: `int` again). False when the call is
// full.
static bool smith_lang_eshape_plain(SmithLangEnumCall *call, const SmithLangGenToken *token) {
    if (call->count >= smith_lang_eshape_tokens_max) return false;
    SmithToken plain = smith_lang_gt_token(&smith_lang_gt, token);
    for (size_t k = 0; k < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; k++) {
        if (smith_lang_equals(plain.text, plain.length, smith_lang_sized[k].standard)) {
            plain.text = smith_lang_sized[k].name;
            plain.length = (uint32_t)strlen(smith_lang_sized[k].name);
        }
    }
    plain.text = smith_lang_tuple_keep(plain.text, plain.length);
    plain.handle = smith_token_no_handle;
    call->tokens[call->count++] = plain;
    return true;
}

// `typeof(xs[a:b])` or `typeof((*xs)[a:b])` as an argument: the slice's struct is the argument (the pointer's star
// taken off). The index of the closing `)`, 0 when the tokens are no such cut, UINT32_MAX when the type is too long.
static uint32_t smith_lang_eshape_cut_type(SmithLangEnumCall *call, const SmithLangInstance *instance,
                                           const SmithLangGenToken *token, uint32_t at, uint32_t end) {
    bool deref = false;
    uint32_t cut = smith_lang_eshape_cut_end(token, at, end, &deref);
    if (cut == 0) return 0;
    const SmithLangGenToken *named = &smith_lang_gt.tokens[at + (deref ? 4U : 2U)];
    if (!smith_lang_eshape_instance_type(call, instance, (size_t)named->param - 1)) return UINT32_MAX;
    if (deref && call->count > 0) call->count -= 1; // the pointer's star
    return cut;
}

// The slice types that the type-function calls of the shape call being read computed (`option(slice_of(T, 1))`): the
// struct of each is defined before the enum struct that holds it.
static uint64_t smith_lang_eshape_slices[4];
static size_t smith_lang_eshape_slice_count;

// `slice_of(T, 1)` (a call of a function that returns `type`) as a whole argument of a shape call: the compiler is
// asked for the type it computes for the instance and its spelling is the argument's tokens. The index of the call's
// closing `)`, or 0 when the tokens are no such call (or no type), which the other readings then try.
static uint32_t smith_lang_eshape_type_call(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                            const SmithLangInstance *instance, uint32_t at, uint32_t end,
                                            SmithLangEnumCall *call) {
    const SmithLangGenToken *name = &smith_lang_gt.tokens[at];
    if (name->kind != SmithTokenKind_Identifier || name->param != 0 || name->special != SmithLangGen_Plain ||
        at + 3 >= end || !smith_lang_gt_punct(name + 1, "(") || smith_lang_cx_word(name, "typeof") ||
        smith_lang_cx_word(name, "__typeof__")) {
        return 0;
    }
    SmithToken written = smith_lang_gt_token(&smith_lang_gt, name);
    if (smith_lang_word(written.text, written.length) != SmithLangWord_Other ||
        smith_lang_find_sized(written.text, written.length) != nullptr) {
        return 0;
    }
    uint32_t stop = smith_lang_cx_skip_group(at + 1, end); // one past the call's `)`
    if (stop >= end || stop <= at + 2 ||
        !(smith_lang_gt_punct(&smith_lang_gt.tokens[stop], ",") ||
          smith_lang_gt_punct(&smith_lang_gt.tokens[stop], ")"))) {
        return 0;
    }
    char spelling[smith_lang_tuple_name_max];
    uint64_t handle = 0;
    SmithLangInstance *kept = &smith_lang_instances[instance - smith_lang_instances];
    if (smith_lang_type_call_ask(lang, host, generic, kept, at, stop, spelling, sizeof spelling, &handle) != nullptr ||
        call->count + strlen(spelling) >= smith_lang_eshape_tokens_max) {
        return 0;
    }
    if (smith_lang_ty_depth_of(handle) > 0 && smith_lang_eshape_slice_count < 4)
        smith_lang_eshape_slices[smith_lang_eshape_slice_count++] = handle;
    for (size_t used = 0; spelling[used] != 0;) { // words, and each other character by itself
        if (spelling[used] == ' ') {
            used += 1;
            continue;
        }
        size_t length = 1;
        bool word = isalnum((unsigned char)spelling[used]) || spelling[used] == '_';
        while (word && (isalnum((unsigned char)spelling[used + length]) || spelling[used + length] == '_'))
            length += 1;
        call->tokens[call->count++] = smith_lang_tuple_made(
            word ? SmithTokenKind_Identifier : SmithTokenKind_Punctuator, spelling + used, length, &written, true);
        used += length;
    }
    return stop - 1;
}

// The comma between two arguments, kept in the call (the spelling joins the arguments by it); the next argument begins.
static bool smith_lang_eshape_comma(SmithLangEnumCall *call, const SmithLangGenToken *token, size_t argument) {
    if (call->count >= smith_lang_eshape_tokens_max) return false;
    call->end[argument] = call->count;
    SmithToken comma = smith_lang_gt_token(&smith_lang_gt, token);
    comma.text = smith_lang_tuple_keep(comma.text, comma.length);
    comma.handle = smith_token_no_handle;
    call->tokens[call->count++] = comma;
    call->begin[argument + 1] = call->count;
    return true;
}

// The shape call at `at` of a generic's tokens, read for `instance`: how many tokens it takes (0 when the tokens are
// no call of a shape), its shape, arguments and struct. An error is said at `place`.
static uint32_t smith_lang_eshape_instance_call(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                                const SmithLangInstance *instance, uint32_t at, uint32_t end,
                                                uint32_t place, const SmithLangEnumShape **found,
                                                SmithLangEnumCall *call, char *tag, size_t capacity) {
    const SmithLangGenToken *name = &smith_lang_gt.tokens[at];
    if (smith_lang_eshape_count == 0 || instance == nullptr || at + 2 >= end || name->param != 0 ||
        name->special != SmithLangGen_Plain || name->kind != SmithTokenKind_Identifier ||
        !smith_lang_gt_punct(&smith_lang_gt.tokens[at + 1], "(")) {
        return 0;
    }
    SmithToken as_token = smith_lang_gt_token(&smith_lang_gt, name);
    const SmithLangEnumShape *shape = smith_lang_eshape_find(&as_token);
    if (shape == nullptr) return 0;
    call->count = 0;
    smith_lang_eshape_slice_count = 0;
    size_t argument = 0;
    call->begin[0] = 0;
    int depth = 0;
    uint32_t close = at + 2;
    for (; close < end; close++) {
        const SmithLangGenToken *token = &smith_lang_gt.tokens[close];
        if (depth == 0 && smith_lang_gt_punct(token, ")")) break;
        depth += smith_lang_gt_punct(token, "(") || smith_lang_gt_punct(token, "[");
        depth -= smith_lang_gt_punct(token, ")") || smith_lang_gt_punct(token, "]");
        if (depth == 0 && smith_lang_gt_punct(token, ",")) {
            if (argument + 1 >= shape->param_count || !smith_lang_eshape_comma(call, token, argument)) return 0;
            argument += 1;
            continue;
        }
        bool typeof_x = (smith_lang_gt_is(token, SmithTokenKind_Identifier, "typeof") ||
                         smith_lang_gt_is(token, SmithTokenKind_Identifier, "__typeof__")) &&
                        close + 3 < end && smith_lang_gt_punct(&smith_lang_gt.tokens[close + 1], "(") &&
                        smith_lang_gt_punct(&smith_lang_gt.tokens[close + 3], ")");
        int own = typeof_x ? smith_lang_param_type_of(generic, instance, (SmithLangSpan){close + 2, close + 3}) : -1;
        if (typeof_x && own >= 0) {
            if (!smith_lang_eshape_instance_type(call, instance, (size_t)own)) return 0;
            close += 3;
            continue;
        }
        uint32_t made = call->count == call->begin[argument]
                            ? smith_lang_eshape_type_call(lang, host, generic, instance, close, end, call)
                            : 0;
        if (made != 0) { // `slice_of(T, 1)`: the type the compiler computes
            close = made;
            continue;
        }
        uint32_t cut = smith_lang_eshape_cut_type(call, instance, token, close, end);
        if (cut == UINT32_MAX) return 0;
        if (cut != 0) { // `typeof(xs[a:b])`: the slice's struct
            close = cut;
            continue;
        }
        if (token->param != 0 && token->kind == SmithTokenKind_Identifier) {
            if (!smith_lang_eshape_instance_type(call, instance, (size_t)token->param - 1)) return 0;
            continue;
        }
        if (!smith_lang_eshape_plain(call, token)) return 0;
    }
    if (close >= end) return 0;
    call->end[argument] = call->count;
    if (argument + 1 != shape->param_count) return 0;
    if (!smith_lang_eshape_tag(lang, host, shape, call, tag, capacity, false)) {
        smith_lang_error(lang, host, place,
                         "an argument of `%s` in this instance is not a type name with stars and "
                         "qualifiers, for now (known gap)",
                         shape->name);
        return 0;
    }
    *found = shape;
    return close + 1 - at;
}

// The result type of an instance of `option(T) name(...)`: the instance's struct, when the head is a shape call.
static bool smith_lang_enum_result_shape(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                         const SmithLangInstance *instance, SmithLangEnumType *type, bool *is_shape) {
    *is_shape = false;
    if (instance == nullptr ||
        !smith_lang_gt_punct(&smith_lang_gt.tokens[generic->first + generic->name_index - 1], ")")) {
        return false;
    }
    const SmithLangEnumShape *shape = nullptr;
    static SmithLangEnumCall call;
    char tag[smith_lang_tuple_name_max];
    uint32_t first = generic->first + smith_lang_generic_skip_attributes(generic);
    uint32_t end = generic->first + generic->name_index;
    if (smith_lang_eshape_instance_call(lang, host, generic, instance, first, end, 0, &shape, &call, tag, sizeof tag) !=
        end - first) {
        return false;
    }
    *is_shape = true;
    char canonical[smith_lang_tuple_name_max + 8];
    snprintf(canonical, sizeof canonical, "struct %s", tag);
    return smith_lang_enum_type(host, canonical, type);
}

// Writes the definition of the struct of a call at file scope where an instance is written (the call's own struct,
// when no scope has it). The words are C as the compiler reads it.
static void smith_lang_eshape_put_definition(SmithLang *lang, const SmithHost *host, const SmithLangEnumShape *shape,
                                             const SmithLangEnumCall *call, const char *tag, uint32_t place,
                                             uint32_t instance_id) {
    static char text[4096];
    smith_lang_eshape_words_header = false;
    bool good = smith_lang_eshape_header_text(lang, shape, call, tag, text, sizeof text);
    smith_lang_eshape_words_header = true;
    if (!good) {
        smith_lang_error(lang, host, place, "this enum type is too long to define");
        return;
    }
    char *body = strchr(text, '{'); // after `#ifndef` and `#define`: from `struct tag {`
    char *start = strstr(text, "struct ");
    char *stop = strstr(text, "\n#endif");
    if (body == nullptr || start == nullptr || stop == nullptr) return;
    *stop = 0;
    smith_lang_gen_note_break(lang);
    for (char *at = start; *at != 0;) {
        if (isspace((unsigned char)*at)) {
            at += 1;
        } else if (isalnum((unsigned char)*at) || *at == '_') {
            char *word = at;
            while (isalnum((unsigned char)*at) || *at == '_')
                at += 1;
            char saved = *at;
            *at = 0;
            size_t length = strlen(word);
            bool macro = strncmp(word, "__", 2) == 0 && length > 8 && strcmp(word + length - 7, "_TYPE__") == 0;
            smith_lang_gen_put_expanding(
                lang, host, isdigit((unsigned char)word[0]) ? SmithTokenKind_Number : SmithTokenKind_Identifier, word,
                place, instance_id, macro);
            *at = saved;
        } else if (*at == '"') {
            char *end = strchr(at + 1, '"');
            if (end == nullptr) return;
            char saved = end[1];
            end[1] = 0;
            smith_lang_gen_put(lang, host, SmithTokenKind_String, at, place, instance_id);
            end[1] = saved;
            at = end + 1;
        } else {
            char one[2] = {*at, 0};
            smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, one, place, instance_id);
            at += 1;
        }
    }
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ";", place, instance_id);
}

// The definitions of the slice structs that the last shape call read computed, in front of the enum struct that holds
// them.
static void smith_lang_eshape_put_slices(SmithLang *lang, const SmithHost *host, uint32_t place, uint32_t instance_id) {
    for (size_t i = 0; i < smith_lang_eshape_slice_count; i++) { // at most 4
        smith_lang_gen_note_break(lang);
        smith_lang_gen_put_slice_result(lang, host, smith_lang_eshape_slices[i], place, instance_id);
        smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ";", place, instance_id);
    }
}

// Before an instance's definition is written at file scope: the struct of each shape call in the generic's tokens that
// no scope defines.
static void smith_lang_instance_define_enums(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                             const SmithLangInstance *instance, uint32_t place, uint32_t instance_id) {
    uint32_t end = generic->first + generic->count;
    for (uint32_t at = generic->first; at < end; at++) {
        const SmithLangEnumShape *shape = nullptr;
        static SmithLangEnumCall call;
        char tag[smith_lang_tuple_name_max];
        uint32_t length = smith_lang_eshape_instance_call(lang, host, generic, instance, at, end, place, &shape, &call,
                                                          tag, sizeof tag);
        if (length == 0) continue;
        if (!smith_lang_eshape_defined(host, tag)) {
            smith_lang_eshape_put_slices(lang, host, place, instance_id);
            smith_lang_eshape_put_definition(lang, host, shape, &call, tag, place, instance_id);
            smith_lang_eshape_note_defined(tag);
        }
        at += length - 1;
    }
}

// A token of an instance's head or body: when it begins a shape call, `struct tag` is written (an error when no scope
// has defined the struct, which a call site cannot do for itself). The number of generic tokens taken, or 0.
static uint32_t smith_lang_eshape_put_call(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                           const SmithLangInstance *instance, uint32_t at, uint32_t end, uint32_t place,
                                           uint32_t instance_id, bool define) {
    const SmithLangEnumShape *shape = nullptr;
    static SmithLangEnumCall call;
    char tag[smith_lang_tuple_name_max];
    uint32_t length =
        smith_lang_eshape_instance_call(lang, host, generic, instance, at, end, place, &shape, &call, tag, sizeof tag);
    if (length == 0) return 0;
    if (!smith_lang_eshape_defined(host, tag)) {
        if (define) {
            smith_lang_eshape_put_slices(lang, host, place, instance_id);
            smith_lang_eshape_put_definition(lang, host, shape, &call, tag, place, instance_id);
            smith_lang_eshape_note_defined(tag);
        } else {
            smith_lang_error(lang, host, place,
                             "the struct of this instance's `%s(...)` is not defined where the call is (known gap): "
                             "name the type at file scope first, for example `typedef %s(...) Name;`",
                             shape->name, shape->name);
        }
    }
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "struct", place, instance_id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, tag, place, instance_id);
    return length;
}

// ---- The shape in a module's header (issue #198, 2c)
//
// `export type option(type T) { return enum { T some; none; }; }` adds a line to the module's header,
// `// smith-eshape: option(T) { T some ; none ; }` (the tokens separated by spaces), which an importer reads when it
// reads the interface (smith_lang_eshape_import) and keeps as a shape of its own whose `module` is the import's name:
// `opt::option(usize)` is the struct `smith_enum_option_usize`, the one a write of `option(usize)` in the module is.

static size_t smith_lang_eshape_lines(const SmithLang *lang, char *out, size_t capacity, size_t used) {
    (void)lang;
    for (size_t i = 0; i < smith_lang_eshape_count; i++) { // at most 16
        const SmithLangEnumShape *shape = &smith_lang_eshapes[i];
        if (!shape->exported || shape->module[0] != 0) continue;
        used = smith_lang_append(out, capacity, used, "// smith-eshape: %s(", shape->name);
        for (size_t p = 0; p < shape->param_count; p++)
            used = smith_lang_append(out, capacity, used, p > 0 ? ",%s" : "%s", shape->params[p]);
        used = smith_lang_append(out, capacity, used, ") {");
        for (size_t t = 0; t < shape->token_count; t++)
            used = smith_lang_append(out, capacity, used, " %.*s", (int)shape->tokens[t].length, shape->tokens[t].text);
        used = smith_lang_append(out, capacity, used, " }\n");
    }
    return used;
}

// The shapes a header's `// smith-eshape:` lines hold, kept for the import named `bind`.
static void smith_lang_eshape_import(const char *text, uint32_t length, const char *bind, size_t bind_length) {
    static const char marker[] = "// smith-eshape: ";
    for (const char *at = memmem(text, length, marker, sizeof marker - 1); at != nullptr;
         at = memmem(at + 1, (size_t)(text + length - at - 1), marker, sizeof marker - 1)) { // one pass per line
        const char *line = at + sizeof marker - 1;
        const char *end = memchr(line, '\n', (size_t)(text + length - line));
        if (end == nullptr) end = text + length;
        if (smith_lang_eshape_count >= smith_lang_eshape_max || bind_length >= sizeof smith_lang_eshapes[0].module)
            return;
        SmithLangEnumShape *shape = &smith_lang_eshapes[smith_lang_eshape_count];
        memset(shape, 0, sizeof *shape);
        memcpy(shape->module, bind, bind_length);
        shape->exported = true;
        const char *cursor = line;
        size_t name = strcspn(cursor, "(");
        if (cursor + name >= end || name >= sizeof shape->name) continue;
        memcpy(shape->name, cursor, name);
        cursor += name + 1;
        for (; cursor < end && *cursor != ')'; cursor += *cursor == ',' ? 1 : 0) { // the parameters
            size_t each = strcspn(cursor, ",)");
            if (each >= sizeof shape->params[0] || shape->param_count >= smith_lang_eshape_params_max) break;
            memcpy(shape->params[shape->param_count++], cursor, each);
            cursor += each;
        }
        cursor += 4; // `) {` and the space
        bool duplicate = false;
        for (size_t i = 0; i < smith_lang_eshape_count; i++)
            duplicate = duplicate || (strcmp(smith_lang_eshapes[i].name, shape->name) == 0 &&
                                      strcmp(smith_lang_eshapes[i].module, shape->module) == 0);
        while (!duplicate && cursor < end) { // the variants' tokens
            while (cursor < end && *cursor == ' ')
                cursor += 1;
            size_t word = 0;
            while (cursor + word < end && cursor[word] != ' ')
                word += 1;
            if (word == 0 || (word == 1 && *cursor == '}' && cursor + 1 >= end)) break;
            if (shape->token_count >= smith_lang_eshape_tokens_max) break;
            SmithToken token = {};
            token.kind = isalpha((unsigned char)*cursor) || *cursor == '_' ? SmithTokenKind_Identifier
                         : isdigit((unsigned char)*cursor)                 ? SmithTokenKind_Number
                                                                           : SmithTokenKind_Punctuator;
            token.text = smith_lang_eshape_keep(cursor, word);
            token.length = (uint32_t)word;
            token.handle = smith_token_no_handle;
            token.space_before = true;
            if (token.text == nullptr) break;
            shape->tokens[shape->token_count++] = token;
            cursor += word;
        }
        if (!duplicate) smith_lang_eshape_count += 1;
    }
}

// `option` and `result` are in scope in every module, with no import: their shapes are the ones the library modules
// `smith::option` and `smith::result` (lib/smith/option.smith, result.smith) export. Those headers are not in git, so
// the two lines the lowering needs of them are written here; `tests/headers_test.smith` checks that each module's
// generated header has exactly its line.
static constexpr char smith_option_header[] = "// smith-eshape: option(T) { T some ; none ; }\n";
static constexpr char smith_result_header[] = "// smith-eshape: result(T,E) { T ok ; E error ; }\n";

static void smith_lang_eshape_builtins(SmithLang *lang) {
    if (smith_lang_eshape_builtins_done || !lang->is_module) return;
    smith_lang_eshape_builtins_done = true;
    size_t before = smith_lang_eshape_count;
    smith_lang_eshape_import(smith_option_header, sizeof smith_option_header - 1, "", 0);
    smith_lang_eshape_import(smith_result_header, sizeof smith_result_header - 1, "", 0);
    for (size_t i = before; i < smith_lang_eshape_count; i++) {
        smith_lang_eshapes[i].builtin = true;
        smith_lang_eshapes[i].exported = false; // the library's header is not this module's to write
    }
}

// Whether the token names a type function that returns an enum.
static bool smith_lang_eshape_named(const SmithToken *name) {
    return smith_lang_eshape_count > 0 && name->kind == SmithTokenKind_Identifier &&
           smith_lang_eshape_find(name) != nullptr;
}

// Whether the token before a name is a type, so that the name is a declarator (`bool option(...)`, `T *option`): a
// shape is written where a type is, after a keyword (`typedef`, `static`, `return`), `export`, `(`, `,` or nothing.
static bool smith_lang_eshape_declares(const SmithToken *before) {
    if (before == nullptr) return false;
    if (smith_lang_punct(before, "*")) return true;
    return before->kind == SmithTokenKind_Identifier && !smith_lang_ident(before, "export") &&
           (smith_lang_word(before->text, before->length) == SmithLangWord_Other ||
            smith_lang_word(before->text, before->length) == SmithLangWord_TypeKeyword);
}

// A use of a shape at `at`: `name (` of this file's own, or `module :: name (` of an import's exported one (not a
// member): its shape, else null, and `*open`, the index of the `(`.
static const SmithLangEnumShape *smith_lang_eshape_use(SmithLang *lang, const SmithHost *host, const SmithToken *first,
                                                       size_t at, const SmithToken *before, size_t *open) {
    const SmithToken *name = smith_lang_eshape_at(lang, host, first, at);
    if (before != nullptr && (smith_lang_punct(before, ".") || smith_lang_punct(before, "->") ||
                              smith_lang_punct(before, "::") || smith_lang_ident(before, "struct"))) {
        return nullptr;
    }
    const SmithLangEnumShape *shape = smith_lang_eshape_find(name);
    if (shape != nullptr && smith_lang_eshape_declares(before)) return nullptr; // `bool option(`: a function's name
    if (shape != nullptr && shape->module[0] == 0 &&
        smith_lang_punct(smith_lang_eshape_at(lang, host, first, at + 1), "(")) {
        *open = at + 1;
        if (shape->builtin) { // the file uses a type function of the language: its header says it has enums
            lang->tuples_seen = true;
            lang->slices_seen = true;
            lang->enums_seen = true;
        }
        return shape;
    }
    if (name->kind != SmithTokenKind_Identifier || smith_lang_eshape_count == 0 ||
        !smith_lang_punct(smith_lang_eshape_at(lang, host, first, at + 1), "::")) {
        return nullptr;
    }
    const SmithToken *member = smith_lang_eshape_at(lang, host, first, at + 2);
    if (member->kind != SmithTokenKind_Identifier ||
        !smith_lang_punct(smith_lang_eshape_at(lang, host, first, at + 3), "(")) {
        return nullptr;
    }
    for (size_t i = 0; i < smith_lang_eshape_count; i++) { // `opt::option(`
        const SmithLangEnumShape *each = &smith_lang_eshapes[i];
        if (each->module[0] != 0 && smith_lang_equals(name->text, name->length, each->module) &&
            smith_lang_equals(member->text, member->length, each->name)) {
            *open = at + 3;
            return each;
        }
    }
    return nullptr;
}

// At the first token of a declaration or statement: the structs its enum type functions' calls name that no scope
// defines are defined in front of it. True when the queue was changed (the token is queued again, after them).
static bool smith_lang_eshape_define_ahead(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    static SmithToken made[smith_lang_queue_max];
    size_t used = 0;
    int depth = 0;
    const SmithToken *before =
        lang->prev_member ? &(SmithToken){.kind = SmithTokenKind_Punctuator, .text = ".", .length = 1} : nullptr;
    // Everything read ahead is lowered in one batch before the parser reads any of it, so a name the parser declares
    // on the way (`for (u32 i = 0; i < n; i++) f(i);`, a variable named like a C function) is not yet declared. The
    // read ends where the head does (a `for`'s clauses are statements of their own), and the body begins another.
    bool control = smith_lang_ident(token, "if") || smith_lang_ident(token, "for") ||
                   smith_lang_ident(token, "while") || smith_lang_ident(token, "switch");
    bool bodied = control || smith_lang_ident(token, "else") || smith_lang_ident(token, "do");
    for (size_t at = 0; at < smith_lang_eshape_scan_max; at++) {
        const SmithToken *each = smith_lang_eshape_at(lang, host, token, at);
        if (each->kind == SmithTokenKind_Eof || each->foreign) break;
        if (bodied && at == 1 && !control) break; // `else` and `do` are followed by a statement of its own
        depth += smith_lang_punct(each, "(") || smith_lang_punct(each, "[");
        depth -= smith_lang_punct(each, ")") || smith_lang_punct(each, "]");
        if (depth < 0 || (control && depth == 0 && smith_lang_punct(each, ")"))) break; // the end of the head
        if (control && depth == 1 && smith_lang_punct(each, ";")) break; // a clause of `for (...)`: the next begins one
        if (depth <= 0 && (smith_lang_punct(each, ";") || smith_lang_punct(each, "{") || smith_lang_punct(each, "}"))) {
            break; // the end of the head or the statement, or of the block it is in
        }
        size_t open = 0;
        const SmithLangEnumShape *shape = smith_lang_eshape_use(lang, host, token, at, before, &open);
        before = each;
        if (shape == nullptr || smith_lang_eshape_dependent(lang, host, token, open)) continue;
        SmithLangEnumCall call;
        char tag[smith_lang_tuple_name_max];
        if (!smith_lang_eshape_call(lang, host, token, open, shape, &call, false) ||
            !smith_lang_eshape_tag(lang, host, shape, &call, tag, sizeof tag, false)) {
            return false; // said where the call is replaced
        }
        at += call.close - at;
        SmithLangEnumDefined *entry = smith_lang_eshape_defined(host, tag) ? smith_lang_eshape_entry(tag) : nullptr;
        if (entry == nullptr) { // in a block the struct is defined there, which C23 makes the file-scope one
            size_t added = smith_lang_eshape_define(lang, host, token, shape, &call, tag, made + used,
                                                    smith_lang_queue_max - used - 2);
            if (added == 0) {
                smith_lang_error(lang, host, each->location, "this enum type is too long to define");
                return false;
            }
            used += added;
            entry = smith_lang_eshape_note_defined(tag);
            if (entry != nullptr) entry->made = smith_lang_eshape_depth > 0;
        }
        if (entry != nullptr && !entry->header && smith_lang_ident(token, "export")) {
            smith_lang_eshape_to_header(lang, token, shape, &call, tag); // the exported declaration needs it there
            entry->header = true;
        }
    }
    if (used == 0 || used + 1 >= smith_lang_queue_max) return false;
    made[used] = *token;
    return smith_lang_tuple_replace(lang, 0, made, used + 1);
}

// The use of a shape at `token`: `option ( usize )` is written `struct smith_enum_option_usize`.
static bool smith_lang_eshape_rewrite(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                      const SmithLangEnumShape *shape, size_t open) {
    SmithLangEnumCall call;
    char tag[smith_lang_tuple_name_max];
    SmithToken made[2];
    if (!smith_lang_eshape_call(lang, host, token, open, shape, &call, true) ||
        !smith_lang_eshape_tag(lang, host, shape, &call, tag, sizeof tag, true)) {
        size_t close = smith_lang_eshape_close(lang, host, token, open, "(", ")");
        if (close == 0) return false;
        made[0] = smith_lang_tuple_made(SmithTokenKind_Identifier, "int", 3, token, token->space_before); // so that the
        return smith_lang_tuple_replace(lang, close, made, 1); // declaration still reads, and says no more
    }
    if (!smith_lang_eshape_defined(host, tag)) {
        char written[smith_lang_tuple_name_max];
        size_t used = (size_t)snprintf(written, sizeof written, "%s(", shape->name);
        for (size_t i = 0; i < call.count && used + 48 < sizeof written; i++)
            used += (size_t)snprintf(written + used, sizeof written - used, "%s%.*s",
                                     i > 0 && !smith_lang_punct(&call.tokens[i], ",") ? " " : "",
                                     (int)call.tokens[i].length, call.tokens[i].text);
        snprintf(written + used, sizeof written - used, ")");
        smith_lang_error(lang, host, token->location,
                         "`%s` is first named where no declaration or statement begins, and the struct it stands for "
                         "cannot be defined in front of it yet (known gap): name it first in a declaration, for "
                         "example `typedef %s Name;`",
                         written, written);
    }
    made[0] = smith_lang_tuple_made(SmithTokenKind_Identifier, "struct", 6, token, token->space_before);
    made[0].line_start = token->line_start; // a statement that begins with it is at the start of its line
    made[1] = smith_lang_tuple_made(SmithTokenKind_Identifier, smith_lang_tuple_keep(tag, strlen(tag)), strlen(tag),
                                    token, true);
    return smith_lang_tuple_replace(lang, call.close, made, 2);
}

// The head of an `if`, `for`, `while` or `switch` and the `else` and `do` before a body: the token after each begins a
// statement of its own, which `smith_lang_eshape_define_ahead` reads (it does not read across them).
static void smith_lang_eshape_track_head(const SmithToken *token) {
    if (token->foreign || token->synthetic) return;
    if (token->kind == SmithTokenKind_Identifier) {
        if (smith_lang_ident(token, "else") || smith_lang_ident(token, "do")) smith_lang_eshape_start = true;
        if (smith_lang_ident(token, "if") || smith_lang_ident(token, "for") || smith_lang_ident(token, "while") ||
            smith_lang_ident(token, "switch")) {
            smith_lang_eshape_head = 1;
        }
    } else if (smith_lang_eshape_head > 0 && smith_lang_punct(token, "(")) {
        smith_lang_eshape_head += 1;
    } else if (smith_lang_eshape_head > 1 && smith_lang_punct(token, ")")) {
        smith_lang_eshape_head -= 1;
        if (smith_lang_eshape_head == 1) {
            smith_lang_eshape_head = 0;
            smith_lang_eshape_start = true;
        }
    }
}

// A token of a file that has type functions returning enums, before any other reading of it: the declaration of one,
// the definitions a declaration needs and a use (`option(usize)` is `struct smith_enum_option_usize`). True when the
// token was consumed.
static bool smith_lang_enum_function(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if ((host->maybe_shape || host->expr_type == nullptr) && !smith_lang_eshape_builtins_done)
        smith_lang_eshape_builtins(lang); // the raw passes have no text to ask
    bool start = smith_lang_eshape_start;
    smith_lang_eshape_start_now = start;
    smith_lang_eshape_start = start && token->kind == SmithTokenKind_Identifier && smith_lang_ident(token, "export");
    if (token->kind == SmithTokenKind_Punctuator && token->length == 1) {
        char c = token->text[0];
        smith_lang_eshape_start = c == ';' || c == '{' || c == '}';
        if (!token->synthetic && c == '{') smith_lang_eshape_depth += 1;
        if (!token->synthetic && c == '}') {
            if (smith_lang_eshape_depth > 0) smith_lang_eshape_depth -= 1;
            size_t kept = 0;
            for (size_t i = 0; i < smith_lang_edefined_count; i++) { // a definition ends with its block
                SmithLangEnumDefined *each = &smith_lang_edefined[i];
                if (each->depth > smith_lang_eshape_depth && each->made) each->closed = true;
                if (each->depth <= smith_lang_eshape_depth || each->closed) smith_lang_edefined[kept++] = *each;
            }
            smith_lang_edefined_count = kept;
        }
    }
    smith_lang_eshape_track_head(token);
    if (token->foreign || lang->in_attribute || !lang->is_module || token->synthetic) return false;
    bool recording =
        lang->recording; // a generic's body: its uses are written as they are in any body, nothing is defined
    if (recording) smith_lang_enum_generic_dot(lang, host, token);
    if (!recording && start && smith_lang_eshape_depth == 0 && smith_lang_ident(token, "type") &&
        smith_lang_eshape_declare(lang, host, token)) {
        return true;
    }
    if (smith_lang_eshape_count == 0) return false;
    if (start && (!recording || smith_lang_eshape_depth > 0) && smith_lang_eshape_define_ahead(lang, host, token))
        return true; // (a generic's body records the definition with its statement: each instance has it)
    size_t open = 0;
    const SmithLangEnumShape *shape =
        lang->prev_member || lang->typeish ? nullptr : smith_lang_eshape_use(lang, host, token, 0, nullptr, &open);
    // (`lang->typeish`: the tokens before end a type, so the name is a declarator: `bool option(...)`)
    if (shape != nullptr && smith_lang_eshape_dependent(lang, host, token, open)) {
        if ((lang->decl.exported || lang->pending_export) && !shape->exported && !shape->builtin &&
            shape->module[0] == 0) {
            smith_lang_error(lang, host, token->location,
                             "an exported generic cannot name `%s(...)` of a generic's own types unless the type "
                             "function is exported too: write `export type %s(...)`; an importer has no shape for it",
                             shape->name, shape->name);
        }
        return false;
    }
    if (shape == nullptr) return false;
    return smith_lang_eshape_rewrite(lang, host, token, shape, open);
}

// `_Pragma ( "text" )` as tokens, after `used` (the text is a string literal's spelling, quotes included).
static size_t smith_lang_enum_pragma(SmithToken *out, size_t used, const SmithToken *like, const char *literal) {
    out[used] = smith_lang_tuple_made(SmithTokenKind_Identifier, "_Pragma", 7, like, used > 0);
    out[used++].expand = true; // the compiler's own macro
    out[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "(", 1, like, false);
    out[used++] = smith_lang_tuple_made(SmithTokenKind_String, literal, strlen(literal), like, false);
    out[used++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ")", 1, like, false);
    return used;
}

// `switch` is being lowered: when the first label of its body is `case .name` the value switched is a payload enum: it
// becomes `switch ((value).tag)`, with -Wswitch-enum an error for the region of the switch (a variant that is not
// named is an error, `default:` or not). True when the token was replaced.
static bool smith_lang_enum_switch(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(") || host->expr_type == nullptr) return false;
    static SmithToken value[smith_lang_slice_chain_max];
    size_t length = 0;
    int depth = 0;
    size_t at = 0;
    for (; at < smith_lang_slice_chain_max - 2; at++) { // the condition, to its `)`
        const SmithToken *next = smith_lang_peek(lang, host, at, false);
        if (next->kind == SmithTokenKind_Eof || next->foreign) return false;
        depth += smith_lang_punct(next, "(");
        depth -= smith_lang_punct(next, ")");
        if (depth == 0) break;
        if (at > 0) value[length++] = *next;
    }
    if (!smith_lang_punct(smith_lang_peek(lang, host, at, false), ")") ||
        !smith_lang_punct(smith_lang_peek(lang, host, at + 1, false), "{")) {
        return false;
    }
    const SmithToken *label = smith_lang_peek(lang, host, at + 2, false);
    const SmithToken *dot = smith_lang_peek(lang, host, at + 3, false);
    if (!smith_lang_ident(label, "case") || !smith_lang_punct(dot, ".") ||
        smith_lang_enum_switch_count >= smith_lang_enum_switch_max) {
        return false;
    }
    SmithLangEnumType type;
    if (!smith_lang_enum_of(lang, host, value, length, &type)) return false;
    SmithLangEnumSwitch *entry = &smith_lang_enum_switches[smith_lang_enum_switch_count++];
    entry->depth = smith_lang_enum_braces;
    entry->type = type;
    static SmithToken made[smith_lang_slice_chain_max + 24];
    SmithLangSliceSpan spans[1] = {{value, length}};
    size_t used = smith_lang_enum_pragma(made, 0, token, "\"clang diagnostic push\"");
    used = smith_lang_enum_pragma(made, used, token, "\"clang diagnostic error \\\"-Wswitch-enum\\\"\"");
    used = smith_lang_slice_pattern(made, used, sizeof made / sizeof made[0], "switch ( ( @0 ) . tag )", spans, token);
    made[0].space_before = token->space_before;
    made[0].line_start = token->line_start;
    return smith_lang_tuple_replace(lang, at + 1, made, used);
}

// `case .name:` in a switch over a payload enum: `case C:`. The dot with no switch to look in is an error.
static bool smith_lang_enum_case(SmithLang *lang, const SmithHost *host) {
    const SmithToken *dot = smith_lang_peek(lang, host, 0, false);
    const SmithToken *name = smith_lang_peek(lang, host, 1, false);
    if (!smith_lang_punct(dot, ".") || name->kind != SmithTokenKind_Identifier) return false;
    if (smith_lang_enum_switch_count == 0) return false;
    const SmithLangEnumSwitch *entry = &smith_lang_enum_switches[smith_lang_enum_switch_count - 1];
    char constant[smith_lang_enum_name_max + 64];
    SmithToken variant = *name;
    SmithToken at = *dot;
    SmithToken made[2];
    if (!smith_lang_enum_variant(lang, host, &entry->type, &at, &variant, false, constant, sizeof constant)) {
        made[0] = smith_lang_tuple_made(SmithTokenKind_Number, "0", 1, dot, true);
    } else {
        made[0] = smith_lang_tuple_made(SmithTokenKind_Identifier, constant, strlen(constant), dot, true);
    }
    return smith_lang_tuple_replace(lang, 2, made, 1);
}

// A `{` or `}` seen in slice mode: the depth, and the end of a switch over a payload enum (the region of its
// -Wswitch-enum ends after its `}`).
static void smith_lang_enum_brace(SmithLang *lang, const SmithToken *token) {
    if (smith_lang_punct(token, "{")) {
        smith_lang_enum_braces += 1;
        return;
    }
    if (!smith_lang_punct(token, "}") || smith_lang_enum_braces == 0) return;
    smith_lang_enum_braces -= 1;
    if (smith_lang_enum_switch_count == 0 ||
        smith_lang_enum_switches[smith_lang_enum_switch_count - 1].depth != smith_lang_enum_braces) {
        return;
    }
    smith_lang_enum_switch_count -= 1;
    SmithToken made[4];
    size_t used = smith_lang_enum_pragma(made, 0, token, "\"clang diagnostic pop\"");
    smith_lang_tuple_replace(lang, 0, made, used);
}

// A `.name` that no context gave a type: an error naming the fix.
static void smith_lang_enum_untyped(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    const SmithToken *name = smith_lang_peek(lang, host, 0, false);
    const SmithToken *after = smith_lang_peek(lang, host, 1, false);
    SmithToken before = {};
    bool postfix = smith_lang_tuple_previous(lang, host, 1, &before) &&
                   ((before.kind == SmithTokenKind_Identifier && !smith_lang_slice_statement_word(&before)) ||
                    smith_lang_punct(&before, ")") || smith_lang_punct(&before, "]") || smith_lang_punct(&before, "}"));
    if (name->kind != SmithTokenKind_Identifier || smith_lang_punct(after, "=") || smith_lang_punct(after, ".") ||
        smith_lang_punct(after, "[") || postfix) {
        return;
    }
    smith_lang_error(lang, host, token->location,
                     "`.%.*s` needs a type to look in: write `Shape x = .%.*s;`, assign it to a variable, return it "
                     "from a function that names the type, or pass it where a parameter does",
                     (int)name->length, name->text, (int)name->length, name->text);
}

// A token of a file that has enums with payloads: the contexts, in the order a token can begin one. True when the token
// was replaced.
static bool smith_lang_enum_token(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (token->foreign || lang->recording || lang->in_attribute || host->expr_type == nullptr) return false;
    smith_lang_enum_brace(lang, token);
    if (token->kind == SmithTokenKind_Identifier && smith_lang_ident(token, "switch")) {
        return smith_lang_enum_switch(lang, host, token);
    }
    if (token->kind == SmithTokenKind_Identifier && smith_lang_ident(token, "case")) {
        smith_lang_enum_case(lang, host);
        return false;
    }
    if (smith_lang_punct(token, ".") && !token->synthetic) {
        if (smith_lang_enum_compare_first(lang, host, token)) return true;
        smith_lang_enum_untyped(lang, host, token);
        return false;
    }
    return !token->synthetic && smith_lang_enum_compare(lang, host, token);
}

// An argument `.v` or `{.v = ...}` for the parameter number `index` of the function whose type the compiler printed as
// `callee`, when that parameter is a payload enum: written as a compound literal. False when it is not one.
static bool smith_lang_enum_argument(SmithLang *lang, const SmithHost *host, const char *callee, size_t index,
                                     const SmithToken *tokens, size_t count, SmithLangSliceCur *cur) {
    SmithLangEnumType type;
    return smith_lang_enum_param(host, callee, index, &type) &&
           smith_lang_enum_value(lang, host, &type, tokens, count, true, cur);
}
