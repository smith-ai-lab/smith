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
    int8_t type_index; // which `type T` it is (-1: an ordinary parameter)
} SmithLangParam;

typedef struct {
    uint32_t generic;
    uint32_t arg_first[smith_lang_type_params_max]; // the type arguments, lowered, in smith_lang_it
    uint32_t arg_count[smith_lang_type_params_max];
    SmithLangText key;
    SmithLangText cname;
    SmithLangText label;
    SmithLangText description;
    uint32_t call_location;
    uint32_t depth;
    uint32_t parent; // 1 + the instance whose body asked for it (0: the file's own code)
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
    for (size_t k = 0; k < generic->type_count; k++) { // bounded by the parameters
        const SmithLangGenToken *token = &smith_lang_gt.tokens[generic->first + generic->type_token[k]];
        if (token->length == length && memcmp(smith_lang_gt_str(token), name, length) == 0) return (int)k;
    }
    return -1;
}

static bool smith_lang_is_type_param(const SmithLang *lang, const char *name, size_t length) {
    return lang->recording && smith_lang_param_named(&smith_lang_generics[lang->recording_generic], name, length) >= 0;
}

// A token that names the T of the generic being recorded is marked, unless it is a member (`x.T`).
static void smith_lang_capture_mark(SmithLang *lang, uint32_t index) {
    const SmithLangGeneric *generic = &smith_lang_generics[lang->recording_generic];
    SmithLangGenToken *token = &smith_lang_gt.tokens[index];
    int found = smith_lang_param_named(generic, smith_lang_gt_str(token), token->length);
    if (found < 0) return;
    if (index > generic->first) {
        const SmithLangGenToken *before = &smith_lang_gt.tokens[index - 1];
        if (smith_lang_gt_punct(before, ".") || smith_lang_gt_punct(before, "->")) return;
    }
    token->param = (uint8_t)(found + 1);
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
        out[count++] = (SmithLangParam){start, at, -1};
        start = at + 1;
    }
    for (size_t i = 0; i < count; i++) { // `constexpr type T`
        const SmithLangGenToken *first = &smith_lang_gt.tokens[out[i].start];
        if (smith_lang_param_is_type(&out[i]))
            out[i].type_index = (int8_t)smith_lang_param_named(generic, smith_lang_gt_str(first + 2), first[2].length);
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

// Reads ahead, to the end of the parameter list of the function the declaration begins with: is there a `type T` in it.
// The tokens stay queued; nothing is changed.
static bool smith_lang_generic_ahead(SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    if (!lang->is_module || !host->maybe_generic || lang->broken) return false;
    int depth = 0;
    bool in_params = false;
    const SmithToken *before = first;
    for (size_t index = 0; index < smith_lang_generic_scan_max; index++) { // bounded: a head is short
        const SmithToken *token = smith_lang_peek(lang, host, index, false);
        if (token->kind == SmithTokenKind_Eof) return false;
        if (token->kind == SmithTokenKind_Punctuator && token->length == 1) {
            char c = token->text[0];
            if (c == '(') {
                in_params = in_params || (depth == 0 && !smith_lang_group_keyword(before));
                depth += 1;
            } else if (c == ')') {
                depth -= 1;
                if (depth == 0 && in_params) return false; // the parameter list ended without one
            } else if (depth == 0 && (c == ';' || c == '=' || c == '{' || c == ',')) {
                return false;
            }
        }
        bool comes_after_separator =
            smith_lang_punct(before, "(") || smith_lang_punct(before, ",") || smith_lang_ident(before, "constexpr");
        if (in_params && depth == 1 && smith_lang_ident(token, "type") && comes_after_separator &&
            smith_lang_type_parameter_at(lang, host, index)) {
            return host->lookup(host->context, "type", 4, SmithLookup_Type).kind == SmithLookup_None;
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

// The type parameters of the head just recorded: `constexpr type T`, their names and which parameter each is. A
// message, or null.
static const char *smith_lang_generic_scan(SmithLangGeneric *generic) {
    SmithLangParam params[smith_lang_call_params_max];
    size_t count = smith_lang_generic_params(generic, params, smith_lang_call_params_max);
    if (count == SIZE_MAX) return "a generic function has at most 16 parameters";
    generic->param_count = (uint8_t)count;
    generic->type_count = 0;
    for (size_t i = 0; i < count; i++) { // the `type T` parameters
        const SmithLangGenToken *first = &smith_lang_gt.tokens[params[i].start];
        if (smith_lang_param_is_bare_type(&params[i]))
            return "a type parameter is compile-time: write `constexpr type T`";
        if (!smith_lang_param_is_type(&params[i])) continue;
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
    return generic->type_count == 0 ? "a generic function needs a `constexpr type T` parameter" : nullptr;
}

static bool smith_lang_held_is_inline(const SmithLangHeld *held) {
    const char *text = smith_lang_scratch_str(held->text);
    return held->kind == SmithTokenKind_Identifier && (smith_lang_equals(text, held->text.length, "inline") ||
                                                       smith_lang_equals(text, held->text.length, "__inline") ||
                                                       smith_lang_equals(text, held->text.length, "__inline__"));
}

// Pushes the head's tokens into the pool; false when it is full.
static bool smith_lang_generic_push_head(SmithLang *lang, SmithLangGeneric *generic) {
    uint32_t pushed = 0;
    for (size_t i = 0; i < lang->head_count; i++) { // bounded by the head
        const SmithLangHeld *held = &lang->head[i];
        if (smith_lang_held_is_inline(held)) continue;
        if (i == lang->decl.last_name) generic->name_index = pushed;
        SmithToken token = smith_lang_held_token(lang, held);
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
    if (lang->decl.storage)
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
    if (problem == nullptr) {
        *generic = (SmithLangGeneric){0};
        generic->first = smith_lang_gt.count;
        if (!smith_lang_generic_push_head(lang, generic) || !smith_lang_generic_parens(generic)) {
            problem = "a generic is a function with a parameter list after its name";
        } else {
            problem = smith_lang_generic_scan(generic);
        }
    }
    if (problem != nullptr) {
        smith_lang_error(lang, host, location, "%s", problem);
        if (smith_lang_gt.count > generic->first && generic->first >= smith_lang_gt.count - lang->head_count) {
            smith_lang_gt.count = generic->first;
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
    snprintf(message, sizeof message, "\"%s::%s is a generic function: call it from a .smith module\"",
             smith_lang_str(generic->module), smith_lang_str(generic->member));
    SmithToken like = smith_lang_synth(SmithTokenKind_Identifier, "", 0, &(SmithToken){0});
    like.location = name->location;
    like.line_start = true;
    static const SmithLangPiece before[] = {
        {SmithTokenKind_Punctuator, "["},           {SmithTokenKind_Punctuator, "["},
        {SmithTokenKind_Identifier, "gnu"},         {SmithTokenKind_Punctuator, "::"},
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
    used = smith_lang_append(out, capacity, used, "%c %u %u %u %u |", generic->is_inline ? 'i' : '-',
                             generic->name_index, generic->open_index, generic->close_index, generic->body_index);
    for (uint32_t i = 0; i < generic->count; i++) { // the head, then the body
        const SmithLangGenToken *token = &smith_lang_gt.tokens[generic->first + i];
        used = smith_lang_append(out, capacity, used, " %c", smith_lang_kind_letter(token));
        if (token->param != 0) used = smith_lang_append(out, capacity, used, "P%u", token->param);
        if ((token->flags & SmithLangGenFlag_Expand) != 0) used = smith_lang_append(out, capacity, used, "x");
        if ((token->flags & SmithLangGenFlag_Space) != 0) used = smith_lang_append(out, capacity, used, "w");
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
        if (generic->own && generic->exported) used = smith_lang_generic_serialize(generic, out, capacity, used);
    }
    return used;
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
    *out = (SmithLangGenToken){0};
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
    default:
        return false;
    }
    for (; at < length && word[at] != '@'; at++) { // the flags
        if (word[at] == 'P' && at + 1 < length) {
            out->param = (uint8_t)(word[++at] - '0');
        } else if (word[at] == 'x') {
            out->flags |= SmithLangGenFlag_Expand;
        } else if (word[at] == 'w') {
            out->flags |= SmithLangGenFlag_Space;
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
    while (at < end) { // each token
        while (at < end && *at == ' ')
            at += 1;
        const char *start = at;
        while (at < end && *at != ' ')
            at += 1;
        if (at == start) break;
        SmithLangGenToken token;
        size_t length = 0;
        if (!smith_lang_generic_read_token(start, (size_t)(at - start), &token, text, sizeof text, &length) ||
            smith_lang_pool_add(&smith_lang_gt, token, text, length) == UINT32_MAX) {
            return false;
        }
    }
    generic->count = smith_lang_gt.count - generic->first;
    return generic->count > 0;
}

static int smith_lang_generic_find(const SmithLang *lang, const char *module, size_t module_length, const char *member,
                                   size_t member_length) {
    for (size_t i = 0; i < lang->generics_count; i++) { // the generics known
        const SmithLangGeneric *generic = &smith_lang_generics[i];
        if (generic->module.length == module_length && generic->member.length == member_length &&
            memcmp(smith_lang_str(generic->module), module, module_length) == 0 &&
            memcmp(smith_lang_str(generic->member), member, member_length) == 0) {
            return (int)i;
        }
    }
    return -1;
}

// Reads the generics a header lists (`// smith-generic:` lines), once for each module.
static void smith_lang_generics_read(SmithLang *lang, const char *text, size_t length) {
    size_t marker = sizeof smith_lang_generic_marker - 1;
    for (size_t at = 0; at + marker < length;) { // each line
        const char *newline = memchr(text + at, '\n', length - at);
        size_t end = newline != nullptr ? (size_t)(newline - text) : length;
        if (end - at > marker && memcmp(text + at, smith_lang_generic_marker, marker) == 0 && lang->cap_frames == 0 &&
            lang->generics_count < smith_lang_generics_max) {
            SmithLangGenericLine parsed = {0};
            SmithLangGeneric *generic = &smith_lang_generics[lang->generics_count];
            *generic = (SmithLangGeneric){0};
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
                generic->is_inline = parsed.words[4][0] == 'i';
                generic->name_index = (uint32_t)strtoul(parsed.words[5], nullptr, 10);
                generic->open_index = (uint32_t)strtoul(parsed.words[6], nullptr, 10);
                generic->close_index = (uint32_t)strtoul(parsed.words[7], nullptr, 10);
                generic->body_index = (uint32_t)strtoul(parsed.words[8], nullptr, 10);
                generic->exported = true;
                good = smith_lang_generic_scan(generic) == nullptr;
            }
            if (good && smith_lang_generic_find(lang, words[0], strlen(words[0]), words[1], strlen(words[1])) < 0) {
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
static void smith_lang_gen_put(SmithLang *lang, const SmithHost *host, SmithTokenKind kind, const char *text,
                               uint32_t location, uint32_t instance) {
    SmithToken token = {0};
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
    written.line_start = false;
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
        name[used++] = word ? c : c == '*' ? 'P' : c == ' ' ? '_' : c == '\x1f' ? '_' : 'x';
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

typedef struct {
    char canonical[smith_lang_type_params_max][256];
    char written[smith_lang_type_params_max][256];
    char spelling[smith_lang_type_params_max][256];
} SmithLangTypeNames;

// How the parser and the source name each type argument: the first is the identity of the type.
static void smith_lang_instance_names(const SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                      const uint32_t first[], const uint32_t count[], SmithLangTypeNames *names) {
    for (size_t k = 0; k < generic->type_count; k++) { // the type arguments
        smith_lang_it_spelling(first[k], count[k], names->spelling[k], sizeof names->spelling[k]);
        smith_lang_it_written(lang, first[k], count[k], names->written[k], sizeof names->written[k]);
        SmithTypeQuery query = {0};
        if (host->type_query != nullptr &&
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

// The string that is the instance's symbol: the module, the generic and the canonical types.
static void smith_lang_instance_label(const SmithLangGeneric *generic, const SmithLangTypeNames *names, char *out,
                                      size_t capacity) {
    int used = snprintf(out, capacity, "\"%s%s::%s(", smith_lang_symbol_underscore, smith_lang_str(generic->module),
                        smith_lang_str(generic->member));
    for (size_t k = 0; k < generic->type_count; k++) { // the types, canonical, in a string
        if (k > 0) used += snprintf(out + used, capacity - (size_t)used, ", ");
        for (const char *c = names->canonical[k]; *c != 0 && (size_t)used + 4 < capacity; c++) {
            if (*c == '"' || *c == '\\') out[used++] = '\\';
            out[used++] = *c;
        }
    }
    snprintf(out + used, capacity - (size_t)used, ")\"");
}

// The note that names an instance: `arena::push(Point)`.
static void smith_lang_instance_description(const SmithLangGeneric *generic, const SmithLangTypeNames *names, char *out,
                                            size_t capacity) {
    int used = snprintf(out, capacity, "%s::%s(", smith_lang_str(generic->module), smith_lang_str(generic->member));
    for (size_t k = 0; k < generic->type_count; k++) // the types, as written
        used += snprintf(out + used, capacity - (size_t)used, "%s%s", k > 0 ? ", " : "", names->written[k]);
    snprintf(out + used, capacity - (size_t)used, ")");
}

// Makes the instance (its types are staged in smith_lang_it) and tells the lowering the name the source gives it.
static int smith_lang_instance_make(SmithLang *lang, uint32_t generic_index, const uint32_t first[],
                                    const uint32_t count[], const SmithLangTypeNames *names, const char *key,
                                    uint32_t location) {
    const SmithLangGeneric *generic = &smith_lang_generics[generic_index];
    SmithLangInstance *instance = &smith_lang_instances[lang->instances_count];
    *instance = (SmithLangInstance){0};
    instance->generic = generic_index;
    for (size_t k = 0; k < generic->type_count; k++) { // the types
        instance->arg_first[k] = first[k];
        instance->arg_count[k] = count[k];
    }
    char spellings[smith_lang_type_params_max * 257] = "";
    for (size_t k = 0; k < generic->type_count; k++) // `Point`, or `Point\x1fu32`
        snprintf(spellings + strlen(spellings), sizeof spellings - strlen(spellings), "%s%s", k > 0 ? "\x1f" : "",
                 names->written[k]);
    char label[smith_lang_message_max];
    char description[smith_lang_message_max];
    smith_lang_instance_label(generic, names, label, sizeof label);
    smith_lang_instance_description(generic, names, description, sizeof description);
    instance->key = smith_lang_store(lang, key, strlen(key));
    instance->cname = smith_lang_instance_cname(lang, generic, spellings);
    instance->label = smith_lang_store(lang, label, strlen(label));
    instance->description = smith_lang_store(lang, description, strlen(description));
    instance->call_location = location;
    instance->depth = lang->instance_depth + 1;
    instance->parent = lang->instance_current;
    const char *member = description + generic->module.length + 2; // `push(Point)`
    smith_lang_written_add(lang, smith_lang_str(instance->cname), instance->cname.length, description,
                           strlen(description), 0, generic->module, true, member, strlen(member));
    lang->instances_count += 1;
    return (int)(lang->instances_count - 1);
}

// Asks for the instance of a generic for the types staged in smith_lang_it (at `first` and `count`; the pool was at
// `mark` before them). The instance made earlier for the same types is used when there is one, and the staged tokens
// are taken out. Returns the instance, or -1 after the error.
static int smith_lang_instance_request(SmithLang *lang, const SmithHost *host, uint32_t generic_index,
                                       const uint32_t first[], const uint32_t count[], const uint32_t mark[2],
                                       uint32_t location) {
    const SmithLangGeneric *generic = &smith_lang_generics[generic_index];
    SmithLangTypeNames names;
    smith_lang_instance_names(lang, host, generic, first, count, &names);
    char key[smith_lang_type_params_max * 257] = "";
    for (size_t k = 0; k < generic->type_count; k++) // the identity: the canonical spellings
        snprintf(key + strlen(key), sizeof key - strlen(key), "%s%s", k > 0 ? "\x1f" : "", names.canonical[k]);
    int found = smith_lang_instance_find(lang, generic_index, key, strlen(key));
    if (found >= 0) {
        smith_lang_it.count = mark[0]; // the staged tokens are kept only for a new instance
        smith_lang_it.text_used = mark[1];
        return found;
    }
    if (lang->instances_count >= smith_lang_instances_max || lang->instance_depth + 1 > smith_lang_instance_depth_max) {
        smith_lang_error(lang, host, location,
                         "generic instances nest more than %zu deep, or there are more than %zu (does a generic ask "
                         "for itself with a bigger type?)",
                         smith_lang_instance_depth_max, smith_lang_instances_max);
        smith_lang_it.count = mark[0];
        smith_lang_it.text_used = mark[1];
        return -1;
    }
    return smith_lang_instance_make(lang, generic_index, first, count, &names, key, location);
}

// ---- Writing a signature and a call
// --------------------------------------------------------------------------------------

constexpr size_t smith_lang_replay_depth_max = 256;
constexpr size_t smith_lang_replay_steps_max = (size_t)1 << 24;

typedef struct {
    uint32_t start; // [start, end) of smith_lang_gt
    uint32_t end;
} SmithLangSpan;

// A type argument in place of its T: its tokens, as a `__typeof__` when it has parentheses (a function pointer's).
static void smith_lang_gen_put_arg(SmithLang *lang, const SmithHost *host, const SmithLangInstance *instance,
                                   unsigned param, uint32_t instance_id) {
    uint32_t first = instance->arg_first[param];
    uint32_t count = instance->arg_count[param];
    bool wrap = smith_lang_it_has_paren(first, count);
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

// A token of a generic with the type arguments of `instance` in place of T (none: as it is).
static void smith_lang_gen_put_subst(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                     const SmithLangInstance *instance, SmithLangGenToken *token, uint32_t fallback,
                                     uint32_t instance_id, bool real) {
    if (token->param != 0 && instance != nullptr) {
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

// `R name(P)` for an instance: the head with the type arguments for T, the instance's name, and the parameters that are
// no `type`. `attributes` keeps the head's own leading attributes (a definition has them, a declaration cannot).
static void smith_lang_gen_signature(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic,
                                     const SmithLangInstance *instance, bool attributes, uint32_t location,
                                     uint32_t instance_id) {
    SmithLangGenToken *tokens = &smith_lang_gt.tokens[generic->first];
    for (uint32_t at = attributes ? 0 : smith_lang_generic_skip_attributes(generic); at < generic->name_index; at++)
        smith_lang_gen_put_subst(lang, host, generic, instance, &tokens[at], location, instance_id, false);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, smith_lang_str(instance->cname), location, instance_id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "(", location, instance_id);
    SmithLangParam params[smith_lang_call_params_max];
    size_t count = smith_lang_generic_params(generic, params, smith_lang_call_params_max);
    size_t written = 0;
    for (size_t i = 0; i < count && count != SIZE_MAX; i++) { // the parameters that are no `type T`
        if (params[i].type_index >= 0) continue;
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
        SmithToken name = {0};
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

typedef struct {
    uint32_t pos; // [pos, end) of smith_lang_gt, or a punctuator to write
    uint32_t end;
    char literal;
} SmithLangFrame;

typedef struct {
    SmithLangFrame frames[smith_lang_replay_depth_max];
    size_t count;
    const SmithLangInstance *instance; // whose type arguments are written for T (none: the file's own code)
    uint32_t instance_id;
    const SmithLangGeneric *generic; // whose tokens these are
    uint32_t location;               // of the call that asked for the instance
} SmithLangReplay;

static SmithLangReplay smith_lang_replay;

static bool smith_lang_replay_push(SmithLangReplay *replay, uint32_t pos, uint32_t end, char literal) {
    if (replay->count >= smith_lang_replay_depth_max) return false;
    replay->frames[replay->count++] = (SmithLangFrame){pos, end, literal};
    return true;
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

// A call to a generic: `args` is the `( ... )` with its parentheses, in smith_lang_gt at [first, last). The types are
// taken out of the arguments and asked for, and the start of the call is written; what follows it (the other arguments
// and the end) is left in the replay for it to write.
static bool smith_lang_call_finish(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                   uint32_t generic_index, uint32_t first, uint32_t last, uint32_t location,
                                   uint32_t real) {
    const SmithLangGeneric *generic = &smith_lang_generics[generic_index];
    SmithLangSpan spans[smith_lang_call_params_max];
    size_t count = smith_lang_split_args(first + 1, last - 1, spans, smith_lang_call_params_max);
    const char *name = smith_lang_str(generic->member);
    if (count == SIZE_MAX || count != generic->param_count) {
        if (count != SIZE_MAX && count + generic->type_count == generic->param_count) {
            smith_lang_error(lang, host, location,
                             "`%s` is called without its type: write it as the argument that "
                             "takes `constexpr type T`",
                             name);
        } else {
            smith_lang_error(lang, host, location, "`%s` takes %u arguments", name, generic->param_count);
        }
        return false;
    }
    uint32_t mark[2] = {smith_lang_it.count, smith_lang_it.text_used};
    uint32_t type_first[smith_lang_type_params_max];
    uint32_t type_count[smith_lang_type_params_max];
    for (size_t k = 0; k < generic->type_count; k++) { // the types
        SmithLangSpan span = spans[generic->type_position[k]];
        if (span.start == span.end ||
            !smith_lang_stage_type(span, replay->instance, location, &type_first[k], &type_count[k])) {
            smith_lang_error(lang, host, location, "the type argument of `%s` is missing or too long", name);
            smith_lang_it.count = mark[0];
            smith_lang_it.text_used = mark[1];
            return false;
        }
    }
    int index = smith_lang_instance_request(lang, host, generic_index, type_first, type_count, mark, location);
    if (index < 0) return false;
    smith_lang_gen_call_start(lang, host, generic, &smith_lang_instances[index], location, real, replay->instance_id);
    static const char closers[] = {')', '}', ';', ')'}; // written in the reverse, last first
    for (size_t i = 0; i < sizeof closers; i++)         // the end of the statement expression
        smith_lang_replay_push(replay, 0, 0, closers[i]);
    bool more = false;
    for (size_t i = count; i > 0; i--) { // the arguments that are no type, last first
        bool is_type = false;
        for (size_t k = 0; k < generic->type_count; k++)
            is_type = is_type || generic->type_position[k] == i - 1;
        if (is_type) continue;
        if (more) smith_lang_replay_push(replay, 0, 0, ',');
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

// A call that was kept as a group in a generic's body, now that its types are known: to a generic, it becomes the call
// of the instance; to anything else it is the plain call it always was.
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
    if (found >= 0 &&
        smith_lang_call_finish(lang, host, replay, (uint32_t)found, begin + 1, end, location, UINT32_MAX)) {
        return;
    }
    char callee[smith_lang_name_max];
    smith_lang_member_name(text, module_length, colons + 1, member_length, callee, sizeof callee);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, callee, location, replay->instance_id);
    smith_lang_replay_push(replay, begin + 1, end, 0);
}

// Writes the tokens the replay holds, in order. A group becomes its call; a token that names T becomes the type.
static void smith_lang_replay_run(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay) {
    for (size_t step = 0; replay->count > 0 && step < smith_lang_replay_steps_max; step++) { // bounded
        SmithLangFrame *frame = &replay->frames[replay->count - 1];
        if (frame->literal != 0) {
            char text[2] = {frame->literal, 0};
            replay->count -= 1;
            smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, text, replay->location, replay->instance_id);
            continue;
        }
        if (frame->pos >= frame->end) {
            replay->count -= 1;
            continue;
        }
        SmithLangGenToken *token = &smith_lang_gt.tokens[frame->pos];
        if (token->special == SmithLangGen_CallBegin) {
            smith_lang_replay_group(lang, host, replay, frame->pos);
            continue;
        }
        frame->pos += 1;
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
                       smith_lang_str(instance->description));
    }
    lang->carry_line_start = true;
    smith_lang_gen_note_break(lang);
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
    *replay = (SmithLangReplay){0};
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

static void smith_lang_process(SmithLang *lang, const SmithHost *host, const SmithToken *token);

// Takes the tokens of a call from its `(` to the matching `)`, each lowered as it is taken (what is written goes to
// the pool). False when the file ends first.
static bool smith_lang_generic_read_args(SmithLang *lang, const SmithHost *host, uint32_t location) {
    int depth = 0;
    for (size_t guard = 0; guard < smith_lang_gt_max; guard++) { // bounded by the pool
        SmithToken token = smith_lang_take(lang, host);
        if (token.kind == SmithTokenKind_Eof) {
            smith_lang_error(lang, host, location, "this call is never closed with `)`");
            smith_lang_process(lang, host, &token);
            return false;
        }
        bool open =
            token.kind == SmithTokenKind_Punctuator && token.length == 1 && strchr("([{", token.text[0]) != nullptr;
        bool close =
            token.kind == SmithTokenKind_Punctuator && token.length == 1 && strchr(")]}", token.text[0]) != nullptr;
        smith_lang_process(lang, host, &token);
        depth += open ? 1 : close ? -1 : 0;
        if (depth <= 0) return true;
    }
    return false;
}

// Pushes a token of the lowering's own into the pool (a group's start or end).
static void smith_lang_gen_mark(SmithLang *lang, SmithLangGenSpecial special, const char *text, size_t length,
                                uint32_t location) {
    SmithLangGenToken token = {0};
    token.kind = SmithTokenKind_Identifier;
    token.special = (uint8_t)special;
    token.location = location;
    if (smith_lang_pool_add(&smith_lang_gt, token, text, length) == UINT32_MAX) lang->broken = true;
}

// After a call: the tokens before the next one are those of an expression that ended.
static void smith_lang_generic_call_done(SmithLang *lang) {
    lang->prev_char = ')';
    lang->prev_member = false;
    lang->prev_tag = false;
    lang->typeish = false;
    lang->prev_name_length = 0;
    lang->namespace_done = true;
}

// In a generic's body: the call, kept whole between a start and an end, to be understood where the generic is written.
static bool smith_lang_generic_record_call(SmithLang *lang, const SmithHost *host, const char *module,
                                           size_t module_length, const char *member, size_t member_length,
                                           uint32_t location) {
    char text[smith_lang_name_max];
    int length = snprintf(text, sizeof text, "%.*s::%.*s", (int)module_length, module, (int)member_length, member);
    smith_lang_gen_mark(lang, SmithLangGen_CallBegin, text, (size_t)length, location);
    bool good = smith_lang_generic_read_args(lang, host, location);
    smith_lang_gen_mark(lang, SmithLangGen_CallEnd, "", 0, location);
    smith_lang_generic_call_done(lang);
    return good;
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
    uint32_t frame_count = smith_lang_gt.count;
    uint32_t frame_text = smith_lang_gt.text_used;
    lang->cap_frames += 1;
    bool good = smith_lang_generic_read_args(lang, host, location);
    lang->cap_frames -= 1;
    if (!good) {
        smith_lang_gt.count = frame_count;
        smith_lang_gt.text_used = frame_text;
        return true;
    }
    uint32_t top_count = smith_lang_gt.count;
    uint32_t top_text = smith_lang_gt.text_used;
    SmithLangReplay *replay = &smith_lang_replay;
    *replay = (SmithLangReplay){.instance = nullptr, .instance_id = 0, .generic = nullptr, .location = location};
    if (smith_lang_call_finish(lang, host, replay, generic_index, frame_count, top_count, location, member_location)) {
        smith_lang_replay_run(lang, host, replay);
    }
    if (lang->cap_frames > 0) {
        smith_lang_gt_slide(frame_count, frame_text, top_count, top_text); // inside another call: where this one was
    } else {
        smith_lang_gt.count = frame_count;
        smith_lang_gt.text_used = frame_text;
    }
    smith_lang_generic_call_done(lang);
    return true;
}

// The call that `name` begins, when it is to a generic (or, in a generic's body, to anything of a module). Returns
// false when it is not one and the name is to be written as any other.
static bool smith_lang_generic_call_core(SmithLang *lang, const SmithHost *host, const char *module,
                                         size_t module_length, const char *member, size_t member_length,
                                         uint32_t location, int generic_index) {
    if (generic_index < 0 && !lang->recording) return false;
    const SmithToken *open = smith_lang_peek(lang, host, 0, false);
    if (!smith_lang_punct(open, "(")) {
        if (generic_index < 0) return false;
        smith_lang_error(lang, host, location, "`%.*s` is a generic function: it can only be called, with its type",
                         (int)member_length, member);
        smith_lang_generic_call_done(lang);
        return true;
    }
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
static void smith_lang_generic_private_name(SmithLang *lang, const SmithHost *host, const SmithLangName *known,
                                            const char *name, size_t length, uint32_t location) {
    if (!lang->decl.exported || known->exported || known->generic != 0) return;
    smith_lang_error(lang, host, location,
                     "the body of an exported generic names `%.*s`, which is private; instances are compiled where the "
                     "generic is called, so export it",
                     (int)length, name);
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
}
