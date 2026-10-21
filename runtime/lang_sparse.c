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
    unsigned closed = 0;   // parentheses closed on the way back
    unsigned unmatched = 0; // parentheses open around the token
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
    const SmithLangSized *sized = smith_lang_find_sized(token->text, token->length);
    SmithToken before;
    bool tag = smith_lang_previous(lang, host, 1, &before) && before.kind == SmithTokenKind_Identifier &&
               smith_lang_word(before.text, before.length) == SmithLangWord_Tag;
    if (sized == nullptr || tag || smith_lang_sparse_member(lang, host)) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
    bool declarator =
        smith_lang_sparse_typeish(lang, host) && smith_lang_follows_declarator(smith_lang_peek(lang, host, 0, false));
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
    host->watch_mode(host->context, SmithWatch_Names);
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
    if (name && smith_lang_name_find(lang, token->text, token->length) != nullptr) {
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

bool smith_lang_body_sparse(const SmithLang *lang) {
    return lang->body_sparse && !lang->broken;
}

bool smith_lang_fast_brace(SmithLang *lang, const SmithHost *host, bool open) {
    if (!lang->body_sparse || lang->broken) return false;
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
