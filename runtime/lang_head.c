// The head of a file-scope declaration in a module: held until it is known what it declares, then written out with the
// module's prefix, `static` if it is private, and for an exported function a prototype carrying the symbol.
// Included by runtime/lang.c, after runtime/lang_import.c.

typedef enum {
    SmithLangForm_Type,     // a typedef, a tag, an assertion: nothing is declared that has a symbol
    SmithLangForm_Function, // a name followed by a parameter list
    SmithLangForm_Variable,
} SmithLangForm;

static SmithToken smith_lang_held_token(const SmithLang *lang, const SmithLangHeld *held) {
    SmithToken token = {0};
    token.kind = held->kind;
    token.line_start = held->line_start;
    token.space_before = held->space_before;
    token.expand = held->expand;
    token.name = held->role == SmithLangRole_Declared;
    token.location = held->location;
    token.handle = held->handle;
    token.text = smith_lang_scratch_str(held->text);
    token.length = held->text.length;
    if (held->header_text.length > 0) {
        token.header_text = smith_lang_scratch_str(held->header_text);
        token.header_length = held->header_text.length;
    }
    (void)lang;
    return token;
}

static void smith_lang_hold(SmithLang *lang, const SmithToken *token, SmithLangRole role,
                            const SmithLangContext *context) {
    if (lang->head_count >= smith_lang_head_max) {
        lang->broken = true;
        return;
    }
    SmithLangHeld *held = &lang->head[lang->head_count++];
    held->kind = token->kind;
    held->role = role;
    held->line_start = token->line_start || lang->carry_line_start;
    lang->carry_line_start = false;
    held->space_before = token->space_before;
    held->expand = token->expand;
    held->after_member = context->after_member;
    held->after_tag = context->after_tag;
    held->typeish = context->typeish;
    held->before = context->before;
    held->location = token->location;
    held->handle = token->handle;
    held->paren_depth = lang->paren_depth;
    held->text = smith_lang_scratch_store(lang, token->text, token->length);
    held->header_text = (SmithLangText){0, 0};
    held->original = held->text;
}

static void smith_lang_emit_held(SmithLang *lang, const SmithHost *host, size_t index, bool header_too) {
    SmithToken token = smith_lang_held_token(lang, &lang->head[index]);
    if (header_too) {
        smith_lang_emit(lang, host, token);
    } else {
        smith_lang_emit_code(lang, host, token);
    }
}

// ---- Resolving a name once the token after it is known
// -----------------------------------------------------------------

// Whether the identifier just held introduces a name, given what follows it.
static bool smith_lang_declares_here(const SmithLang *lang, const SmithLangHeld *held, const SmithToken *next) {
    if (held->after_member || held->after_tag || !lang->decl.saw_type || lang->decl.name_found) return false;
    if (held->paren_depth == 0) {
        return smith_lang_punct(next, "(") || smith_lang_punct(next, "[") || smith_lang_punct(next, "=") ||
               smith_lang_punct(next, ";") || smith_lang_punct(next, ",");
    }
    // void (*callback)(int): the name inside the first parentheses, after a star.
    return held->paren_depth == 1 && held->before == '*' && smith_lang_punct(next, ")") && !lang->decl.in_params;
}

static bool smith_lang_type_use_follows(const SmithToken *next) {
    if (next->kind == SmithTokenKind_Identifier) return true;
    return smith_lang_punct(next, "*");
}

static void smith_lang_replace_held(SmithLang *lang, SmithLangHeld *held, const SmithLangLowered *lowered) {
    if (!lowered->replaced) return;
    held->handle = smith_token_no_handle;
    held->text = smith_lang_scratch_store(lang, lowered->text, lowered->length);
    held->expand = lowered->expand;
    if (lowered->header != nullptr)
        held->header_text = smith_lang_scratch_store(lang, lowered->header, lowered->header_length);
}

static void smith_lang_resolve(SmithLang *lang, const SmithHost *host, const SmithToken *next) {
    if (lang->pending == SIZE_MAX) return;
    SmithLangHeld *held = &lang->head[lang->pending];
    size_t index = lang->pending;
    lang->pending = SIZE_MAX;
    const char *name = smith_lang_scratch_str(held->text);
    size_t length = held->text.length;
    bool is_type = false;
    bool declared = false;
    if (held->role == SmithLangRole_Plain && smith_lang_word(name, length) == SmithLangWord_Other &&
        smith_lang_declares_here(lang, held, next)) {
        SmithLangNameKind kind = lang->decl.is_typedef         ? SmithLangName_Type
                                 : smith_lang_punct(next, "(") ? SmithLangName_Function
                                                               : SmithLangName_Value;
        const char *prefixed = smith_lang_declare(lang, host, name, length, held->location, kind, lang->decl.exported);
        if (prefixed != nullptr) {
            SmithLangLowered lowered = {.replaced = true, .text = prefixed, .length = lang->tmp_length};
            smith_lang_replace_held(lang, held, &lowered);
        }
        held->role = SmithLangRole_Declared;
        declared = true;
        lang->decl.name_found = true;
        lang->decl.declared += 1;
        lang->decl.last_name = (uint32_t)index;
        if (kind == SmithLangName_Function) lang->decl.is_function = true;
    } else if (held->role == SmithLangRole_Plain) {
        SmithLangContext context = {held->after_member, held->after_tag, held->typeish, false, held->before};
        SmithLangLowered lowered = smith_lang_lower(lang, host, name, length, held->location, &context, next);
        smith_lang_replace_held(lang, held, &lowered);
        is_type = lowered.is_type;
    }
    if (!declared) is_type = is_type || smith_lang_type_use_follows(next);
    if (is_type) lang->decl.saw_type = true;
    SmithToken as_token = smith_lang_held_token(lang, held);
    smith_lang_track(lang, &as_token, is_type);
    lang->typeish = is_type;
    lang->prev_name_length = 0;
}

// ---- Taking a token into the head
// ----------------------------------------------------------------------------------------

static void smith_lang_note_keyword(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangWord word = smith_lang_word(token->text, token->length);
    if (word == SmithLangWord_Typedef) lang->decl.is_typedef = true;
    if (word == SmithLangWord_TypeKeyword) lang->decl.saw_type = true;
    if (word == SmithLangWord_Tag) {
        lang->decl.aggregate = true;
        lang->decl.last_tag_enum = smith_lang_ident(token, "enum");
    }
    if (word == SmithLangWord_Storage) {
        if (smith_lang_ident(token, "static") || smith_lang_ident(token, "extern")) lang->decl.storage = true;
        if (smith_lang_ident(token, "extern")) lang->decl.is_extern = true;
        if (smith_lang_ident(token, "static") && lang->decl.exported) {
            smith_lang_error(lang, host, token->location,
                             "an exported declaration cannot be `static`: export is what makes it public");
        }
        if (smith_lang_ident(token, "constexpr")) lang->decl.is_constexpr = true;
    }
    if (smith_lang_ident(token, "inline")) lang->decl.is_inline = true;
}

static bool smith_lang_head_punct(SmithLang *lang, const SmithToken *token) {
    if (smith_lang_punct(token, "(")) {
        if (lang->decl.params_next && lang->paren_depth == 0) lang->decl.in_params = true;
        lang->decl.params_next = false;
        lang->paren_depth += 1;
    } else if (smith_lang_punct(token, ")")) {
        if (lang->paren_depth > 0) lang->paren_depth -= 1;
        if (lang->paren_depth == 0) lang->decl.in_params = false;
    } else if (smith_lang_punct(token, "[")) {
        lang->bracket_depth += 1;
    } else if (smith_lang_punct(token, "]")) {
        if (lang->bracket_depth > 0) lang->bracket_depth -= 1;
    } else if (smith_lang_punct(token, ",") && lang->paren_depth == 0 && lang->bracket_depth == 0) {
        lang->decl.name_found = false;
    }
    if (lang->paren_depth != 0 || lang->bracket_depth != 0) return false;
    return smith_lang_punct(token, ";") || smith_lang_punct(token, "=") || smith_lang_punct(token, "{");
}

// ---- Writing the head out
// ------------------------------------------------------------------------------------------------

#ifdef __APPLE__
constexpr char smith_lang_symbol_underscore[] = "_";
#else
constexpr char smith_lang_symbol_underscore[] = "";
#endif

static SmithToken smith_lang_punct_after(const char *text, const SmithToken *like) {
    SmithToken made = smith_lang_synth(SmithTokenKind_Punctuator, text, strlen(text), like);
    made.line_start = false;
    made.space_before = false;
    made.synthetic = true;
    return made;
}

// __asm__("_game::replay::open") after a declarator, so the symbol is the module's.
static void smith_lang_emit_label(SmithLang *lang, const SmithHost *host, const SmithToken *like, bool header_too) {
    const SmithLangHeld *name_held = &lang->head[lang->decl.last_name];
    char label[smith_lang_name_max * 2];
    int length = snprintf(label, sizeof label, "\"%s%s%s\"", smith_lang_symbol_underscore, lang->symbol,
                          smith_lang_scratch_str(name_held->original));
    SmithToken asm_token = smith_lang_synth(SmithTokenKind_Identifier, "__asm__", 7, like);
    asm_token.line_start = false;
    asm_token.space_before = true;
    asm_token.synthetic = true;
    SmithToken open = smith_lang_punct_after("(", like);
    SmithToken string = smith_lang_synth(SmithTokenKind_String, label, (size_t)length, like);
    string.line_start = false;
    string.space_before = false;
    string.synthetic = true;
    SmithToken close = smith_lang_punct_after(")", like);
    SmithToken pieces[4] = {asm_token, open, string, close};
    for (size_t i = 0; i < 4; i++) { // the four pieces
        if (header_too) {
            smith_lang_emit(lang, host, pieces[i]);
        } else {
            smith_lang_emit_code(lang, host, pieces[i]);
        }
    }
}

static SmithToken smith_lang_last_of_head(const SmithLang *lang) {
    return smith_lang_held_token(lang, &lang->head[lang->head_count - 1]);
}

static void smith_lang_emit_head(SmithLang *lang, const SmithHost *host, bool header_too, bool with_static) {
    for (size_t i = 0; i < lang->head_count; i++) { // bounded by the head
        if (i == 0 && with_static) {
            SmithToken first = smith_lang_held_token(lang, &lang->head[0]);
            smith_lang_emit_code(lang, host, smith_lang_static_keyword(&first));
            lang->head[0].line_start = false;
            lang->head[0].space_before = true;
        }
        smith_lang_emit_held(lang, host, i, header_too);
    }
}

static SmithLangForm smith_lang_form(const SmithLang *lang) {
    if (lang->decl.is_typedef || lang->decl.declared == 0) return SmithLangForm_Type;
    return lang->decl.is_function ? SmithLangForm_Function : SmithLangForm_Variable;
}

// extern T name __asm__("sym"); in the header for an exported variable defined with an initializer.
static void smith_lang_header_variable(SmithLang *lang, bool labeled) {
    SmithToken first = smith_lang_held_token(lang, &lang->head[0]);
    SmithToken keyword = smith_lang_synth(SmithTokenKind_Identifier, "extern", 6, &first);
    if (!lang->decl.is_extern) smith_lang_emit_header(lang, keyword);
    for (size_t i = 0; i < lang->head_count; i++) { // bounded by the head
        SmithToken token = smith_lang_held_token(lang, &lang->head[i]);
        if (i == 0 && !lang->decl.is_extern) {
            token.line_start = false;
            token.space_before = true;
        }
        smith_lang_emit_header(lang, token);
    }
    SmithToken last = smith_lang_last_of_head(lang);
    if (!labeled) { // a chosen C name is the symbol: extern T name;
        smith_lang_emit_header(lang, smith_lang_punct_after(";", &last));
        return;
    }
    char label[smith_lang_name_max * 2];
    int length = snprintf(label, sizeof label, "\"%s%s%s\"", smith_lang_symbol_underscore, lang->symbol,
                          smith_lang_scratch_str(lang->head[lang->decl.last_name].original));
    SmithToken pieces[5] = {smith_lang_synth(SmithTokenKind_Identifier, "__asm__", 7, &last),
                            smith_lang_punct_after("(", &last),
                            smith_lang_synth(SmithTokenKind_String, label, (size_t)length, &last),
                            smith_lang_punct_after(")", &last), smith_lang_punct_after(";", &last)};
    pieces[0].line_start = false;
    pieces[0].space_before = true;
    pieces[2].line_start = false;
    pieces[2].space_before = false;
    for (size_t i = 0; i < 5; i++)
        smith_lang_emit_header(lang, pieces[i]); // the five pieces
}

static void smith_lang_end_declaration(SmithLang *lang) {
    if (lang->decl.c_name.length > 0 && !lang->decl.c_name_used) { // reported by the next token fed (it has the host)
        lang->unnamed_c_name = true;
        lang->unnamed_c_name_location = lang->decl.c_name_location;
    }
    // A comment after the export's last token follows it in the header.
    lang->tail_open = lang->export_active || lang->export_tail;
    lang->export_tail = false;
    lang->export_active = false;
    lang->state = SmithLangState_Start;
    lang->decl = (SmithLangDecl){0};
    lang->pending_export = false;
    lang->init_top = false;
    lang->tail_declarator = false;
    lang->statement_typedef = false;
    lang->agg_pending = false;
    lang->head_count = 0;
    lang->scratch_used = 0;
    lang->pending = SIZE_MAX;
    lang->paren_depth = 0;
    lang->bracket_depth = 0;
}

static void smith_lang_push_brace(SmithLang *lang, const SmithHost *host, SmithLangBrace kind, uint32_t location) {
    if (lang->brace_depth >= smith_lang_braces_max) {
        smith_lang_error(lang, host, location, "braces nest more deeply than smith reads (%zu)", smith_lang_braces_max);
        lang->broken = true;
        return;
    }
    lang->braces[lang->brace_depth++] = kind;
}

// A function's `{`: its body needs the braces, the module's names and the imported ones.
static void smith_lang_enter_body(SmithLang *lang, const SmithHost *host, uint32_t location) {
    if (host->previous != nullptr) {
        if (lang->has_c && !lang->imports_watched) host->watch_imports(host->context);
        lang->imports_watched = true;
        lang->body_sparse = true;
        host->watch_mode(host->context, SmithWatch_Braces);
    }
    smith_lang_push_brace(lang, host, SmithLangBrace_Function, location);
    lang->in_function = true;
    lang->export_active = lang->decl.inline_header; // an exported inline function's body is in the header too
    lang->decl.checking = false;
}

// After the head is written: the terminator that ended it, and where the declaration goes from there.
static void smith_lang_after_head(SmithLang *lang, const SmithHost *host, const SmithToken *terminator,
                                  SmithLangForm form, bool proto_written) {
    bool semicolon = smith_lang_punct(terminator, ";");
    if (proto_written && semicolon) {
        smith_lang_end_declaration(lang);
        return;
    }
    smith_lang_emit(lang, host, *terminator);
    lang->head_count = 0;
    lang->scratch_used = 0;
    lang->pending = SIZE_MAX;
    lang->paren_depth = 0;
    lang->bracket_depth = 0;
    if (semicolon) {
        smith_lang_end_declaration(lang);
        return;
    }
    lang->state = SmithLangState_Stream;
    lang->prev_char = terminator->text[0];
    lang->prev_member = false;
    lang->prev_tag = false;
    lang->typeish = false;
    lang->prev_name_length = 0;
    if (smith_lang_punct(terminator, "=")) {
        lang->init_top = true;
        return;
    }
    if (form == SmithLangForm_Function) {
        smith_lang_enter_body(lang, host, terminator->location);
        return;
    }
    smith_lang_push_brace(lang, host, lang->decl.last_tag_enum ? SmithLangBrace_Enum : SmithLangBrace_Aggregate,
                          terminator->location);
}

// An exported inline function is defined in the header, as `static inline`, and so in the module: it has no symbol
// (the includer's compiler inlines it, or makes its own copy), and its body goes to the header with the head.
static void smith_lang_emit_inline_head(SmithLang *lang, const SmithHost *host) {
    lang->decl.inline_header = true;
    SmithToken first = smith_lang_held_token(lang, &lang->head[0]);
    smith_lang_emit(lang, host, smith_lang_static_keyword(&first));
    lang->head[0].line_start = false;
    lang->head[0].space_before = true;
    smith_lang_emit_head(lang, host, true, false);
}

// runtime/lang_generic.c
static void smith_lang_generic_flush_head(SmithLang *lang, const SmithHost *host, const SmithToken *terminator);

// The head is complete: the terminator says what follows. Writes the head, then the terminator, and picks the state.
static void smith_lang_flush_head(SmithLang *lang, const SmithHost *host, const SmithToken *terminator) {
    if (lang->decl.generic) { // recorded, not written
        smith_lang_generic_flush_head(lang, host, terminator);
        return;
    }
    SmithLangForm form = smith_lang_form(lang);
    bool is_main = false;
    if (form != SmithLangForm_Type) {
        const SmithLangHeld *name_held = &lang->head[lang->decl.last_name];
        is_main = smith_lang_equals(smith_lang_scratch_str(name_held->original), name_held->original.length, "main");
    }
    if (lang->decl.exported && lang->decl.declared > 1) {
        smith_lang_error(lang, host, lang->head[0].location, "an exported declaration declares one name; split it");
    }
    // A chosen C name is the symbol too, as C writes it: no label, so C and C++ define and call it by that name.
    bool has_c_name = lang->decl.c_name_used && lang->decl.c_name.length > 0;
    bool symbol = lang->decl.exported && form != SmithLangForm_Type && !lang->decl.is_constexpr && !is_main;
    bool private_static =
        lang->is_module && !lang->decl.exported && form != SmithLangForm_Type && !lang->decl.storage && !is_main;
    bool proto_written = false;
    SmithToken like = smith_lang_last_of_head(lang);
    if (symbol && form == SmithLangForm_Function && lang->decl.is_inline && smith_lang_punct(terminator, "{")) {
        smith_lang_emit_inline_head(lang, host);
    } else if (symbol && form == SmithLangForm_Function) {
        // With a body the head is written twice: the prototype that carries the symbol is a copy, and the definition
        // after it has the tokens of the source.
        lang->emit_synthetic = smith_lang_punct(terminator, "{");
        smith_lang_emit_head(lang, host, true, false);
        if (!has_c_name) smith_lang_emit_label(lang, host, &like, true);
        smith_lang_emit(lang, host, smith_lang_punct_after(";", &like));
        lang->emit_synthetic = false;
        lang->export_active = false;
        proto_written = true;
        if (smith_lang_punct(terminator, "{")) {
            smith_lang_emit_head(lang, host, false, false);
            proto_written = false;
        } else {
            lang->export_tail = true;
            smith_lang_end_declaration(lang);
            return;
        }
    } else if (symbol && form == SmithLangForm_Variable) {
        smith_lang_header_variable(lang, !has_c_name);
        lang->export_active = false;
        lang->export_tail = true;
        smith_lang_emit_head(lang, host, false, false);
        if (!has_c_name) smith_lang_emit_label(lang, host, &like, false);
    } else {
        smith_lang_emit_head(lang, host, true, private_static);
    }
    smith_lang_after_head(lang, host, terminator, form, proto_written);
}

// ---- A token in the head
// ----------------------------------------------------------------------------------------------------

static bool smith_lang_enter_namespace(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                       SmithToken *result) {
    if (lang->bindings_count == 0 || lang->prev_member) return false;
    SmithLangBinding *binding = smith_lang_binding_find(lang, token->text, token->length);
    if (binding == nullptr) return false;
    return smith_lang_namespace(lang, host, token, binding, result);
}

static void smith_lang_head_identifier(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangContext context = smith_lang_context(lang, host, false);
    if (smith_lang_word(token->text, token->length) != SmithLangWord_Other) {
        smith_lang_note_keyword(lang, host, token);
        smith_lang_hold(lang, token, SmithLangRole_Plain, &context);
        smith_lang_track(lang, token, false);
        return;
    }
    SmithToken resolved;
    if (smith_lang_enter_namespace(lang, host, token, &resolved)) {
        smith_lang_hold(lang, &resolved, SmithLangRole_Done, &context);
    } else {
        smith_lang_hold(lang, token, SmithLangRole_Plain, &context);
    }
    lang->pending = lang->head_count - 1;
}

static void smith_lang_head(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    smith_lang_resolve(lang, host, token);
    if (token->kind == SmithTokenKind_Identifier) {
        smith_lang_head_identifier(lang, host, token);
        return;
    }
    if (token->kind == SmithTokenKind_Punctuator && smith_lang_head_punct(lang, token)) {
        smith_lang_flush_head(lang, host, token);
        return;
    }
    SmithLangContext context = smith_lang_context(lang, host, false);
    smith_lang_hold(lang, token, SmithLangRole_Plain, &context);
    smith_lang_track(lang, token, false);
    if (smith_lang_punct(token, "*") || smith_lang_punct(token, "(") || smith_lang_punct(token, ")")) return;
}
