// The lowering of a file that is not a module, as the clang hook runs it: the host offers only the identifiers it was
// told to watch (the contextual words, the sized type names, the import bindings) and the end of the file, so the plain
// C of a .smith file costs the compiler next to nothing. What the full path knows from every token (the token before,
// the brace depth) it asks of the host's record of the last few tokens, when a watched identifier needs it. A module is
// the full path: its `module` line asks the host for every token. Included by runtime/lang.c, after
// runtime/lang_head.c.

static void smith_lang_end_of_file(SmithLang *lang, const SmithHost *host, const SmithToken *token);
static void smith_lang_stream_open(SmithLang *lang, const SmithHost *host, const SmithToken *token);
static void smith_lang_stream_close(SmithLang *lang, const SmithHost *host, const SmithToken *token);

// The names the host should offer from the start of any .smith file.
void smith_lang_watch_basics(const SmithHost *host) {
    static const char *const words[] = {"module", "import", "export"};
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) // 3 entries
        host->watch(host->context, words[i], (uint32_t)strlen(words[i]));
    for (size_t i = 0; i < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; i++) // 13 entries
        host->watch(host->context, smith_lang_sized[i].name, (uint32_t)strlen(smith_lang_sized[i].name));
    // `if` and `else` are keywords, which a host offers only in its all-tokens mode.
    if (host->maybe_if_constexpr || host->maybe_tuple || host->maybe_type_value)
        host->watch_mode(host->context, SmithWatch_All);
}

// The token `back` places before the one being lowered (1 is the one before): this batch's own, then the host's record.
static bool smith_lang_previous(SmithLang *lang, const SmithHost *host, unsigned back, SmithToken *token) {
    if (back <= lang->recent_count) {
        *token = lang->recent[back - 1];
        return true;
    }
    return host->previous(host->context, back - (unsigned)lang->recent_count, token);
}

static void smith_lang_remember(SmithLang *lang, const SmithToken *token) {
    size_t keep = sizeof lang->recent / sizeof lang->recent[0];
    for (size_t i = keep - 1; i > 0; i--)
        lang->recent[i] = lang->recent[i - 1];
    lang->recent[0] = *token;
    if (lang->recent_count < keep) lang->recent_count += 1;
}

static bool smith_lang_previous_is(SmithLang *lang, const SmithHost *host, unsigned back, const char *punctuator) {
    SmithToken before;
    return smith_lang_previous(lang, host, back, &before) && smith_lang_punct(&before, punctuator);
}

// A declaration can start here: at the start of the file, after a `;`, or after a `}`.
static bool smith_lang_sparse_at_start(SmithLang *lang, const SmithHost *host) {
    SmithToken before;
    if (!smith_lang_previous(lang, host, 1, &before)) return true;
    return smith_lang_punct(&before, ";") || smith_lang_punct(&before, "}");
}

static bool smith_lang_sparse_member(SmithLang *lang, const SmithHost *host) {
    return smith_lang_previous_is(lang, host, 1, ".") || smith_lang_previous_is(lang, host, 1, "->");
}

// Inside [[ ]], where gnu::unused is no module path, or inside the arguments of __attribute__(( )).
static bool smith_lang_sparse_in_attribute(SmithLang *lang, const SmithHost *host) {
    unsigned closed = 0;                        // parentheses closed on the way back
    unsigned unmatched = 0;                     // parentheses open around the token
    for (unsigned back = 1; back < 8; back++) { // the host remembers eight
        SmithToken before;
        if (!smith_lang_previous(lang, host, back, &before)) return false;
        if (smith_lang_punct(&before, "]") || smith_lang_punct(&before, ";") || smith_lang_punct(&before, "{") ||
            smith_lang_punct(&before, "}")) {
            return false;
        }
        if (smith_lang_punct(&before, "[") && smith_lang_previous_is(lang, host, back + 1, "[")) return true;
        if (smith_lang_punct(&before, ")")) closed += 1;
        if (smith_lang_punct(&before, "(")) {
            if (closed > 0) {
                closed -= 1;
            } else {
                unmatched += 1;
            }
        }
        if (before.kind == SmithTokenKind_Identifier && unmatched >= 2 &&
            (smith_lang_ident(&before, "__attribute__") || smith_lang_ident(&before, "__attribute"))) {
            return true;
        }
    }
    return false;
}

// Whether the tokens before end a type, so that a name now is a declarator: a type keyword, a typedef name or a tag
// name, followed by any stars and qualifiers.
static bool smith_lang_sparse_typeish(SmithLang *lang, const SmithHost *host) {
    for (unsigned back = 1; back < 8; back++) { // the host remembers eight
        SmithToken before;
        if (!smith_lang_previous(lang, host, back, &before)) return false;
        if (smith_lang_punct(&before, "*")) continue;
        if (smith_lang_punct(&before, "}")) return true;
        if (before.kind != SmithTokenKind_Identifier) return false;
        SmithLangWord word = smith_lang_word(before.text, before.length);
        if (word == SmithLangWord_Qualifier) continue;
        if (word == SmithLangWord_TypeKeyword) return true;
        if (word != SmithLangWord_Other) return false;
        if (smith_lang_find_sized(before.text, before.length) != nullptr) return true;
        SmithToken tag;
        if (smith_lang_previous(lang, host, back + 1, &tag) && tag.kind == SmithTokenKind_Identifier &&
            smith_lang_word(tag.text, tag.length) == SmithLangWord_Tag) {
            return true;
        }
        return host->lookup(host->context, before.text, before.length, SmithLookup_Type).kind == SmithLookup_Type;
    }
    return false;
}

static void smith_lang_sparse_sized(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithToken shown = *token;
    if (smith_lang_sparse_at_start(lang, host) && smith_lang_io_main_begin(lang, host, token)) {
        smith_lang_emit(lang, host, smith_lang_static_keyword(token));
        shown.line_start = false;
        shown.space_before = true;
        token = &shown;
    }
    const SmithLangSized *sized = smith_lang_find_sized(token->text, token->length);
    SmithToken before;
    bool tag = smith_lang_previous(lang, host, 1, &before) && before.kind == SmithTokenKind_Identifier &&
               smith_lang_word(before.text, before.length) == SmithLangWord_Tag;
    if (sized == nullptr || tag || smith_lang_sparse_member(lang, host)) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
    bool declarator =
        smith_lang_declares_name(lang, smith_lang_peek(lang, host, 0, false), smith_lang_sparse_typeish(lang, host));
    if (declarator ||
        host->lookup(host->context, token->text, token->length, SmithLookup_Type).kind != SmithLookup_None) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
    SmithToken renamed = smith_lang_synth(SmithTokenKind_Identifier, sized->standard, strlen(sized->standard), token);
    renamed.expand = true;
    renamed.header_text = sized->header;
    renamed.header_length = (uint32_t)strlen(sized->header);
    smith_lang_emit(lang, host, renamed);
}

// module, import or export where a declaration starts. Returns false when the word is just a name.
static bool smith_lang_sparse_word(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    bool module = smith_lang_ident(token, "module");
    bool import = smith_lang_ident(token, "import");
    if (!module && !import && !smith_lang_ident(token, "export")) return false;
    if (!smith_lang_sparse_at_start(lang, host)) return false;
    if (module && smith_lang_is_module_line(lang, host, token)) {
        smith_lang_module_line(lang, host, token);
        if (lang->is_module) host->watch_mode(host->context, SmithWatch_All);
        return true;
    }
    if (import && smith_lang_is_import(lang, host, token)) {
        smith_lang_import(lang, host, token);
        if (lang->has_c && !lang->is_module && !lang->imports_watched) { // the first token after the imports is read
            host->watch_mode(host->context, SmithWatch_All);
        }
        return true;
    }
    if (!module && !import && !smith_lang_is_own_word(lang, host, token)) {
        smith_lang_error(lang, host, token->location, "`export` needs a `module` line at the top of the file");
        return true;
    }
    return false;
}

static bool smith_lang_body_c_name(SmithLang *lang, const SmithHost *host, const SmithToken *token);

// A file with no `module` line still puts an import's names under `c::`: once its imports are done (the first token
// that is not one), the names the imported headers declared are watched, and the host offers only those from then on.
static void smith_lang_sparse_imports_done(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!lang->has_c || lang->is_module || lang->imports_watched || host->watch_imports == nullptr) return;
    if (token->kind == SmithTokenKind_Identifier && smith_lang_ident(token, "import")) return;
    host->watch_imports(host->context);
    lang->imports_watched = true;
    host->watch_mode(host->context,
                     host->maybe_if_constexpr || smith_lang_type_on(lang, host) || smith_lang_tuples_on(lang, host)
                         ? SmithWatch_All
                         : SmithWatch_Names);
}

// `countof(xs)` where xs is an array parameter of the function being read (`countof(m[0])` for the next dimension): the
// size the parameter was written with, in parentheses, as a usize. Anything else named countof is the program's.
static bool smith_lang_countof_parameter(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (lang->array_param_count == 0 || !lang->in_function || lang->recording || !smith_lang_ident(token, "countof") ||
        smith_lang_sparse_member(lang, host) || !smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(")) {
        return false;
    }
    const SmithToken *name = smith_lang_peek(lang, host, 1, false);
    if (name->kind != SmithTokenKind_Identifier) return false;
    SmithLangArrayParam *param = nullptr;
    for (uint32_t i = 0; i < lang->array_param_count; i++) { // at most 8
        SmithLangArrayParam *candidate = &lang->array_params[i];
        if (smith_lang_equals(name->text, name->length, candidate->name)) param = candidate;
    }
    if (param == nullptr) return false;
    size_t at = 2;
    uint32_t dimension = 0;
    while (smith_lang_punct(smith_lang_peek(lang, host, at, false), "[") && // `[0]` per dimension, at most 3
           smith_lang_equals(smith_lang_peek(lang, host, at + 1, false)->text,
                             smith_lang_peek(lang, host, at + 1, false)->length, "0") &&
           smith_lang_punct(smith_lang_peek(lang, host, at + 2, false), "]") && dimension < param->dims) {
        dimension += 1;
        at += 3;
    }
    if (!smith_lang_punct(smith_lang_peek(lang, host, at, false), ")")) return false;
    if (dimension >= param->dims || !param->sizes[dimension].whole) {
        smith_lang_error(lang, host, token->location,
                         "countof(%s) cannot say the size of this parameter: it is not an array", param->name);
        return false;
    }
    for (size_t i = 0; i <= at; i++) // the tokens of the call
        smith_lang_take(lang, host);
    SmithToken pieces[8 + smith_lang_array_tokens_max];
    size_t count = 0;
    static const char *const head[] = {"(", "(", "__SIZE_TYPE__", ")", "("};
    for (size_t i = 0; i < 5; i++) { // five pieces
        bool word = i == 2;
        pieces[count] = smith_lang_synth(word ? SmithTokenKind_Identifier : SmithTokenKind_Punctuator, head[i],
                                         strlen(head[i]), token);
        pieces[count].expand = word;
        pieces[count].space_before = i == 0 ? token->space_before : false;
        count += 1;
    }
    for (uint32_t i = 0; i < param->sizes[dimension].count; i++) { // at most 4
        pieces[count] = smith_lang_synth(param->sizes[dimension].kinds[i], param->sizes[dimension].texts[i],
                                         strlen(param->sizes[dimension].texts[i]), token);
        pieces[count].space_before = false;
        count += 1;
    }
    for (size_t i = 0; i < 2; i++) { // two closing
        pieces[count] = smith_lang_synth(SmithTokenKind_Punctuator, ")", 1, token);
        pieces[count].space_before = false;
        count += 1;
    }
    for (size_t i = 0; i < count; i++) // at most 11
        smith_lang_emit(lang, host, pieces[i]);
    return true;
}

static void smith_lang_sparse(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!token->foreign) smith_lang_sparse_imports_done(lang, host, token);
    if (token->foreign) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
    if (token->kind == SmithTokenKind_Eof) {
        smith_lang_end_of_file(lang, host, token);
        return;
    }
    if (token->kind != SmithTokenKind_Identifier) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
    if (smith_lang_sparse_word(lang, host, token)) return;
    SmithLangBinding *binding = lang->bindings_count > 0 && !smith_lang_sparse_member(lang, host)
                                    ? smith_lang_binding_find(lang, token->text, token->length)
                                    : nullptr;
    SmithToken resolved;
    if (binding != nullptr) {
        lang->in_attribute = smith_lang_sparse_in_attribute(lang, host);
        if (smith_lang_namespace(lang, host, token, binding, &resolved)) {
            if (lang->namespace_done) { // a generic call, written with its arguments
                lang->namespace_done = false;
                return;
            }
            smith_lang_emit(lang, host, resolved);
            return;
        }
    }
    if (!lang->is_module && lang->imports_watched && !smith_lang_sparse_member(lang, host) &&
        smith_lang_body_c_name(lang, host, token)) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
    smith_lang_sparse_sized(lang, host, token);
}

// ---- A function body in a module
// ----------------------------------------------------------------------------------------

// A `{` that opens a struct, union or enum: `struct {` or `struct S {`.
static bool smith_lang_body_aggregate(SmithLang *lang, const SmithHost *host, bool *is_enum) {
    SmithToken before;
    for (unsigned back = 1; back <= 2; back++) { // the keyword, or the tag and then the keyword
        if (!smith_lang_previous(lang, host, back, &before) || before.kind != SmithTokenKind_Identifier) return false;
        if (smith_lang_word(before.text, before.length) == SmithLangWord_Tag) {
            *is_enum = smith_lang_ident(&before, "enum");
            return true;
        }
    }
    return false;
}

static void smith_lang_body_brace(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (smith_lang_punct(token, "{")) {
        bool is_enum = false;
        if (smith_lang_body_aggregate(lang, host, &is_enum)) {
            // The members are not names in scope: read the aggregate with every token, up to the end of the function.
            lang->agg_pending = true;
            lang->enum_pending = is_enum;
            lang->body_sparse = false;
            host->watch_mode(host->context, SmithWatch_All);
        }
        smith_lang_stream_open(lang, host, token);
        return;
    }
    smith_lang_stream_close(lang, host, token);
    if (lang->state == SmithLangState_Start) { // the function ended: the next declaration is read whole
        lang->body_sparse = false;
        host->watch_mode(host->context, SmithWatch_All);
    }
}

// A watched name that an imported header declared, used without c:: (a tag after struct, else a name): the error,
// unless the token declares a name of its own. Returns true when the token has been dealt with.
static bool smith_lang_body_c_name(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!lang->has_c || smith_lang_find_sized(token->text, token->length) != nullptr) return false;
    if (lang->bindings_count > 0 && smith_lang_binding_find(lang, token->text, token->length) != nullptr) return false;
    SmithToken before;
    const SmithToken *next = nullptr;
    if (smith_lang_previous(lang, host, 1, &before) && before.kind == SmithTokenKind_Identifier &&
        smith_lang_word(before.text, before.length) == SmithLangWord_Tag) {
        smith_lang_lower_tag(lang, host, token->text, token->length, token->location, &next);
        return true;
    }
    next = smith_lang_peek(lang, host, 0, false);
    bool declarator = smith_lang_follows_declarator(next) && smith_lang_sparse_typeish(lang, host);
    bool is_type = false;
    if (!declarator) smith_lang_check_c_name(lang, host, token->text, token->length, token->location, &is_type);
    return true;
}

// A watched name that is not the module's own: a C name of an import, a binding (rp::open), a sized type, or kept.
static void smith_lang_sparse_ident(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (smith_lang_body_c_name(lang, host, token)) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
    SmithLangBinding *binding =
        lang->bindings_count > 0 ? smith_lang_binding_find(lang, token->text, token->length) : nullptr;
    SmithToken resolved;
    lang->in_attribute = binding != nullptr && smith_lang_sparse_in_attribute(lang, host);
    if (binding != nullptr && smith_lang_namespace(lang, host, token, binding, &resolved)) {
        if (lang->namespace_done) { // a generic call, written with its arguments
            lang->namespace_done = false;
            return;
        }
        smith_lang_emit(lang, host, resolved);
    } else {
        smith_lang_sparse_sized(lang, host, token);
    }
}

// A token of a function body, when the host offers only braces and the names it was told to watch.
static void smith_lang_body(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!token->foreign && token->kind == SmithTokenKind_Eof) {
        smith_lang_end_of_file(lang, host, token);
        return;
    }
    if (!token->foreign && (smith_lang_punct(token, "{") || smith_lang_punct(token, "}"))) { // others were read ahead
        smith_lang_body_brace(lang, host, token);
        return;
    }
    bool name = !token->foreign && token->kind == SmithTokenKind_Identifier && !smith_lang_sparse_member(lang, host);
    if (name && smith_lang_countof_parameter(lang, host, token)) return;
    const SmithLangName *known = name ? smith_lang_name_find(lang, token->text, token->length) : nullptr;
    if (known != nullptr && known->generic != 0 && smith_lang_generic_call_here(lang, host, token, known)) return;
    // An import named as a name of the module wins when it is written with `::` (`import smith::build;` in `module
    // build;`: `build::app` is the import's, `build(b)` the module's).
    if (known != nullptr && smith_lang_binding_find(lang, token->text, token->length) != nullptr &&
        smith_lang_punct(smith_lang_peek(lang, host, 0, false), "::")) {
        smith_lang_sparse_ident(lang, host, token);
        return;
    }
    if (known != nullptr && smith_lang_is_param(lang, token->text, token->length)) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
    if (known != nullptr) {
        const char *prefixed = smith_lang_prefixed(lang, token->text, token->length);
        smith_lang_emit(lang, host, smith_lang_synth(SmithTokenKind_Identifier, prefixed, lang->tmp_length, token));
        return;
    }
    if (!name) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
    smith_lang_sparse_ident(lang, host, token);
}

bool smith_lang_param_name(const SmithLang *lang, const char *name, size_t length) {
    return lang->in_function && smith_lang_is_param(lang, name, length);
}

bool smith_lang_import_name(SmithLang *lang, const char *name, size_t length) {
    return lang->bindings_count > 0 && smith_lang_binding_find(lang, name, length) != nullptr;
}

bool smith_lang_type_name(SmithLang *lang, const char *name, size_t length) {
    if (!lang->is_module) return false;
    const SmithLangName *known = smith_lang_name_find(lang, name, length);
    return known != nullptr && known->kind == SmithLangName_Type;
}

bool smith_lang_body_sparse(const SmithLang *lang) {
    return lang->body_sparse && !lang->broken;
}

bool smith_lang_fast_brace(SmithLang *lang, const SmithHost *host, bool open) {
    if (!lang->body_sparse || lang->broken || lang->ifc_count > 0) return false; // an `if constexpr` counts braces
    if (!open) {
        if (lang->brace_depth == 1 && lang->braces[0] == SmithLangBrace_Function) { // the function's own: it ends
            lang->brace_depth = 0;
            lang->in_function = false;
            smith_lang_end_declaration(lang);
            lang->body_sparse = false;
            host->watch_mode(host->context, SmithWatch_All);
            return true;
        }
        if (lang->brace_depth <= 1) return false;
        lang->brace_depth -= 1;
        return true;
    }
    bool is_enum = false;
    if (lang->brace_depth >= smith_lang_braces_max || smith_lang_body_aggregate(lang, host, &is_enum)) return false;
    lang->braces[lang->brace_depth++] = SmithLangBrace_Block;
    return true;
}
