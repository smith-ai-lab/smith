// `fields(T)` in a generic's body (issue #83). Included by runtime/lang_constexpr.c.
//
//   for constexpr (usize i = 0; i < countof(fields(T)); i++) {
//       if constexpr (is_float(typeof(x->[fields(T)[i]]))) { c::puts(fields(T)[i].name); }
//   }
//
// `fields(T)` is a compile-time list of the named fields of the struct T, as the parser has them: `countof(fields(T))`
// is how many, `fields(T)[i]` is one (with a compile-time index), `fields(T)[i].name` is its name as a string literal,
// `x.[fields(T)[i]]` and `x->[fields(T)[i]]` are the member (`x.name`, `x->name`), and `typeof(T.[fields(T)[i]])` (or
// `typeof(x->[...])`) is its type, in a condition the type the parser says the field has and in code `typeof(x->name)`
// (`typeof(((T *)0)->name)` for a T). Every use is written as plain member access; nothing is left for run time.
// Unions, anonymous members and a T that is no struct are errors.

static void smith_lang_generic_poison(SmithLang *lang, const SmithHost *host, uint32_t location);
static bool smith_lang_cx_text(const SmithLangCx *cx, uint32_t first, uint32_t last, char *text, size_t capacity);
static void smith_lang_fields_alias(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                    const SmithLangFrame *frame);
static bool smith_lang_fields_valued(uint32_t first, uint32_t last);
static bool smith_lang_fields_valued_type(SmithLangCx *cx, uint32_t first, uint32_t last, char *spelling,
                                          size_t capacity);
static bool smith_lang_cx_simple(SmithLangCx *cx, uint32_t first, uint32_t last, int64_t *result);
static int smith_lang_param_type_of(const SmithLangGeneric *generic, const SmithLangInstance *instance,
                                    SmithLangSpan span);

static bool smith_lang_ty_handle(SmithLang *lang, const SmithHost *host, const char *spelling, uint32_t location,
                                 uint64_t *handle);
static const char *smith_lang_ty_spelling_of(SmithLang *lang, uint64_t handle);

constexpr size_t smith_lang_fields_pieces_max = 40;

// The spelling of the type the last `smith_lang_fields_load` read.
static char smith_lang_fields_spelling[smith_lang_cx_name_max];

// What a use is written as: a few tokens.
typedef struct {
    size_t count;
    SmithTokenKind kind[smith_lang_fields_pieces_max];
    char text[smith_lang_fields_pieces_max][smith_lang_name_max];
} SmithLangFieldPieces;

// One field of `fields(T)[index]`: where the expression ends, which field it is.
typedef struct {
    uint32_t consumed; // the tokens of `fields ( T ) [ index ]`
    uint32_t type_at;  // the token T
    size_t index;
    SmithFieldInfo info;
    char message[smith_lang_message_max];
} SmithLangFieldRef;

static bool smith_lang_fields_punct(uint32_t at, uint32_t limit, const char *text) {
    return at < limit && smith_lang_gt_punct(smith_lang_cx_token(at), text);
}

// How many tokens `fields ( T )` at `at` takes (T a type parameter of the generic), or `fields ( typeof ( x ) )` (x
// a parameter of the generic whose type is a type of the instance: `auto x`, `T x`): 4, 7, or 0 when it is neither.
static uint32_t smith_lang_fields_call_len(uint32_t at, uint32_t limit) {
    if (at + 3 >= limit) return 0;
    const SmithLangGenToken *name = smith_lang_cx_token(at);
    if (!smith_lang_cx_word(name, "fields") || name->special != SmithLangGen_Plain || name->param != 0 ||
        !smith_lang_fields_punct(at + 1, limit, "(")) {
        return 0;
    }
    const SmithLangGenToken *type = smith_lang_cx_token(at + 2);
    if (type->kind == SmithTokenKind_Identifier && type->special == SmithLangGen_Plain && type->param != 0 &&
        smith_lang_fields_punct(at + 3, limit, ")")) {
        return 4;
    }
    const SmithLangGenToken *value = smith_lang_cx_token(at + 4);
    return at + 6 < limit && smith_lang_cx_word(type, "typeof") && smith_lang_fields_punct(at + 3, limit, "(") &&
                   value->kind == SmithTokenKind_Identifier && value->special == SmithLangGen_Plain &&
                   smith_lang_fields_punct(at + 5, limit, ")") && smith_lang_fields_punct(at + 6, limit, ")")
               ? 7
               : 0;
}

// Whether the tokens at `at` are `fields ( T ) [`: a field.
static bool smith_lang_fields_field_at(uint32_t at, uint32_t limit) {
    uint32_t length = smith_lang_fields_call_len(at, limit);
    return length != 0 && smith_lang_fields_punct(at + length, limit, "[");
}

// How many tokens `countof ( fields ( T ) )` at `at` takes, or 0.
static uint32_t smith_lang_fields_countof_len(uint32_t at, uint32_t limit) {
    if (at + 6 >= limit || !smith_lang_cx_word(smith_lang_cx_token(at), "countof") ||
        !smith_lang_fields_punct(at + 1, limit, "(")) {
        return 0;
    }
    uint32_t length = smith_lang_fields_call_len(at + 2, limit);
    return length != 0 && smith_lang_fields_punct(at + 2 + length, limit, ")") ? length + 3 : 0;
}

static bool smith_lang_fields_countof_at(uint32_t at, uint32_t limit) {
    return smith_lang_fields_countof_len(at, limit) != 0;
}

// What the parser says of the type of `fields ( ... )` at `call_at`, as the instance has it: the `index`th field. False
// with the message.
static bool smith_lang_fields_load(const SmithHost *host, const SmithLangGeneric *generic,
                                   const SmithLangInstance *instance, uint32_t call_at, uint32_t location, size_t index,
                                   SmithFieldInfo *info, char *message, size_t capacity) {
    if (host->type_field == nullptr || instance == nullptr) {
        snprintf(message, capacity,
                 "`fields(T)` needs the compiler to read the struct: a raw lowering (a bootstrapped module, the "
                 "stage0) cannot write it");
        return false;
    }
    bool of_value = smith_lang_cx_word(smith_lang_cx_token(call_at + 2), "typeof");
    uint32_t type_at = call_at + (of_value ? 4 : 2);
    uint32_t mark[2] = {smith_lang_it.count, smith_lang_it.text_used};
    uint32_t first = 0;
    uint32_t count = 0;
    int own = of_value ? smith_lang_param_type_of(generic, instance, (SmithLangSpan){type_at, type_at + 1}) : -1;
    if (of_value && own < 0) {
        snprintf(message, capacity, "`fields(typeof(%s))`: `%s` is not a parameter of this generic whose type is known",
                 smith_lang_gt_str(smith_lang_cx_token(type_at)), smith_lang_gt_str(smith_lang_cx_token(type_at)));
        return false;
    }
    if (of_value) {
        first = instance->arg_first[own];
        count = instance->arg_count[own];
    } else if (!smith_lang_stage_type((SmithLangSpan){type_at, type_at + 1}, instance, location, &first, &count)) {
        snprintf(message, capacity, "`fields(T)`: this type is too long");
        return false;
    }
    char spelling[smith_lang_cx_name_max];
    smith_lang_it_spelling(first, count, spelling, sizeof spelling);
    smith_lang_it.count = mark[0];
    smith_lang_it.text_used = mark[1];
    snprintf(smith_lang_fields_spelling, sizeof smith_lang_fields_spelling, "%s", spelling);
    *info = (SmithFieldInfo){};
    host->type_field(host->context, spelling, (uint32_t)strlen(spelling), (uint32_t)index, info);
    if (info->status == SmithFields_Ok) return true;
    static const char *const what[] = {
        "",
        "is not a struct",
        "is a union; its members share their storage, so it has no list of fields",
        "has a member that is an anonymous struct or union; give it a name",
        "is an incomplete struct",
        "is not a type the compiler can read",
    };
    snprintf(message, capacity, "`fields(%s)`: `%s` %s", smith_lang_gt_str(smith_lang_cx_token(type_at)), spelling,
             what[info->status]);
    return false;
}

// The field expression `fields ( T ) [ index ]` at `at`: 1 and the field, 0 when it is none, -1 and the message.
static int smith_lang_field_expr(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay, uint32_t at,
                                 uint32_t limit, uint32_t place, SmithLangFieldRef *ref) {
    *ref = (SmithLangFieldRef){};
    if (replay->instance == nullptr || !smith_lang_fields_field_at(at, limit)) return 0;
    uint32_t length = smith_lang_fields_call_len(at, limit);
    uint32_t after = smith_lang_cx_skip_group(at + length, limit);
    ref->consumed = after == UINT32_MAX ? limit - at : after - at;
    ref->type_at = at + 2;
    if (after == UINT32_MAX || after - 1 <= at + length + 1) {
        snprintf(ref->message, sizeof ref->message, "`fields(T)[` is not closed by `]`, or has no index");
        return -1;
    }
    static SmithLangCx evaluation;
    evaluation =
        (SmithLangCx){.lang = lang, .host = host, .instance = replay->instance, .replay = replay, .location = place};
    int64_t value = 0;
    if (!smith_lang_cx_simple(&evaluation, at + length + 1, after - 1, &value)) {
        snprintf(ref->message, sizeof ref->message,
                 "the index of `fields(T)[...]` is a compile-time number (a literal, the variable of a `for "
                 "constexpr`, `countof(...)`, with `+` and `-`): %s",
                 evaluation.message);
        return -1;
    }
    SmithFieldInfo info;
    if (!smith_lang_fields_load(host, replay->generic, replay->instance, at, place, 0, &info, ref->message,
                                sizeof ref->message)) {
        return -1;
    }
    if (value < 0 || (uint64_t)value >= info.count) {
        snprintf(ref->message, sizeof ref->message, "`fields(T)[%lld]` is out of range: the type has %u field%s",
                 (long long)value, info.count, info.count == 1 ? "" : "s");
        return -1;
    }
    ref->index = (size_t)value;
    return smith_lang_fields_load(host, replay->generic, replay->instance, at, place, ref->index, &ref->info,
                                  ref->message, sizeof ref->message)
               ? 1
               : -1;
}

// The name of the field a `constexpr Field` holds, as its member: a tuple's "0" is `_0`.
static void smith_lang_fields_member(const char *name, char *member, size_t capacity) {
    bool digits = name[0] != 0;
    for (const char *c = name; *c != 0; c++)
        digits = digits && *c >= '0' && *c <= '9';
    snprintf(member, capacity, digits ? "_%s" : "%s", name);
}

// What the compiler evaluates `( E ) . member` to for the tokens [first, last), as an integer or a string.
static bool smith_lang_fields_ask(SmithLangCx *cx, uint32_t first, uint32_t last, const char *member,
                                  SmithValueQuery *query) {
    static SmithValue nodes[smith_lang_cx_value_nodes];
    char text[1024];
    char tokens[1100];
    if (cx->host->value_query == nullptr) {
        return smith_lang_cx_fail(cx, "a field kept in a variable needs smith's compiler to read it; the raw lowering "
                                      "has none");
    }
    if (!smith_lang_cx_text(cx, first, last, text, sizeof text)) return smith_lang_cx_fail(cx, "it is too long");
    snprintf(tokens, sizeof tokens, "(\x1f%s\x1f)\x1f.\x1f%s", text, member);
    *query = (SmithValueQuery){.nodes = nodes, .capacity = smith_lang_cx_value_nodes};
    if (cx->host->value_query(cx->host->context, tokens, (uint32_t)strlen(tokens), query)) return true;
    return smith_lang_cx_fail(cx, "it is not a `Field`: %s", query->why != nullptr ? query->why : "no value");
}

// The field named by the tokens [first, last) inside `.[ ]`: `fields(T)[i]` itself, or any expression of type `Field`
// (a `constexpr Field f`, a call that returns one), whose name and type the compiler reads. 1 with the field, -1 and
// the message.
static int smith_lang_fields_group(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                   uint32_t first, uint32_t last, uint32_t place, SmithLangFieldRef *ref) {
    *ref = (SmithLangFieldRef){};
    if (replay == nullptr || replay->instance == nullptr) {
        snprintf(ref->message, sizeof ref->message, "`.[...]` needs an instance of the generic to read it");
        return -1;
    }
    uint32_t length = smith_lang_fields_call_len(first, last);
    if (length != 0 && smith_lang_fields_field_at(first, last) &&
        smith_lang_cx_skip_group(first + length, last) == last) {
        return smith_lang_field_expr(lang, host, replay, first, last, place, ref);
    }
    static SmithLangCx evaluation;
    evaluation =
        (SmithLangCx){.lang = lang, .host = host, .instance = replay->instance, .replay = replay, .location = place};
    static SmithValueQuery query;
    if (last <= first || !smith_lang_fields_ask(&evaluation, first, last, "name.data", &query)) {
        snprintf(ref->message, sizeof ref->message, "the field in `.[...]` is `fields(T)[i]` or a `Field`: %s",
                 last <= first ? "it is empty" : evaluation.message);
        return -1;
    }
    char name[sizeof ref->info.name];
    uint32_t used = 0;
    if (query.root.kind == SmithValue_Array && query.root.count < sizeof name) {
        for (; used < query.root.count; used++)
            name[used] = (char)query.root.items[used].magnitude;
    }
    name[used] = 0;
    if (used == 0 || query.root.kind != SmithValue_Array) {
        snprintf(ref->message, sizeof ref->message,
                 "the field in `.[...]` has no name: it is not a `Field` made by "
                 "`fields(T)`");
        return -1;
    }
    smith_lang_fields_member(name, ref->info.name, sizeof ref->info.name);
    if (smith_lang_fields_ask(&evaluation, first, last, "type", &query) && query.root.kind == SmithValue_Int) {
        const char *spelling = smith_lang_ty_spelling_of(lang, query.root.magnitude);
        if (spelling != nullptr) snprintf(ref->info.type, sizeof ref->info.type, "%s", spelling);
    }
    ref->consumed = last - first;
    return 1;
}

// The number of fields in `fields ( T )` at `at`. False with the message.
static bool smith_lang_fields_count(const SmithHost *host, const SmithLangGeneric *generic,
                                    const SmithLangInstance *instance, uint32_t at, uint32_t location, int64_t *count,
                                    char *message, size_t capacity) {
    SmithFieldInfo info;
    if (!smith_lang_fields_load(host, generic, instance, at, location, 0, &info, message, capacity)) return false;
    *count = info.count;
    return true;
}

// ---- In a condition

// The `[` that matches the `]` at `close`, searching down to `floor`, or UINT32_MAX.
static uint32_t smith_lang_fields_open_of(uint32_t close, uint32_t floor) {
    int depth = 0;
    for (uint32_t i = close + 1; i > floor; i--) { // bounded by the group
        const SmithLangGenToken *part = smith_lang_cx_token(i - 1);
        depth += smith_lang_gt_close(part) ? 1 : smith_lang_gt_open(part) ? -1 : 0;
        if (depth == 0) return i - 1;
    }
    return UINT32_MAX;
}

// In `typeof ( ... . [ F ] [ i ] [ j ] )` from `at` to `after` (the index after its `)`): the `]` that closes the field
// group and how many subscripts follow it (the element of a slice or an array: `typeof(x.[f][0])`). False when the
// tokens are no member of a struct.
static bool smith_lang_fields_typeof_parts(uint32_t at, uint32_t after, uint32_t *close, uint32_t *subscripts) {
    *close = after - 2;
    *subscripts = 0;
    for (size_t guard = 0; guard < 8; guard++) { // the subscripts
        if (!smith_lang_gt_punct(smith_lang_cx_token(*close), "]")) return false;
        uint32_t open = smith_lang_fields_open_of(*close, at + 1);
        if (open == UINT32_MAX || open <= at + 2) return false;
        const SmithLangGenToken *before = smith_lang_cx_token(open - 1);
        if (smith_lang_gt_punct(before, ".") || smith_lang_gt_punct(before, "->")) return true;
        if (!smith_lang_gt_punct(before, "]")) return false;
        *subscripts += 1;
        *close = open - 1;
    }
    return false;
}

// The index after `typeof ( ... . [ fields(T)[i] ] )` at `at`, or 0 when the tokens are not that: the type of a field
// (or of an element of it, `typeof(x.[f][0])`).
static uint32_t smith_lang_fields_typeof_end(uint32_t at, uint32_t limit) {
    const SmithLangGenToken *token = smith_lang_cx_token(at);
    if (at + 5 >= limit || !(smith_lang_cx_word(token, "typeof") || smith_lang_cx_word(token, "__typeof__")) ||
        !smith_lang_fields_punct(at + 1, limit, "(")) {
        return 0;
    }
    uint32_t after = smith_lang_cx_skip_group(at + 1, limit);
    uint32_t close = 0;
    uint32_t subscripts = 0;
    if (after == UINT32_MAX || after < at + 8 || !smith_lang_fields_typeof_parts(at, after, &close, &subscripts)) {
        return 0;
    }
    return after;
}

// The field in `typeof ( ... [ F ] )`, the tokens [first, last): 1 and the field, -1 and the message. `subscripts` is
// how many `[i]` follow the field.
static int smith_lang_fields_in_typeof(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                       uint32_t place, uint32_t first, uint32_t last, SmithLangFieldRef *ref,
                                       uint32_t *subscripts) {
    uint32_t close = 0;
    smith_lang_fields_typeof_parts(first, last, &close, subscripts);
    uint32_t open = smith_lang_fields_open_of(close, first + 1);
    return smith_lang_fields_group(lang, host, replay, open + 1, close, place, ref);
}

// The element of a type: what a subscript of a value of it is. `spelling` is rewritten (a pointer or an array, or the
// struct of a slice, whose element is what its `data` points to). False when there is none.
static bool smith_lang_fields_element(const SmithHost *host, char *spelling, size_t capacity) {
    size_t length = strlen(spelling);
    char *open = strchr(spelling, '[');
    if (open != nullptr && length > 0 && spelling[length - 1] == ']') { // `double[4]`: the first dimension goes
        char *close = strchr(open, ']');
        memmove(open, close + 1, strlen(close + 1) + 1);
        length = strlen(spelling);
        while (length > 0 && spelling[length - 1] == ' ')
            spelling[--length] = 0;
        return true;
    }
    if (strncmp(spelling, "struct smith_array_", 19) == 0 && host->type_field != nullptr) {
        SmithFieldInfo info = {};
        host->type_field(host->context, spelling, (uint32_t)length, 0, &info);
        if (info.status != SmithFields_Ok || strcmp(info.name, "data") != 0) return false;
        snprintf(spelling, capacity, "%s", info.type);
        length = strlen(spelling);
    }
    if (length == 0 || spelling[length - 1] != '*') return false;
    spelling[--length] = 0;
    while (length > 0 && spelling[length - 1] == ' ')
        spelling[--length] = 0;
    return true;
}

// The spelling of the type of the field in `typeof ( ... [ F ] )` at `first`..`last`, for the parser to read.
static bool smith_lang_cx_field_type(SmithLangCx *cx, uint32_t first, uint32_t last, char *spelling, size_t capacity) {
    SmithLangFieldRef ref;
    uint32_t subscripts = 0;
    if (smith_lang_fields_in_typeof(cx->lang, cx->host, cx->replay, cx->location, first, last, &ref, &subscripts) !=
        1) {
        return smith_lang_cx_fail(cx, "%s", ref.message);
    }
    snprintf(spelling, capacity, "%s", ref.info.type);
    for (uint32_t i = 0; i < subscripts; i++) { // the element of the field
        if (!smith_lang_fields_element(cx->host, spelling, capacity)) {
            return smith_lang_cx_fail(cx, "`%s` has no element: it is not a slice, an array or a pointer", spelling);
        }
    }
    return true;
}

// Whether the tokens [first, last) use `fields`, so the arguments of a call that holds them are typed as the instance
// writes them (a member of a parameter, which is not declared while the body is written).
static bool smith_lang_fields_in(uint32_t first, uint32_t last) {
    for (uint32_t at = first; at < last; at++) { // bounded by the call
        if (smith_lang_fields_call_len(at, last) != 0) return true;
        if ((smith_lang_fields_punct(at, last, ".") || smith_lang_fields_punct(at, last, "->")) &&
            smith_lang_fields_punct(at + 1, last, "[")) {
            return true;
        }
    }
    return false;
}

// ---- In the written code

static void smith_lang_fields_piece(SmithLangFieldPieces *pieces, SmithTokenKind kind, const char *text) {
    if (pieces->count >= smith_lang_fields_pieces_max) return;
    pieces->kind[pieces->count] = kind;
    snprintf(pieces->text[pieces->count], sizeof pieces->text[0], "%s", text);
    pieces->count += 1;
}

// Whether the tokens at `at` begin a use of `fields`: the cheap check that nothing needs evaluating for.
static bool smith_lang_fields_begins(const SmithLangReplay *replay, uint32_t at, uint32_t limit) {
    if (replay->generic == nullptr || replay->instance == nullptr) return false;
    const SmithLangGenToken *token = smith_lang_cx_token(at);
    if (token->special != SmithLangGen_Plain) return false;
    if (smith_lang_fields_countof_at(at, limit) || smith_lang_fields_call_len(at, limit) != 0) return true;
    if (smith_lang_fields_typeof_end(at, limit) != 0) return true;
    bool member = smith_lang_fields_punct(at, limit, ".") || smith_lang_fields_punct(at, limit, "->");
    if (member && smith_lang_fields_punct(at + 1, limit, "[")) return true;
    return token->kind == SmithTokenKind_Identifier && token->param != 0 &&
           smith_lang_fields_punct(at + 1, limit, ".") && smith_lang_fields_punct(at + 2, limit, "[");
}

// The tokens of the instance's own T, as the parser reads them.
static void smith_lang_fields_type_pieces(const SmithLangReplay *replay, uint32_t type_at,
                                          SmithLangFieldPieces *pieces) {
    unsigned param = (unsigned)smith_lang_cx_token(type_at)->param - 1;
    for (uint32_t i = 0; i < replay->instance->arg_count[param]; i++) {
        const SmithLangGenToken *part = &smith_lang_it.tokens[replay->instance->arg_first[param] + i];
        char text[smith_lang_name_max];
        size_t length = part->length < sizeof text - 1 ? part->length : sizeof text - 1;
        memcpy(text, smith_lang_pool_text(&smith_lang_it, part), length);
        text[length] = 0;
        smith_lang_fields_piece(pieces, (SmithTokenKind)part->kind, text);
    }
}

// A use of `fields` being written: where it is, what it takes, what it becomes.
typedef struct {
    SmithLang *lang;
    const SmithHost *host;
    const SmithLangReplay *replay;
    uint32_t at;
    uint32_t limit;
    uint32_t place;
    uint32_t consumed;
    SmithLangFieldPieces *pieces;
    SmithLangFieldRef ref;
} SmithLangFieldUse;

// `typeof(... [F])` of a bit-field, whose type C cannot take with typeof: the field's declared type is written. 0 for a
// member that is not a bit-field (typeof of it as it is), 1 written, -1 an error.
static int smith_lang_fields_use_typeof(SmithLangFieldUse *use) {
    uint32_t end = smith_lang_fields_typeof_end(use->at, use->limit);
    uint32_t subscripts = 0;
    int found = smith_lang_fields_in_typeof(use->lang, use->host, use->replay, use->place, use->at, end, &use->ref,
                                            &subscripts);
    if (found == 1 && (!use->ref.info.bit_field || subscripts != 0)) return 0;
    use->consumed = end - use->at;
    if (found != 1) return found;
    char text[smith_lang_name_max];
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Identifier, smith_lang_gt_str(smith_lang_cx_token(use->at)));
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Punctuator, "(");
    for (const char *word = use->ref.info.type; *word != 0;) { // the words of the type, `unsigned char`
        size_t length = strcspn(word, " ");
        snprintf(text, sizeof text, "%.*s", (int)length, word);
        smith_lang_fields_piece(use->pieces, SmithTokenKind_Identifier, text);
        word += length + (word[length] == ' ' ? 1 : 0);
    }
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Punctuator, ")");
    return 1;
}

// `countof(fields(T))`: the number.
static int smith_lang_fields_use_countof(SmithLangFieldUse *use) {
    int64_t count = 0;
    use->consumed = smith_lang_fields_countof_len(use->at, use->limit);
    if (!smith_lang_fields_count(use->host, use->replay->generic, use->replay->instance, use->at + 2, use->place,
                                 &count, use->ref.message, sizeof use->ref.message)) {
        return -1;
    }
    char text[smith_lang_name_max];
    snprintf(text, sizeof text, "%lld", (long long)count);
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Number, text);
    return 1;
}

// The arrays `fields(T)` is, one per type, that the instance being written declared (smith_lang_fields_declare).
typedef struct {
    uint32_t instance;
    size_t count;
    char spelling[16][smith_lang_cx_name_max];
} SmithLangFieldArrays;

static SmithLangFieldArrays smith_lang_field_arrays;

// A `constexpr Field f = fields(T)[i];` of the body being written is not read by the parser until the instance is done,
// so the compiler cannot be asked about `f` (`is_f32(f)`, `x.[f]`): the name stands for its value, written out.
typedef struct {
    size_t count;
    char name[16][smith_lang_name_max];
    char text[16][1024];
} SmithLangFieldAliases;

static SmithLangFieldAliases smith_lang_field_aliases;

// The name of the maker: written as it is, since the definition is (a module's own functions are prefixed).
static void smith_lang_fields_maker_name(SmithLang *lang, char *name, size_t capacity) {
    (void)lang;
    snprintf(name, capacity, "smith_field_make");
}

// The call `smith_field_make("name", length, handle)` that is the field `fields(T)[i]`, as pieces.
static bool smith_lang_fields_make(SmithLangFieldUse *use, const SmithFieldInfo *info) {
    uint64_t handle = 0;
    if (!smith_lang_ty_handle(use->lang, use->host, info->type, use->place, &handle)) {
        snprintf(use->ref.message, sizeof use->ref.message, "the type of the field `%s` is no type the compiler reads",
                 info->name);
        return false;
    }
    const char *name = info->tuple ? info->name + 1 : info->name;
    char text[smith_lang_name_max];
    smith_lang_fields_maker_name(use->lang, text, sizeof text);
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Identifier, text);
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Punctuator, "(");
    snprintf(text, sizeof text, "\"%s\"", name);
    smith_lang_fields_piece(use->pieces, SmithTokenKind_String, text);
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Punctuator, ",");
    snprintf(text, sizeof text, "%zu", strlen(name));
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Number, text);
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Punctuator, ",");
    snprintf(text, sizeof text, "%lluULL", (unsigned long long)handle);
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Number, text);
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Punctuator, ")");
    return true;
}

// `fields(T)[i]` as a value is a call of `smith_field_make`; `fields(T)` alone is its array.
static int smith_lang_fields_use_value(SmithLangFieldUse *use) {
    uint32_t length = smith_lang_fields_call_len(use->at, use->limit);
    if (smith_lang_fields_punct(use->at + length, use->limit, "[")) {
        int found =
            smith_lang_field_expr(use->lang, use->host, use->replay, use->at, use->limit, use->place, &use->ref);
        use->consumed = use->ref.consumed;
        return found == 1 && !smith_lang_fields_make(use, &use->ref.info) ? -1 : found;
    }
    use->consumed = length;
    SmithFieldInfo info;
    if (!smith_lang_fields_load(use->host, use->replay->generic, use->replay->instance, use->at, use->place, 0, &info,
                                use->ref.message, sizeof use->ref.message)) {
        return -1;
    }
    for (size_t k = 0; k < smith_lang_field_arrays.count; k++) { // at most 16
        if (smith_lang_field_arrays.instance != use->replay->instance_id ||
            strcmp(smith_lang_field_arrays.spelling[k], smith_lang_fields_spelling) != 0) {
            continue;
        }
        char text[smith_lang_name_max];
        snprintf(text, sizeof text, "smith_fields_%u_%zu", use->replay->instance_id, k);
        smith_lang_fields_piece(use->pieces, SmithTokenKind_Identifier, text);
        return 1;
    }
    snprintf(use->ref.message, sizeof use->ref.message, "this instance uses `fields` of more than 16 types");
    return -1;
}

// `fields(T)[i].name`: the name as a string literal; `fields(T)` and `fields(T)[i]` otherwise: the value.
static int smith_lang_fields_use_name(SmithLangFieldUse *use) {
    uint32_t length = smith_lang_fields_call_len(use->at, use->limit);
    uint32_t after = smith_lang_fields_punct(use->at + length, use->limit, "[")
                         ? smith_lang_cx_skip_group(use->at + length, use->limit)
                         : UINT32_MAX;
    if (after == UINT32_MAX || !smith_lang_fields_punct(after, use->limit, ".") ||
        !smith_lang_cx_word(smith_lang_cx_token(after + 1), "name")) {
        return smith_lang_fields_use_value(use);
    }
    int found = smith_lang_field_expr(use->lang, use->host, use->replay, use->at, use->limit, use->place, &use->ref);
    use->consumed = use->ref.consumed;
    if (found != 1) return found;
    use->consumed += 2;
    char text[smith_lang_name_max];
    snprintf(text, sizeof text, "\"%s\"", use->ref.info.tuple ? use->ref.info.name + 1 : use->ref.info.name);
    smith_lang_fields_piece(use->pieces, SmithTokenKind_String, text);
    return 1;
}

// `.[F]`, `->[F]` or `T.[F]`: the member name after its operator.
static int smith_lang_fields_use_member(SmithLangFieldUse *use) {
    bool object = smith_lang_cx_token(use->at)->kind == SmithTokenKind_Identifier;
    uint32_t open = use->at + (object ? 2 : 1); // the `[`
    uint32_t after = smith_lang_cx_skip_group(open, use->limit);
    if (after == UINT32_MAX) {
        use->consumed = use->limit - use->at;
        snprintf(use->ref.message, sizeof use->ref.message, "`.[` is closed by `]` right after the field");
        return -1;
    }
    int found = smith_lang_fields_group(use->lang, use->host, use->replay, open + 1, after - 1, use->place, &use->ref);
    use->consumed = after - use->at;
    if (found != 1) return found;
    if (object) { // T.[F]: the member of a value that is never evaluated
        static const char *const opening[] = {"(", "("};
        for (size_t i = 0; i < 2; i++)
            smith_lang_fields_piece(use->pieces, SmithTokenKind_Punctuator, opening[i]);
        smith_lang_fields_type_pieces(use->replay, use->at, use->pieces);
        static const char *const closing[] = {"*", ")"};
        for (size_t i = 0; i < 2; i++)
            smith_lang_fields_piece(use->pieces, SmithTokenKind_Punctuator, closing[i]);
        smith_lang_fields_piece(use->pieces, SmithTokenKind_Number, "0");
        smith_lang_fields_piece(use->pieces, SmithTokenKind_Punctuator, ")");
        smith_lang_fields_piece(use->pieces, SmithTokenKind_Punctuator, "->");
    } else {
        smith_lang_fields_piece(use->pieces, SmithTokenKind_Punctuator,
                                smith_lang_gt_str(smith_lang_cx_token(use->at)));
    }
    smith_lang_fields_piece(use->pieces, SmithTokenKind_Identifier, use->ref.info.name);
    return 1;
}

// What the use of `fields` at `at` is written as: 0 when the tokens are none (nothing written), 1 with the pieces, -1
// after an error (reported; the pieces are empty). `consumed` is how many tokens the use takes.
static int smith_lang_fields_use(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay, uint32_t at,
                                 uint32_t limit, SmithLangFieldPieces *pieces, uint32_t *consumed) {
    pieces->count = 0;
    if (!smith_lang_fields_begins(replay, at, limit)) return 0;
    SmithLangFieldUse use = {.lang = lang,
                             .host = host,
                             .replay = replay,
                             .at = at,
                             .limit = limit,
                             .place =
                                 smith_lang_gen_place(host, replay->generic, smith_lang_cx_token(at), replay->location),
                             .consumed = 1,
                             .pieces = pieces};
    int found = 0;
    if (smith_lang_fields_typeof_end(at, limit) != 0)
        found = smith_lang_fields_use_typeof(&use);
    else if (smith_lang_fields_countof_at(at, limit))
        found = smith_lang_fields_use_countof(&use);
    else if (smith_lang_fields_call_len(at, limit) != 0)
        found = smith_lang_fields_use_name(&use);
    else
        found = smith_lang_fields_use_member(&use);
    *consumed = use.consumed;
    if (found >= 0) return found;
    smith_lang_error(lang, host, use.place, "%s", use.ref.message);
    pieces->count = 0;
    bool member =
        smith_lang_cx_token(at)->kind == SmithTokenKind_Punctuator && smith_lang_fields_punct(at + 1, limit, "[");
    if (member) { // `x->[F]`: the operator stays, so that the error is the only one
        smith_lang_fields_piece(pieces, SmithTokenKind_Punctuator, smith_lang_gt_str(smith_lang_cx_token(at)));
    }
    return -1;
}

// ---- `countof` of a member in an instance's body (issue #123)

// `countof(x.[f])`, `countof(p->[f])`, `countof(p->rows[1])`: the member is a slice or a fixed array of a struct the
// instance was given, which the compiler has not read (the body is written at the end of the file). The member is typed
// the way a call's arguments are (its names as values of their types), and then the count is written: a number for an
// array, the `count` of a slice's struct.

// Whether the tokens at `at` are `countof ( E )` with a member access in E: where E is, [*first, *last), and the index
// after the `)`.
static bool smith_lang_countof_member_at(const SmithLangFrame *frame, uint32_t *first, uint32_t *last,
                                         uint32_t *after) {
    uint32_t at = frame->pos;
    const SmithLangGenToken *token = smith_lang_cx_token(at);
    if (at + 4 >= frame->end || !smith_lang_cx_word(token, "countof") || token->special != SmithLangGen_Plain ||
        token->param != 0 || smith_lang_member_before(at) || !smith_lang_fields_punct(at + 1, frame->end, "(") ||
        smith_lang_fields_countof_at(at, frame->end)) {
        return false;
    }
    uint32_t close = smith_lang_cx_skip_group(at + 1, frame->end);
    if (close == UINT32_MAX) return false;
    bool member = false;
    for (uint32_t i = at + 2; i + 1 < close; i++) { // bounded by the group
        const SmithLangGenToken *part = smith_lang_cx_token(i);
        member = member || (part->special == SmithLangGen_Plain &&
                            (smith_lang_gt_punct(part, ".") || smith_lang_gt_punct(part, "->")));
    }
    *first = at + 2;
    *last = close - 1;
    *after = close;
    return member;
}

static bool smith_lang_countof_words(SmithLangReplay *replay, uint32_t place, const char *words) {
    if (!smith_lang_replay_push(replay, 0, 0, 0)) return false;
    SmithLangFrame *frame = &replay->frames[replay->count - 1];
    snprintf(frame->words, sizeof frame->words, "%s", words);
    frame->place = place;
    return true;
}

// The `countof` at the frame's position, if it takes a member: true when the tokens were taken.
static bool smith_lang_countof_begin(SmithLangReplay *replay, uint32_t place, SmithLangFrame *frame) {
    uint32_t first = 0;
    uint32_t last = 0;
    uint32_t after = 0;
    if (!smith_lang_countof_member_at(frame, &first, &last, &after) ||
        replay->count + 4 > smith_lang_replay_depth_max) {
        return false;
    }
    frame->pos = after;
    if (replay->typing) { // only a number of the type is wanted
        smith_lang_countof_words(replay, place, "( ( __SIZE_TYPE__ ) 0 )");
        return true;
    }
    smith_lang_replay_push(replay, first, last, 0);
    SmithLangFrame *finish = &replay->frames[replay->count - 1];
    finish->action = SmithLangFrame_CountFinish;
    finish->place = place;
    size_t finish_at = replay->count - 1;
    smith_lang_replay_push(replay, first, last, 0); // the member, written to be typed
    smith_lang_replay_push(replay, 0, 0, 0);
    SmithLangFrame *start = &replay->frames[replay->count - 1];
    start->action = SmithLangFrame_TypedStart;
    start->source = (int32_t)finish_at;
    return true;
}

// The member was written to the pool: ask the compiler its type, then write the count.
static void smith_lang_countof_finish(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                      SmithLangFrame *frame) {
    const SmithLangFrame done = *frame;
    replay->count -= 1;
    lang->cap_frames -= 1;
    replay->typing = done.started;
    char tokens[1024];
    size_t used = 0;
    bool fits = true;
    for (uint32_t at = done.pool_first; at < smith_lang_gt.count; at++) { // the captured tokens
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (used + token->length + 2 >= sizeof tokens) {
            fits = false;
            break;
        }
        if (at > done.pool_first) tokens[used++] = '\x1f';
        memcpy(tokens + used, smith_lang_gt_str(token), token->length);
        used += token->length;
    }
    tokens[used] = 0;
    smith_lang_gt.count = done.pool_first;
    smith_lang_gt.text_used = done.pool_text;
    SmithExprType type = {.explain = true};
    bool typed = fits && host->expr_type != nullptr && host->expr_type(host->context, tokens, (uint32_t)used, &type);
    uint32_t place = done.place;
    if (!typed) {
        smith_lang_error(lang, host, place, "cannot tell what `countof` counts here: %s",
                         !fits                 ? "the member is too long"
                         : type.why != nullptr ? type.why
                                               : "the compiler cannot say");
        smith_lang_generic_poison(lang, host, place);
        return;
    }
    static const char tag[] = "struct smith_array_";
    if (type.is_array) {
        char number[32];
        snprintf(number, sizeof number, "%llu", (unsigned long long)type.array_size);
        smith_lang_gen_put(lang, host, SmithTokenKind_Number, number, place, replay->instance_id);
    } else if (strncmp(type.canonical, tag, sizeof tag - 1) == 0 && strpbrk(type.canonical + 7, "*[( ") == nullptr) {
        smith_lang_countof_words(replay, place, ") . count [ 0 ] )");
        smith_lang_replay_push(replay, done.pos, done.end, 0);
        smith_lang_countof_words(replay, place, "( (");
    } else {
        smith_lang_error(lang, host, place, "`countof` of this is no slice or array: it is `%s`", type.written);
        smith_lang_generic_poison(lang, host, place);
    }
}

// Writes the use of `fields` at the frame's position, if there is one: true when the tokens were taken.
static bool smith_lang_fields_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                     SmithLangFrame *frame) {
    static SmithLangFieldPieces pieces;
    uint32_t consumed = 0;
    if (replay->instance != nullptr && smith_lang_cx_word(smith_lang_cx_token(frame->pos), "constexpr")) {
        smith_lang_fields_alias(lang, host, replay, frame);
    }
    if (replay->instance != nullptr &&
        smith_lang_countof_begin(
            replay, smith_lang_gen_place(host, replay->generic, smith_lang_cx_token(frame->pos), replay->location),
            frame)) {
        return true;
    }
    int found = smith_lang_fields_use(lang, host, replay, frame->pos, frame->end, &pieces, &consumed);
    if (found == 0) return false;
    uint32_t place = smith_lang_gen_place(host, replay->generic, smith_lang_cx_token(frame->pos), replay->location);
    frame->pos += consumed;
    for (size_t i = 0; i < pieces.count; i++)
        smith_lang_gen_put(lang, host, pieces.kind[i], pieces.text[i], place, replay->instance_id);
    if (found < 0) smith_lang_generic_poison(lang, host, place);
    return true;
}

// ---- `fields(T)` as a value

// How the `fields ( T )` at `at` (a call of `len` tokens) is used as a value: 0 not (`countof(...)`, inside `.[ ]` or
// `typeof`, `fields(T)[i].name`: the places that are written without a `Field`), 1 as `fields(T)[i]` (a call of
// smith_field_make), 2 as the array.
static int smith_lang_fields_value_use(uint32_t at, uint32_t len, uint32_t first, uint32_t limit) {
    if (at >= first + 2 && smith_lang_fields_punct(at - 1, limit, "(") &&
        smith_lang_cx_word(smith_lang_cx_token(at - 2), "countof")) {
        return 0;
    }
    if (at >= first + 2 && smith_lang_fields_punct(at - 1, limit, "[") &&
        (smith_lang_fields_punct(at - 2, limit, ".") || smith_lang_fields_punct(at - 2, limit, "->"))) {
        return 0;
    }
    if (!smith_lang_fields_punct(at + len, limit, "[")) return 2;
    uint32_t after = smith_lang_cx_skip_group(at + len, limit);
    bool name = after != UINT32_MAX && smith_lang_fields_punct(after, limit, ".") &&
                smith_lang_cx_word(smith_lang_cx_token(after + 1), "name");
    return name ? 0 : 1;
}

static void smith_lang_fields_put(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                  uint32_t location, uint32_t id, SmithTokenKind kind, const char *text) {
    (void)generic;
    smith_lang_gen_put_expanding(lang, host, kind, text, location, id, strcmp(text, "__SIZE_TYPE__") == 0);
}

// The definition of `Field`, as the lowering leaves it: `struct smith_field { char name[:]; u64 type; }` (a `type` is a
// 64-bit handle to clang), and `smith_field_make`, the function every `fields(T)[i]` is written as a call of: a
// compile-time call the parser can evaluate wherever it has read the definition, which the file's first declaration
// brings. The second list is for a file where the struct of a `char[:]` is already defined.
static const char *const smith_lang_field_words[] = {
    "typedef", "struct", "smith_field", "{",    "struct", "smith_array_char_n",
    "{",       "char",   "*",           "data", ";",      "__SIZE_TYPE__",
    "count",   "[",      "1",           "]",    ";",      "}",
    "name",    ";",      "unsigned",    "long", "long",   "type",
    ";",       "}",      "Field",       ";"};
static const char *const smith_lang_field_words_bare[] = {
    "typedef", "struct", "smith_field", "{",    "struct", "smith_array_char_n",
    "name",    ";",      "unsigned",    "long", "long",   "type",
    ";",       "}",      "Field",       ";"};
static const char *const smith_lang_field_maker[] = {"[",      "[",     "maybe_unused",
                                                     "]",      "]",     "static",
                                                     "inline", "Field", "smith_field_make",
                                                     "(",      "char",  "*",
                                                     "name",   ",",     "__SIZE_TYPE__",
                                                     "length", ",",     "unsigned",
                                                     "long",   "long",  "kind",
                                                     ")",      "{",     "return",
                                                     "(",      "Field", ")",
                                                     "{",      "{",     "name",
                                                     ",",      "{",     "length",
                                                     "}",      "}",     ",",
                                                     "kind",   "}",     ";",
                                                     "}"};

// The words of the definition, in one list.
static size_t smith_lang_field_definition(bool bare, const char *const **words) {
    static const char *combined[128];
    size_t used = 0;
    size_t count = bare ? sizeof smith_lang_field_words_bare / sizeof smith_lang_field_words_bare[0]
                        : sizeof smith_lang_field_words / sizeof smith_lang_field_words[0];
    for (size_t i = 0; i < count; i++)
        combined[used++] = bare ? smith_lang_field_words_bare[i] : smith_lang_field_words[i];
    for (size_t i = 0; i < sizeof smith_lang_field_maker / sizeof smith_lang_field_maker[0]; i++)
        combined[used++] = smith_lang_field_maker[i];
    *words = combined;
    return used;
}

static bool smith_lang_slice_defined(const SmithLang *lang, const char *tag);
static const char *smith_lang_prefixed(SmithLang *lang, const char *name, size_t length);

// Whether the struct of a `char[:]` is defined already, by this file or by a header it includes.
static bool smith_lang_fields_slice_bare(const SmithLang *lang, const SmithHost *host) {
    static const char slice[] = "smith_array_char_n";
    return smith_lang_slice_defined(lang, slice) ||
           (host->lookup != nullptr &&
            host->lookup(host->context, slice, sizeof slice - 1, SmithLookup_Tag).kind == SmithLookup_Tag);
}

// Writes the definition of `Field` where an instance is, in a file that did not define it at its first declaration.
static void smith_lang_fields_define(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                     uint32_t location, uint32_t id) {
    lang->field_defined = true; // first: a lookup below can set the state aside
    bool bare = smith_lang_fields_slice_bare(lang, host);
    bool dumping = lang->gen_dump_on; // the definitions are the library's, not what a test reads of the generics
    lang->gen_dump_on = false;
    const char *const *words = nullptr;
    size_t count = smith_lang_field_definition(bare, &words);
    for (size_t i = 0; i < count; i++) { // the tokens
        const char *word = words[i];
        bool identifier = isalpha((unsigned char)word[0]) || word[0] == '_';
        smith_lang_fields_put(lang, host, generic, location, id,
                              identifier                        ? SmithTokenKind_Identifier
                              : isdigit((unsigned char)word[0]) ? SmithTokenKind_Number
                                                                : SmithTokenKind_Punctuator,
                              word);
    }
    lang->gen_dump_on = dumping;
    if (!bare && lang->slice.tag_count < 32) {
        snprintf(lang->slice.tags[lang->slice.tag_count++], sizeof lang->slice.tags[0], "smith_array_char_n");
    }
}

// The array of one type: `static constexpr Field smith_fields_ID_K[N] = {{{"name", {length}}, handle}, ...};`, the
// name a `char[:]` made of a literal and the type a handle (runtime/lang_type.c).
static void smith_lang_fields_array(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                    const SmithLangInstance *instance, uint32_t call_at, uint32_t id, uint32_t location,
                                    size_t k) {
    SmithFieldInfo first;
    char message[smith_lang_message_max];
    if (!smith_lang_fields_load(host, generic, instance, call_at, location, 0, &first, message, sizeof message)) return;
    char spelling[smith_lang_cx_name_max];
    snprintf(spelling, sizeof spelling, "%s", smith_lang_fields_spelling);
    static const char *const head[] = {"[", "[", "maybe_unused", "]", "]", "static", "constexpr", "Field"};
    for (size_t i = 0; i < sizeof head / sizeof head[0]; i++) {
        bool punct = head[i][0] == '[' || head[i][0] == ']';
        smith_lang_fields_put(lang, host, generic, location, id,
                              punct ? SmithTokenKind_Punctuator : SmithTokenKind_Identifier, head[i]);
    }
    char text[smith_lang_name_max];
    snprintf(text, sizeof text, "smith_fields_%u_%zu", id, k);
    smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Identifier, text);
    smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, "[");
    snprintf(text, sizeof text, "%u", first.count);
    smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Number, text);
    smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, "]");
    smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, "=");
    smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, "{");
    for (uint32_t index = 0; index < first.count; index++) { // the fields
        SmithFieldInfo info;
        if (!smith_lang_fields_load(host, generic, instance, call_at, location, index, &info, message,
                                    sizeof message)) {
            break;
        }
        uint64_t handle = 0;
        if (!smith_lang_ty_handle(lang, host, info.type, location, &handle)) break;
        static const char *const open[] = {"{", "{"};
        for (size_t i = 0; i < 2; i++)
            smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, open[i]);
        snprintf(text, sizeof text, "\"%s\"", info.tuple ? info.name + 1 : info.name);
        smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_String, text);
        smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, ",");
        smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, "{");
        snprintf(text, sizeof text, "%zu", strlen(info.tuple ? info.name + 1 : info.name));
        smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Number, text);
        smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, "}");
        smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, "}");
        smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, ",");
        snprintf(text, sizeof text, "%lluULL", (unsigned long long)handle);
        smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Number, text);
        smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, "}");
        smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, ",");
    }
    smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, "}");
    smith_lang_fields_put(lang, host, generic, location, id, SmithTokenKind_Punctuator, ";");
    snprintf(smith_lang_field_arrays.spelling[k], sizeof smith_lang_field_arrays.spelling[k], "%s", spelling);
}

// Before an instance is written: the array of each type its body asks `fields` of as a value, in the file.
static void smith_lang_fields_declare(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                      const SmithLangInstance *instance, uint32_t id, uint32_t location) {
    smith_lang_field_arrays.instance = id;
    smith_lang_field_arrays.count = 0;
    smith_lang_field_aliases.count = 0;
    uint32_t first = generic->first + generic->body_index;
    uint32_t end = generic->first + generic->count;
    for (uint32_t at = first; at < end; at++) { // the tokens of the body
        uint32_t len = smith_lang_fields_call_len(at, end);
        int use = len == 0 || smith_lang_cx_token(at)->special != SmithLangGen_Plain
                      ? 0
                      : smith_lang_fields_value_use(at, len, first, end);
        if (use == 0) continue;
        if (use == 1) { // the call of smith_field_make needs its definition
            if (!lang->field_defined) smith_lang_fields_define(lang, host, generic, location, id);
            continue;
        }
        SmithFieldInfo info;
        char message[smith_lang_message_max];
        if (!smith_lang_fields_load(host, generic, instance, at, location, 0, &info, message, sizeof message)) continue;
        bool known = false;
        for (size_t k = 0; k < smith_lang_field_arrays.count; k++)
            known = known || strcmp(smith_lang_field_arrays.spelling[k], smith_lang_fields_spelling) == 0;
        if (known || smith_lang_field_arrays.count >= 16) continue;
        if (!lang->field_defined) smith_lang_fields_define(lang, host, generic, location, id);
        smith_lang_fields_array(lang, host, generic, instance, at, id, location, smith_lang_field_arrays.count++);
    }
}

// ---- A `Field` in a condition

// What the `fields(T)[i]` at `at` is as an expression for the compiler: tokens of a call of `smith_field_make`, as many
// as the group takes. 0 when the tokens are no such group, UINT32_MAX when it cannot be read.
static uint32_t smith_lang_fields_cx_piece(const SmithLangCx *cx, uint32_t at, uint32_t last, char *piece,
                                           size_t capacity) {
    const SmithLangGenToken *token = smith_lang_cx_token(at);
    if (token->kind == SmithTokenKind_Identifier && token->special == SmithLangGen_Plain && token->param == 0) {
        for (size_t i = smith_lang_field_aliases.count; i > 0; i--) { // the newest first
            if (!smith_lang_cx_word(token, smith_lang_field_aliases.name[i - 1])) continue;
            snprintf(piece, capacity, "(\x1f%s\x1f)", smith_lang_field_aliases.text[i - 1]);
            return 1;
        }
    }
    uint32_t length = smith_lang_fields_call_len(at, last);
    if (length == 0 || !smith_lang_fields_punct(at + length, last, "[") || cx->replay == nullptr ||
        cx->instance == nullptr) {
        return 0;
    }
    uint32_t after = smith_lang_cx_skip_group(at + length, last);
    static SmithLangCx evaluation;
    evaluation = *cx;
    int64_t index = 0;
    if (after == UINT32_MAX || !smith_lang_cx_simple(&evaluation, at + length + 1, after - 1, &index) || index < 0) {
        return UINT32_MAX;
    }
    SmithFieldInfo info;
    char message[smith_lang_message_max];
    uint64_t handle = 0;
    if (!smith_lang_fields_load(cx->host, cx->replay->generic, cx->instance, at, cx->location, (size_t)index, &info,
                                message, sizeof message) ||
        (uint64_t)index >= info.count || !smith_lang_ty_handle(cx->lang, cx->host, info.type, cx->location, &handle)) {
        return UINT32_MAX;
    }
    const char *name = info.tuple ? info.name + 1 : info.name;
    char maker[smith_lang_name_max];
    smith_lang_fields_maker_name(cx->lang, maker, sizeof maker);
    snprintf(piece, capacity, "%s\x1f(\x1f\"%s\"\x1f,\x1f%zu\x1f,\x1f%lluULL\x1f)", maker, name, strlen(name),
             (unsigned long long)handle);
    return after - at;
}

// At a `constexpr` of the replay: `constexpr Field f = E;` or `constexpr auto f = E;` makes `f` stand for E.
static void smith_lang_fields_alias(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                    const SmithLangFrame *frame) {
    uint32_t at = frame->pos;
    uint32_t limit = frame->end;
    const SmithLangGenToken *kind = smith_lang_cx_token(at + 1);
    if (at + 5 >= limit || !smith_lang_cx_word(smith_lang_cx_token(at), "constexpr") ||
        !(smith_lang_cx_word(kind, "Field") || smith_lang_cx_word(kind, "auto")) ||
        smith_lang_cx_token(at + 2)->kind != SmithTokenKind_Identifier ||
        !smith_lang_fields_punct(at + 3, limit, "=")) {
        return;
    }
    uint32_t end = at + 4;
    for (size_t guard = 0; end < limit && guard < smith_lang_gt_max; guard++) { // to the `;` outside any group
        const SmithLangGenToken *part = smith_lang_cx_token(end);
        if (smith_lang_gt_punct(part, ";")) break;
        uint32_t after = smith_lang_gt_open(part) ? smith_lang_cx_skip_group(end, limit) : end + 1;
        if (after == UINT32_MAX) return;
        end = after;
    }
    if (end >= limit || end == at + 4 || smith_lang_field_aliases.count >= 16) return;
    static SmithLangCx evaluation;
    evaluation = (SmithLangCx){
        .lang = lang,
        .host = host,
        .instance = replay->instance,
        .replay = replay,
        .location = smith_lang_gen_place(host, replay->generic, smith_lang_cx_token(at), replay->location)};
    char text[1024];
    if (!smith_lang_cx_text(&evaluation, at + 4, end, text, sizeof text)) return;
    size_t index = smith_lang_field_aliases.count++;
    snprintf(smith_lang_field_aliases.name[index], sizeof smith_lang_field_aliases.name[index], "%s",
             smith_lang_gt_str(smith_lang_cx_token(at + 2)));
    snprintf(smith_lang_field_aliases.text[index], sizeof smith_lang_field_aliases.text[index], "%s", text);
}

// Whether the tokens [first, last) are a `Field`'s member read as a type (`f.type`, `fields(T)[i].type`): they begin
// with a variable that stands for a `Field`, or with `fields(`.
static bool smith_lang_fields_valued(uint32_t first, uint32_t last) {
    const SmithLangGenToken *token = smith_lang_cx_token(first);
    if (last <= first + 1 || token->special != SmithLangGen_Plain) return false;
    if (smith_lang_fields_call_len(first, last) != 0) return true;
    for (size_t i = 0; i < smith_lang_field_aliases.count; i++) { // at most 16
        if (token->param == 0 && smith_lang_cx_word(token, smith_lang_field_aliases.name[i])) return true;
    }
    return false;
}

// The spelling of the type that the handle the tokens [first, last) evaluate to names.
static bool smith_lang_fields_valued_type(SmithLangCx *cx, uint32_t first, uint32_t last, char *spelling,
                                          size_t capacity) {
    static SmithValue nodes[smith_lang_cx_value_nodes];
    static SmithValueQuery query;
    char text[2048];
    if (cx->host->value_query == nullptr) {
        return smith_lang_cx_fail(cx, "a type read from a `Field` needs smith's compiler; the raw lowering has none");
    }
    if (!smith_lang_cx_text(cx, first, last, text, sizeof text)) return smith_lang_cx_fail(cx, "it is too long");
    query = (SmithValueQuery){.nodes = nodes, .capacity = smith_lang_cx_value_nodes};
    if (!cx->host->value_query(cx->host->context, text, (uint32_t)strlen(text), &query)) {
        return smith_lang_cx_fail(cx, "%s", query.why != nullptr ? query.why : "the compiler cannot evaluate it");
    }
    const char *known =
        query.root.kind == SmithValue_Int ? smith_lang_ty_spelling_of(cx->lang, query.root.magnitude) : nullptr;
    if (known == nullptr) return smith_lang_cx_fail(cx, "the value is not a type");
    snprintf(spelling, capacity, "%s", known);
    return true;
}
