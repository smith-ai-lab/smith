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
static bool smith_lang_cx_simple(SmithLangCx *cx, uint32_t first, uint32_t last, int64_t *result);
static int smith_lang_param_type_of(const SmithLangGeneric *generic, const SmithLangInstance *instance,
                                    SmithLangSpan span);

constexpr size_t smith_lang_fields_pieces_max = 40;

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

// The index after `typeof ( ... . [ fields(T)[i] ] )` at `at`, or 0 when the tokens are not that: the type of a field.
static uint32_t smith_lang_fields_typeof_end(uint32_t at, uint32_t limit) {
    const SmithLangGenToken *token = smith_lang_cx_token(at);
    if (at + 5 >= limit || !(smith_lang_cx_word(token, "typeof") || smith_lang_cx_word(token, "__typeof__")) ||
        !smith_lang_fields_punct(at + 1, limit, "(")) {
        return 0;
    }
    uint32_t after = smith_lang_cx_skip_group(at + 1, limit);
    if (after == UINT32_MAX || after < at + 8 || !smith_lang_fields_punct(after - 2, limit, "]")) return 0;
    uint32_t open = after - 2; // the `[` that matches this `]`
    int depth = 0;
    for (uint32_t i = after - 2; i > at + 1; i--) { // bounded by the group
        const SmithLangGenToken *part = smith_lang_cx_token(i);
        depth += smith_lang_gt_close(part) ? 1 : smith_lang_gt_open(part) ? -1 : 0;
        if (depth == 0) {
            open = i;
            break;
        }
    }
    bool member = open > at + 2 &&
                  (smith_lang_fields_punct(open - 1, limit, ".") || smith_lang_fields_punct(open - 1, limit, "->"));
    if (!member || !smith_lang_fields_field_at(open + 1, limit)) return 0;
    return smith_lang_cx_skip_group(open + 1 + smith_lang_fields_call_len(open + 1, limit), limit) == after - 2 ? after
                                                                                                                : 0;
}

// The field in `typeof ( ... [ F ] )`, the tokens [first, last): 1 and the field, -1 and the message.
static int smith_lang_fields_in_typeof(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                       uint32_t place, uint32_t first, uint32_t last, SmithLangFieldRef *ref) {
    uint32_t close = last - 2; // the `]`
    uint32_t open = close;
    int depth = 0;
    for (uint32_t i = close; i > first + 1; i--) { // bounded by the group
        const SmithLangGenToken *part = smith_lang_cx_token(i);
        depth += smith_lang_gt_close(part) ? 1 : smith_lang_gt_open(part) ? -1 : 0;
        if (depth == 0) {
            open = i;
            break;
        }
    }
    return smith_lang_field_expr(lang, host, replay, open + 1, close, place, ref);
}

// The spelling of the type of the field in `typeof ( ... [ F ] )` at `first`..`last`, for the parser to read.
static bool smith_lang_cx_field_type(SmithLangCx *cx, uint32_t first, uint32_t last, char *spelling, size_t capacity) {
    SmithLangFieldRef ref;
    if (smith_lang_fields_in_typeof(cx->lang, cx->host, cx->replay, cx->location, first, last, &ref) != 1) {
        return smith_lang_cx_fail(cx, "%s", ref.message);
    }
    snprintf(spelling, capacity, "%s", ref.info.type);
    return true;
}

// Whether the tokens [first, last) use `fields`, so the arguments of a call that holds them are typed as the instance
// writes them (a member of a parameter, which is not declared while the body is written).
static bool smith_lang_fields_in(uint32_t first, uint32_t last) {
    for (uint32_t at = first; at < last; at++) { // bounded by the call
        if (smith_lang_fields_call_len(at, last) != 0) return true;
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
    if (smith_lang_fields_countof_at(at, limit) || smith_lang_fields_field_at(at, limit)) return true;
    if (smith_lang_fields_typeof_end(at, limit) != 0) return true;
    bool member = smith_lang_fields_punct(at, limit, ".") || smith_lang_fields_punct(at, limit, "->");
    if (member && smith_lang_fields_punct(at + 1, limit, "[") && smith_lang_fields_field_at(at + 2, limit)) {
        return true;
    }
    return token->kind == SmithTokenKind_Identifier && token->param != 0 &&
           smith_lang_fields_punct(at + 1, limit, ".") && smith_lang_fields_punct(at + 2, limit, "[") &&
           smith_lang_fields_field_at(at + 3, limit);
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
    int found = smith_lang_fields_in_typeof(use->lang, use->host, use->replay, use->place, use->at, end, &use->ref);
    if (found == 1 && !use->ref.info.bit_field) return 0;
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

// `fields(T)[i].name`: the name as a string literal.
static int smith_lang_fields_use_name(SmithLangFieldUse *use) {
    int found = smith_lang_field_expr(use->lang, use->host, use->replay, use->at, use->limit, use->place, &use->ref);
    use->consumed = use->ref.consumed;
    if (found != 1) return found;
    uint32_t after = use->at + use->ref.consumed;
    if (!smith_lang_fields_punct(after, use->limit, ".") ||
        !smith_lang_cx_word(smith_lang_cx_token(after + 1), "name")) {
        snprintf(use->ref.message, sizeof use->ref.message,
                 "`fields(T)[i]` is used as `fields(T)[i].name` (its name) or as `x.[fields(T)[i]]` (the member)");
        return -1;
    }
    use->consumed += 2;
    char text[smith_lang_name_max];
    snprintf(text, sizeof text, "\"%s\"", use->ref.info.name);
    smith_lang_fields_piece(use->pieces, SmithTokenKind_String, text);
    return 1;
}

// `.[F]`, `->[F]` or `T.[F]`: the member name after its operator.
static int smith_lang_fields_use_member(SmithLangFieldUse *use) {
    bool object = smith_lang_cx_token(use->at)->kind == SmithTokenKind_Identifier;
    uint32_t field_at = use->at + (object ? 3 : 2);
    int found = smith_lang_field_expr(use->lang, use->host, use->replay, field_at, use->limit, use->place, &use->ref);
    use->consumed = (field_at - use->at) + use->ref.consumed + 1;
    if (found != 1) return found;
    if (!smith_lang_fields_punct(field_at + use->ref.consumed, use->limit, "]")) {
        snprintf(use->ref.message, sizeof use->ref.message, "`.[` is closed by `]` right after the field");
        return -1;
    }
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
    else if (smith_lang_fields_field_at(at, limit))
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

// Writes the use of `fields` at the frame's position, if there is one: true when the tokens were taken.
static bool smith_lang_fields_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                     SmithLangFrame *frame) {
    static SmithLangFieldPieces pieces;
    uint32_t consumed = 0;
    int found = smith_lang_fields_use(lang, host, replay, frame->pos, frame->end, &pieces, &consumed);
    if (found == 0) return false;
    uint32_t place = smith_lang_gen_place(host, replay->generic, smith_lang_cx_token(frame->pos), replay->location);
    frame->pos += consumed;
    for (size_t i = 0; i < pieces.count; i++)
        smith_lang_gen_put(lang, host, pieces.kind[i], pieces.text[i], place, replay->instance_id);
    if (found < 0) smith_lang_generic_poison(lang, host, place);
    return true;
}
