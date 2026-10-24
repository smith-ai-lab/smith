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
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, text, strlen(text), token, at > 0);
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
    return host->lookup(host->context, tag, (uint32_t)length, SmithLookup_Tag).kind == SmithLookup_Tag;
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
    return length > 0 && (size_t)length < capacity &&
           host->lookup(host->context, out, (uint32_t)length, SmithLookup_Value).kind == SmithLookup_Value;
}

// Whether the variant carries a payload: the struct has a member of its name.
static bool smith_lang_enum_carries(const SmithHost *host, const SmithLangEnumType *type, const SmithToken *variant) {
    char text[smith_lang_enum_name_max * 2 + 16];
    SmithExprType found = {};
    snprintf(text, sizeof text, "(\x1f%s\x1f)\x1f{\x1f}\x1f.\x1f%.*s", type->type, (int)variant->length, variant->text);
    for (char *at = text; *at != 0; at++) { // the type's words are tokens
        if (*at == ' ') *at = '\x1f';
    }
    return host->expr_type(host->context, text, (uint32_t)strlen(text), &found);
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

// A value for an enum of `type`: `.name` or `{.name = payload}` written as C's initializer (`literal` false, after a
// declaration's `=`) or compound literal (`literal` true). False when the tokens are neither (they are left alone).
static bool smith_lang_enum_value(SmithLang *lang, const SmithHost *host, const SmithLangEnumType *type,
                                  const SmithToken *tokens, size_t count, bool literal, SmithLangSliceCur *cur) {
    bool bare = smith_lang_enum_is_bare(tokens, count);
    if (!bare && !smith_lang_enum_is_braced(tokens, count)) return false;
    const SmithToken *dot = bare ? &tokens[0] : &tokens[1];
    const SmithToken *variant = bare ? &tokens[1] : &tokens[2];
    char constant[smith_lang_enum_name_max + 64];
    if (!smith_lang_enum_variant(lang, host, type, dot, variant, bare, constant, sizeof constant)) {
        char empty[smith_lang_enum_name_max + 16]; // a value of the type, so that nothing else is said
        snprintf(empty, sizeof empty, literal ? "( %s ) { }" : "{ }", type->type);
        SmithLangSliceCur piece = {};
        piece.count = smith_lang_slice_pattern(piece.tokens, 0, smith_lang_slice_cur_max, empty, nullptr, &tokens[0]);
        smith_lang_slice_add(cur, piece.tokens, piece.count);
        cur->changed = true;
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

// `T x = .v;`, `T x = {.v = ...};` (a declaration: the initializer is C's) and `x = .v;`, `x = {.v = ...};` (an
// assignment: a compound literal). Called at the `=`. True when the right side was written again.
static bool smith_lang_enum_equals(SmithLang *lang, const SmithHost *host) {
    const SmithToken *first = smith_lang_peek(lang, host, 0, false);
    if (!(smith_lang_punct(first, ".") || smith_lang_punct(first, "{"))) return false;
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, 0, right, smith_lang_slice_chain_max);
    if (!smith_lang_enum_is_bare(right, count) && !smith_lang_enum_is_braced(right, count)) return false;
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
    static SmithLangSliceCur converted;
    converted = (SmithLangSliceCur){};
    if (!smith_lang_enum_value(lang, host, &type, right, count, !declares, &converted) || converted.overflow)
        return false;
    return smith_lang_tuple_replace(lang, count, converted.tokens, converted.count);
}

// `return .v;` and `return {.v = ...};`: the type is the function's result.
static bool smith_lang_enum_return(SmithLang *lang, const SmithHost *host) {
    const SmithToken *first = smith_lang_peek(lang, host, 0, false);
    if (!(smith_lang_punct(first, ".") || smith_lang_punct(first, "{")) || host->return_type == nullptr) return false;
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, 0, right, smith_lang_slice_chain_max);
    if (!smith_lang_enum_is_bare(right, count) && !smith_lang_enum_is_braced(right, count)) return false;
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
                    smith_lang_punct(token, ".") || smith_lang_punct(token, "->") || depth > 0;
        if (!part || *length + 1 >= smith_lang_slice_chain_max) return false;
        depth += open;
        depth -= close;
        if (depth < 0) return false;
        operand[(*length)++] = *token;
    }
    return false;
}

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
    if (lang->prev_char != 0 && strchr(")]", lang->prev_char) != nullptr) return false;
    static SmithToken operand[smith_lang_slice_chain_max];
    size_t length = 0;
    size_t taken = 0;
    const SmithToken *op = nullptr;
    const SmithToken *variant = nullptr;
    if (!smith_lang_enum_comparison(lang, host, token, operand, &length, &taken, &op, &variant)) return false;
    SmithLangEnumType type;
    char constant[smith_lang_enum_name_max + 64];
    if (!smith_lang_enum_of(lang, host, operand, length, &type)) return false;
    SmithToken dot = *smith_lang_peek(lang, host, taken - 2, false);
    SmithToken name = *variant;
    SmithToken oper = *op;
    if (!smith_lang_enum_variant(lang, host, &type, &dot, &name, false, constant, sizeof constant)) return false;
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
                    smith_lang_punct(next, ".") || smith_lang_punct(next, "->") || depth > 0 ||
                    (length == 0 && smith_lang_punct(next, "*"));
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
static bool smith_lang_enum_result(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                   SmithLangEnumType *type) {
    if (generic == nullptr || generic->name_index < 1) return false;
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
    return smith_lang_enum_type(host, canonical, type);
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
    bool braced = smith_lang_gt_punct(&smith_lang_gt.tokens[at + 1], "{");
    uint32_t dot = at + (braced ? 2 : 1);
    const SmithLangGenToken *variant = &smith_lang_gt.tokens[dot + 1];
    bool bare = !braced && smith_lang_gt_punct(&smith_lang_gt.tokens[at + 3], ";");
    if (!smith_lang_gt_punct(&smith_lang_gt.tokens[dot], ".") || variant->kind != SmithTokenKind_Identifier ||
        (braced && !smith_lang_gt_punct(&smith_lang_gt.tokens[dot + 2], "=")) || (!braced && !bare)) {
        return false;
    }
    SmithLangEnumType type;
    if (!smith_lang_enum_result(lang, host, replay->generic, &type)) return false;
    SmithToken dot_token = smith_lang_gt_token(&smith_lang_gt, &smith_lang_gt.tokens[dot]);
    SmithToken name_token = smith_lang_gt_token(&smith_lang_gt, variant);
    char constant[smith_lang_enum_name_max + 64];
    uint32_t place = smith_lang_gen_place(host, replay->generic, keyword, replay->location);
    char words[smith_lang_enum_name_max * 3 + 96];
    if (!smith_lang_enum_variant(lang, host, &type, &dot_token, &name_token, bare, constant, sizeof constant)) {
        snprintf(words, sizeof words, "return ( %s ) { }", type.type); // a value of the type: nothing else is said
    } else if (bare) {
        snprintf(words, sizeof words, "return ( %s ) { . tag = %s }", type.type, constant);
    } else {
        snprintf(words, sizeof words, "return ( %s ) { . tag = %s , . %.*s =", type.type, constant,
                 (int)variant->length, smith_lang_gt_str(variant));
    }
    frame->pos = dot + (braced ? 3 : 2);
    SmithLangSlicePart parts[] = {{words, 0, 0}};
    smith_lang_slice_parts(replay, place, parts, 1);
    return true;
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
constexpr size_t smith_lang_eshape_tokens_max = 96;
constexpr size_t smith_lang_eshape_defined_max = 48;
constexpr size_t smith_lang_eshape_scan_max = 160;

typedef struct {
    char name[64];
    size_t param_count;
    char params[smith_lang_eshape_params_max][48];
    SmithToken tokens[smith_lang_eshape_tokens_max]; // between the braces of the enum
    size_t token_count;
} SmithLangEnumShape;

typedef struct {
    char tag[smith_lang_tuple_name_max];
    uint32_t depth; // the brace depth of the definition: it ends with that block
} SmithLangEnumDefined;

static SmithLangEnumShape smith_lang_eshapes[smith_lang_eshape_max];
static size_t smith_lang_eshape_count;
static char smith_lang_eshape_pool[8192];
static size_t smith_lang_eshape_pool_used;
static SmithLangEnumDefined smith_lang_edefined[smith_lang_eshape_defined_max];
static size_t smith_lang_edefined_count;
static uint32_t smith_lang_eshape_depth; // `{` minus `}` of the source's tokens
static bool smith_lang_eshape_start;     // the next token begins a statement or a declaration

// The tables are static like the lowering's, and a module's interface read in the middle of a file
// (smith_lang_keep_aside) would end with its own in them: they are set aside and put back with the lowering's.
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
        smith_lang_eshape_pool_used = smith_lang_saved_eshape_pool_used;
        smith_lang_edefined_count = smith_lang_saved_edefined_count;
        smith_lang_eshape_depth = smith_lang_saved_eshape_depth;
        smith_lang_eshape_start = smith_lang_saved_eshape_start;
        smith_lang_enum_switch_count = smith_lang_saved_enum_switch_count;
        smith_lang_enum_braces = smith_lang_saved_enum_braces;
    }
}

static void smith_lang_enum_shapes_reset(void) {
    smith_lang_eshape_count = 0;
    smith_lang_eshape_pool_used = 0;
    smith_lang_edefined_count = 0;
    smith_lang_eshape_depth = 0;
    smith_lang_eshape_start = true;
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
} SmithLangEnumCall;

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

static bool smith_lang_eshape_defined(const char *tag) {
    for (size_t i = 0; i < smith_lang_edefined_count; i++) { // at most 48
        if (strcmp(smith_lang_edefined[i].tag, tag) == 0) return true;
    }
    return false;
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

// The definition of the struct of one call, ready for the queue (`struct tag {...} __attribute__(...);`).
static size_t smith_lang_eshape_define(SmithLang *lang, const SmithHost *host, const SmithToken *like,
                                       const SmithLangEnumShape *shape, const SmithLangEnumCall *call, const char *tag,
                                       SmithToken *made, size_t capacity) {
    static SmithToken span[smith_lang_queue_max];
    size_t used = 0;
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
    size_t count = smith_lang_enum_build(lang, host, like, span, 1, close, &name, made, capacity - 2);
    if (count == 0) return 0;
    made[count++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ";", 1, like, false);
    return count;
}

static void smith_lang_eshape_note_defined(const char *tag) {
    if (smith_lang_edefined_count >= smith_lang_eshape_defined_max) return;
    SmithLangEnumDefined *entry = &smith_lang_edefined[smith_lang_edefined_count++];
    snprintf(entry->tag, sizeof entry->tag, "%s", tag);
    entry->depth = smith_lang_eshape_depth;
}

// A use of a shape at `at` (a name and a `(`), not a member: its shape, else null.
static const SmithLangEnumShape *smith_lang_eshape_use(SmithLang *lang, const SmithHost *host, const SmithToken *first,
                                                       size_t at, const SmithToken *before) {
    const SmithToken *name = smith_lang_eshape_at(lang, host, first, at);
    const SmithLangEnumShape *shape = smith_lang_eshape_find(name);
    if (shape == nullptr || !smith_lang_punct(smith_lang_eshape_at(lang, host, first, at + 1), "(")) return nullptr;
    if (before != nullptr && (smith_lang_punct(before, ".") || smith_lang_punct(before, "->") ||
                              smith_lang_punct(before, "::") || smith_lang_ident(before, "struct"))) {
        return nullptr;
    }
    return shape;
}

// At the first token of a declaration or statement: the structs its enum type functions' calls name that no scope
// defines are defined in front of it. True when the queue was changed (the token is queued again, after them).
static bool smith_lang_eshape_define_ahead(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    static SmithToken made[smith_lang_queue_max];
    size_t used = 0;
    int depth = 0;
    const SmithToken *before =
        lang->prev_member ? &(SmithToken){.kind = SmithTokenKind_Punctuator, .text = ".", .length = 1} : nullptr;
    for (size_t at = 0; at < smith_lang_eshape_scan_max; at++) {
        const SmithToken *each = smith_lang_eshape_at(lang, host, token, at);
        if (each->kind == SmithTokenKind_Eof || each->foreign) break;
        depth += smith_lang_punct(each, "(") || smith_lang_punct(each, "[");
        depth -= smith_lang_punct(each, ")") || smith_lang_punct(each, "]");
        if (depth <= 0 && (smith_lang_punct(each, ";") || smith_lang_punct(each, "{") || smith_lang_punct(each, "}"))) {
            break; // the end of the head or the statement, or of the block it is in
        }
        const SmithLangEnumShape *shape = smith_lang_eshape_use(lang, host, token, at, before);
        before = each;
        if (shape == nullptr) continue;
        SmithLangEnumCall call;
        char tag[smith_lang_tuple_name_max];
        if (!smith_lang_eshape_call(lang, host, token, at + 1, shape, &call, false) ||
            !smith_lang_eshape_tag(lang, host, shape, &call, tag, sizeof tag, false)) {
            return false; // said where the call is replaced
        }
        if (smith_lang_ident(token, "export")) {
            smith_lang_error(lang, host, each->location,
                             "an exported declaration cannot name `%s(...)` yet: its struct is not in the module's "
                             "header (known gap); name a typedef of it in a non-exported declaration",
                             shape->name);
            return false;
        }
        at += call.close - at;
        if (smith_lang_eshape_defined(tag) || smith_lang_eshape_depth > 0) continue; // a body names it, or errs there
        size_t added = smith_lang_eshape_define(lang, host, token, shape, &call, tag, made + used,
                                                smith_lang_queue_max - used - 2);
        if (added == 0) {
            smith_lang_error(lang, host, each->location, "this enum type is too long to define");
            return false;
        }
        used += added;
        smith_lang_eshape_note_defined(tag);
    }
    if (used == 0 || used + 1 >= smith_lang_queue_max) return false;
    made[used] = *token;
    return smith_lang_tuple_replace(lang, 0, made, used + 1);
}

// A token of a file that has type functions returning enums, before any other reading of it: the declaration of one,
// the definitions a declaration needs and a use (`option(usize)` is `struct smith_enum_option_usize`). True when the
// token was consumed.
static bool smith_lang_enum_function(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    bool start = smith_lang_eshape_start;
    smith_lang_eshape_start = false;
    if (token->kind == SmithTokenKind_Punctuator && token->length == 1) {
        char c = token->text[0];
        smith_lang_eshape_start = c == ';' || c == '{' || c == '}';
        if (!token->synthetic && c == '{') smith_lang_eshape_depth += 1;
        if (!token->synthetic && c == '}') {
            if (smith_lang_eshape_depth > 0) smith_lang_eshape_depth -= 1;
            size_t kept = 0;
            for (size_t i = 0; i < smith_lang_edefined_count; i++) { // a definition ends with its block
                if (smith_lang_edefined[i].depth <= smith_lang_eshape_depth)
                    smith_lang_edefined[kept++] = smith_lang_edefined[i];
            }
            smith_lang_edefined_count = kept;
        }
    }
    if (token->foreign || lang->in_attribute || !lang->is_module || token->synthetic) return false;
    if (lang->recording) {
        smith_lang_enum_generic_dot(lang, host, token);
        const SmithLangEnumShape *inside =
            lang->prev_member ? nullptr : smith_lang_eshape_use(lang, host, token, 0, nullptr);
        if (inside != nullptr) {
            smith_lang_error(lang, host, token->location, "`%s(...)` cannot be used inside a generic yet (known gap)",
                             inside->name);
        }
        return false;
    }
    if (start && smith_lang_eshape_depth == 0 && smith_lang_ident(token, "type") &&
        smith_lang_eshape_declare(lang, host, token)) {
        return true;
    }
    if (smith_lang_eshape_count == 0) return false;
    if (start && smith_lang_eshape_define_ahead(lang, host, token)) return true;
    const SmithLangEnumShape *shape =
        lang->prev_member ? nullptr : smith_lang_eshape_use(lang, host, token, 0, nullptr);
    if (shape == nullptr) return false;
    SmithLangEnumCall call;
    char tag[smith_lang_tuple_name_max];
    SmithToken made[2];
    if (!smith_lang_eshape_call(lang, host, token, 1, shape, &call, true) ||
        !smith_lang_eshape_tag(lang, host, shape, &call, tag, sizeof tag, true)) {
        size_t close = smith_lang_eshape_close(lang, host, token, 1, "(", ")");
        if (close == 0) return false;
        made[0] = smith_lang_tuple_made(SmithTokenKind_Identifier, "int", 3, token, token->space_before); // so that the
        return smith_lang_tuple_replace(lang, close, made, 1); // declaration still reads, and says no more
    }
    if (!smith_lang_eshape_defined(tag)) {
        char written[smith_lang_tuple_name_max];
        size_t used = (size_t)snprintf(written, sizeof written, "%s(", shape->name);
        for (size_t i = 0; i < call.count && used + 48 < sizeof written; i++)
            used += (size_t)snprintf(written + used, sizeof written - used, "%s%.*s",
                                     i > 0 && !smith_lang_punct(&call.tokens[i], ",") ? " " : "",
                                     (int)call.tokens[i].length, call.tokens[i].text);
        snprintf(written + used, sizeof written - used, ")");
        smith_lang_error(lang, host, token->location,
                         "`%s` is first named inside a function (or where no declaration begins), and the struct it "
                         "stands for cannot be defined there yet (known gap): name it first at file scope, in a "
                         "declaration, for example `typedef %s Name;`",
                         written, written);
    }
    made[0] = smith_lang_tuple_made(SmithTokenKind_Identifier, "struct", 6, token, token->space_before);
    made[1] = smith_lang_tuple_made(SmithTokenKind_Identifier, smith_lang_tuple_keep(tag, strlen(tag)), strlen(tag),
                                    token, true);
    return smith_lang_tuple_replace(lang, call.close, made, 2);
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
                    smith_lang_punct(&before, ")") || smith_lang_punct(&before, "]"));
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
    if (!smith_lang_enum_is_bare(tokens, count) && !smith_lang_enum_is_braced(tokens, count)) return false;
    SmithLangEnumType type;
    return smith_lang_enum_param(host, callee, index, &type) &&
           smith_lang_enum_value(lang, host, &type, tokens, count, true, cur);
}
