// A module's private declarations, read as they stream by: no head is held, so a host that offers every token pays for
// each one only by smith_lang_fast. What a head needs is decided from the tokens before it, not after: `static` goes
// before the first token (from what that token is), the name a declaration introduces is the first name after a type,
// and `main` is told at the first token. An exported declaration is still held (runtime/lang_head.c), since its
// prototype repeats the head; so is everything in a host with no record of the last tokens (the plain lexer). The
// host's fast path calls smith_lang_fast with every token; what needs a look ahead, or the full logic of a name, it
// answers Slow, before it has changed anything, and the token goes through smith_lang_feed, which can look ahead.
// Included by runtime/lang.c, after runtime/lang_sparse.c.

static void smith_lang_reset_declaration(SmithLang *lang);

// runtime/lang_generic.c
static bool smith_lang_generic_candidate(const SmithLang *lang, const SmithHost *host, const SmithToken *first);
static void smith_lang_generic_finish(SmithLang *lang);

// ---- The state a token changes in the stream (after a head), apart from writing it
// ---------------------------------------

static void smith_lang_open_state(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangBrace kind = SmithLangBrace_Block;
    if (lang->agg_pending) {
        kind = lang->enum_pending ? SmithLangBrace_Enum : SmithLangBrace_Aggregate;
    } else if (lang->brace_depth == 0) {
        kind = lang->init_top ? SmithLangBrace_Init : SmithLangBrace_Function;
        if (kind == SmithLangBrace_Function) lang->in_function = true;
    }
    lang->agg_pending = false;
    smith_lang_push_brace(lang, host, kind, token->location);
}

static void smith_lang_close_state(SmithLang *lang) {
    if (lang->brace_depth == 0) return;
    SmithLangBrace kind = lang->braces[--lang->brace_depth];
    if (lang->brace_depth != 0) return;
    if (kind == SmithLangBrace_Function) {
        lang->in_function = false;
        if (lang->recording) smith_lang_generic_finish(lang);
        smith_lang_end_declaration(lang);
    } else if (kind == SmithLangBrace_Aggregate || kind == SmithLangBrace_Enum) {
        lang->tail_declarator = true;
    }
}

// A punctuator that is not a brace.
static void smith_lang_punctuator_state(SmithLang *lang, char c) {
    if (c == '(') lang->paren_depth += 1;
    if (c == ')' && lang->paren_depth > 0) lang->paren_depth -= 1;
    if (c == '=' && lang->brace_depth == 0) {
        lang->init_top = true;
        lang->tail_declarator = false;
    }
    if (c == ',' && lang->brace_depth == 0 && lang->paren_depth == 0 && !lang->init_top) {
        lang->tail_declarator = true;
    }
    if (c == '(' || c == ')' || c == '=') lang->agg_pending = false;
}

// After the `;` is written.
static void smith_lang_semicolon_state(SmithLang *lang, char c) {
    if (c != ';' || lang->paren_depth != 0) return;
    lang->statement_typedef = false;
    lang->agg_pending = false;
    if (lang->brace_depth == 0) smith_lang_end_declaration(lang);
}

// typedef, struct, union and enum.
static void smith_lang_word_state(SmithLang *lang, const SmithToken *token, SmithLangWord word) {
    if (word == SmithLangWord_Typedef) {
        lang->statement_typedef = true;
        lang->typedef_depth = lang->brace_depth;
    }
    if (word == SmithLangWord_Tag) {
        lang->agg_pending = true;
        lang->enum_pending = smith_lang_ident(token, "enum");
    }
}

static SmithLangAct smith_lang_act(SmithLangActKind kind) {
    SmithLangAct act = {.kind = kind};
    return act;
}

static SmithLangAct smith_lang_rename_act(const char *text, size_t length) {
    SmithLangAct act = {.kind = SmithLangAct_Rename, .text = text, .length = (uint32_t)length};
    return act;
}

// Whether the declaration that begins here is `main`, which stays public: `main` before the first ( ; = or {.
static bool smith_lang_is_main(SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    if (smith_lang_ident(first, "main")) return true;
    for (size_t index = 0; index < 8; index++) { // a type is a few tokens
        const SmithToken *next = smith_lang_peek(lang, host, index, false);
        if (smith_lang_ident(next, "main")) return true;
        if (next->kind == SmithTokenKind_Eof || smith_lang_punct(next, "(") || smith_lang_punct(next, ";") ||
            smith_lang_punct(next, "=") || smith_lang_punct(next, "{")) {
            return false;
        }
    }
    return false;
}

// Whether the declaration that begins with this token is a function or variable that is private, so that it needs
// `static`. Sets *slow when the answer needs a look ahead that the caller cannot make.
static bool smith_lang_wants_static(SmithLang *lang, const SmithHost *host, const SmithToken *token, bool may_peek,
                                    bool *slow) {
    if (token->kind != SmithTokenKind_Identifier) return false;
    SmithLangWord word = smith_lang_word(token->text, token->length);
    if (word == SmithLangWord_Typedef) return false;
    if (word == SmithLangWord_Storage) return !smith_lang_ident(token, "static") && !smith_lang_ident(token, "extern");
    if (word == SmithLangWord_Keyword) {
        return !smith_lang_ident(token, "static_assert") && !smith_lang_ident(token, "_Static_assert") &&
               !smith_lang_ident(token, "asm") && !smith_lang_ident(token, "__asm") &&
               !smith_lang_ident(token, "__asm__");
    }
    if (word != SmithLangWord_Tag && !host->maybe_main) return true;
    if (!may_peek) { // struct S {…}; or int main
        *slow = true;
        return false;
    }
    if (word != SmithLangWord_Tag) return !smith_lang_is_main(lang, host, token);
    size_t at = 0;
    const SmithToken *next = smith_lang_peek(lang, host, at, false);
    for (size_t depth = 0; smith_lang_punct(next, "[") || depth > 0;
         at++) {                                                       // `struct [[attribute]] S {`: skip the [[ ]]
        if (at >= 64 || next->kind == SmithTokenKind_Eof) return true; // bounded
        depth += smith_lang_punct(next, "[") ? 1 : 0;
        depth -= smith_lang_punct(next, "]") ? 1 : 0;
        next = smith_lang_peek(lang, host, at + 1, false);
    }
    const SmithToken *after =
        next->kind == SmithTokenKind_Identifier ? smith_lang_peek(lang, host, at + 1, false) : next;
    return !(smith_lang_punct(after, "{") || smith_lang_punct(after, ";") || smith_lang_punct(after, ":"));
}

// The first token of a file-scope declaration of a module that is not exported: a declaration begins (or not).
static SmithLangAct smith_lang_head_begin(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                          bool may_peek, bool forced, bool *begun) {
    *begun = false;
    if (!forced && token->watched &&
        (smith_lang_ident(token, "module") || smith_lang_ident(token, "import") || smith_lang_ident(token, "export"))) {
        return smith_lang_act(SmithLangAct_Slow);
    }
    if (smith_lang_punct(token, ";")) return smith_lang_act(SmithLangAct_Keep);
    if (!forced && smith_lang_punct(token, "[")) return smith_lang_act(SmithLangAct_Slow); // [[smith::c_name("...")]]
    if (!forced && smith_lang_generic_candidate(lang, host, token)) return smith_lang_act(SmithLangAct_Slow);
    if (!forced && lang->linked_count > 0) return smith_lang_act(SmithLangAct_Slow); // held, as an export's is
    bool slow = false;
    bool wants = smith_lang_wants_static(lang, host, token, may_peek, &slow);
    if (slow) return smith_lang_act(SmithLangAct_Slow);
    smith_lang_reset_declaration(lang);
    lang->seen_declaration = true;
    lang->state = SmithLangState_Declare;
    lang->decl.static_inserted = wants;
    lang->decl.attribute_first = smith_lang_punct(token, "[");
    if (lang->has_c && !lang->imports_watched && host->watch_imports != nullptr) {
        host->watch_imports(host->context);
        lang->imports_watched = true;
    }
    *begun = true;
    SmithLangAct act = smith_lang_act(SmithLangAct_Keep);
    act.insert_static = wants;
    return act;
}

// `{` ends the head: a function body, an aggregate's or an enumeration's.
static void smith_lang_head_brace(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    lang->state = SmithLangState_Stream;
    lang->prev_char = '{';
    lang->prev_member = false;
    lang->prev_tag = false;
    lang->typeish = false;
    lang->prev_name_length = 0;
    lang->paren_depth = 0;
    lang->bracket_depth = 0;
    if (lang->decl.is_function && !lang->decl.is_typedef) {
        smith_lang_enter_body(lang, host, token->location);
        return;
    }
    smith_lang_push_brace(lang, host, lang->decl.last_tag_enum ? SmithLangBrace_Enum : SmithLangBrace_Aggregate,
                          token->location);
}

static void smith_lang_head_initializer(SmithLang *lang) {
    lang->state = SmithLangState_Stream;
    lang->init_top = true;
    lang->prev_char = '=';
    lang->prev_member = false;
    lang->prev_tag = false;
    lang->typeish = false;
    lang->prev_name_length = 0;
    lang->paren_depth = 0;
    lang->bracket_depth = 0;
}

// A punctuator of a head.
static void smith_lang_head_punctuator(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangDecl *decl = &lang->decl;
    char c = token->length == 1 ? token->text[0] : 0;
    decl->prev_star = c == '*';
    bool after_tag = decl->prev_tag;
    decl->prev_tag = false;
    if (token->tuple_brace) { // a tuple's definition is part of the head, as a parenthesis is
        if (c == '{') {
            lang->paren_depth += 1;
        } else if (lang->paren_depth > 0) {
            lang->paren_depth -= 1;
        }
        return;
    }
    if (c == '(') {
        if (decl->name_found && lang->paren_depth == 0 && lang->bracket_depth == 0) {
            decl->in_params = true;
            decl->is_function = true;
            lang->params_count = 0;
            lang->array_param_count = 0;
            lang->array_capture = -1;
            lang->array_after_close = false;
        }
        lang->paren_depth += 1;
    } else if (c == ')') {
        if (lang->paren_depth > 0) lang->paren_depth -= 1;
        if (lang->paren_depth == 0) decl->in_params = false;
    } else if (c == '[') {
        if (after_tag || lang->tag_attr > 0) lang->tag_attr += 1; // `struct [[attribute]] Name`: the name follows
        lang->bracket_depth += 1;
    } else if (c == ']') {
        if (lang->bracket_depth > 0) lang->bracket_depth -= 1;
        if (lang->tag_attr > 0) {
            lang->tag_attr -= 1;
            decl->prev_tag = lang->tag_attr == 0;
        }
    } else if (lang->paren_depth == 0 && lang->bracket_depth == 0) {
        if (c == ',') {
            decl->name_found = false;
        } else if (c == ';') {
            smith_lang_end_declaration(lang);
        } else if (c == '=') {
            smith_lang_head_initializer(lang);
        } else if (c == '{') {
            smith_lang_head_brace(lang, host, token);
        }
    }
}

// Where a name would be the one the declaration introduces.
static bool smith_lang_declarator_here(const SmithLang *lang) {
    const SmithLangDecl *decl = &lang->decl;
    if (!decl->saw_type || decl->name_found || decl->prev_tag) return false;
    if (lang->paren_depth == 0 && lang->bracket_depth == 0) return true;
    return lang->paren_depth == 1 && decl->prev_star && !decl->in_params && lang->bracket_depth == 0;
}

// A name that is not the module's and not the declarator: a tag, a sized type, a binding, a header's.
static SmithLangAct smith_lang_head_other_name(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                               bool may_peek) {
    SmithLangDecl *decl = &lang->decl;
    if (decl->prev_tag) { // the name after struct, union or enum
        if (!may_peek) return smith_lang_act(SmithLangAct_Slow);
        const SmithToken *next = nullptr;
        lang->generated_name = token->synthetic;
        SmithLangLowered lowered =
            smith_lang_lower_tag(lang, host, token->text, token->length, token->location, &next, decl->last_tag_word);
        lang->generated_name = false;
        decl->prev_tag = false;
        decl->saw_type = true;
        decl->prev_star = false;
        return lowered.replaced ? smith_lang_rename_act(lowered.text, lowered.length)
                                : smith_lang_act(SmithLangAct_Keep);
    }
    bool top = lang->paren_depth == 0 && lang->bracket_depth == 0; // inside [[ ]] or (( )) a name is no type
    if (!token->watched) { // nothing watches it: a type name of a header or the program's
        decl->prev_star = false;
        decl->saw_type = decl->saw_type || top;
        return smith_lang_act(SmithLangAct_Keep);
    }
    if (!may_peek) return smith_lang_act(SmithLangAct_Slow);
    decl->prev_star = false;
    decl->saw_type = decl->saw_type || top;
    smith_lang_sparse_ident(lang, host, token);
    return smith_lang_act(SmithLangAct_Done);
}

// An identifier of a head that is not a keyword.
static SmithLangAct smith_lang_head_name(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                         bool may_peek) {
    SmithLangDecl *decl = &lang->decl;
    if (token->watched && (smith_lang_previous_is(lang, host, 1, ".") || smith_lang_previous_is(lang, host, 1, "->"))) {
        return smith_lang_act(SmithLangAct_Keep);
    }
    if (!token->synthetic && smith_lang_declarator_here(lang)) { // the name the declaration introduces
        decl->name_found = true;
        decl->prev_star = false;
        if (!decl->is_typedef && (smith_lang_equals(token->text, token->length, "main") ||
                                  smith_lang_equals(token->text, token->length, smith_lang_io_main_name))) {
            return smith_lang_act(SmithLangAct_Keep);
        }
        const char *prefixed = smith_lang_declare(lang, host, token->text, token->length, token->location,
                                                  decl->is_typedef ? SmithLangName_Type : SmithLangName_Value, false);
        decl->declared += 1;
        return prefixed != nullptr ? smith_lang_rename_act(prefixed, lang->tmp_length)
                                   : smith_lang_act(SmithLangAct_Keep);
    }
    SmithLangName *known =
        token->watched && !decl->prev_tag ? smith_lang_name_find(lang, token->text, token->length) : nullptr;
    if (known == nullptr) return smith_lang_head_other_name(lang, host, token, may_peek);
    decl->prev_star = false;
    if (smith_lang_param_here(lang, known, token->text, token->length)) return smith_lang_act(SmithLangAct_Keep);
    if ((known->kind == SmithLangName_Type || known->kind == SmithLangName_Tag) && lang->paren_depth == 0 &&
        lang->bracket_depth == 0) {
        decl->saw_type = true;
    }
    const char *prefixed = smith_lang_prefixed(lang, token->text, token->length);
    return smith_lang_rename_act(prefixed, lang->tmp_length);
}

// A keyword of a head: what it says of the declaration.
static void smith_lang_head_keyword(SmithLang *lang, const SmithToken *token, SmithLangWord word) {
    SmithLangDecl *decl = &lang->decl;
    decl->prev_star = false;
    decl->prev_tag = word == SmithLangWord_Tag;
    if (word == SmithLangWord_Typedef) decl->is_typedef = true;
    if (word == SmithLangWord_TypeKeyword) decl->saw_type = true;
    if (word == SmithLangWord_Tag) {
        decl->aggregate = true;
        decl->last_tag_enum = smith_lang_ident(token, "enum");
        decl->last_tag_word = token->text[0];
    }
    if (smith_lang_ident(token, "constexpr")) decl->is_constexpr = true;
    if (smith_lang_ident(token, "extern")) decl->is_extern = true;
}

// An array parameter in a private head: the size is kept for `countof`, and the token after its `[` gets a `static`
// before it (an error when there is none). Runs for each token the head takes.
static void smith_lang_array_step(SmithLang *lang, const SmithHost *host, SmithToken *token, const SmithLangAct *act,
                                  bool may_peek, uint32_t bracket_before, bool after_name, bool size_next,
                                  bool *insert_static) {
    if (lang->decl.in_params && lang->paren_depth == 1) {
        bool renamed = act->kind == SmithLangAct_Rename;
        smith_lang_array_token(lang, host, renamed ? act->text : token->text, renamed ? act->length : token->length,
                               token->kind, bracket_before, lang->bracket_depth);
    }
    lang->decl.prev_ident =
        token->kind == SmithTokenKind_Identifier && smith_lang_word(token->text, token->length) == SmithLangWord_Other;
    lang->decl.param_bracket = false;
    if (!size_next) {
        if (smith_lang_punct(token, "[")) {
            lang->decl.param_bracket =
                after_name && lang->decl.in_params && lang->paren_depth == 1 && lang->bracket_depth == 1;
        }
        return;
    }
    SmithLangArray size = smith_lang_array_size(token);
    if (size == SmithLangArray_Unsized) smith_lang_unsized_error(lang, host, token->location);
    if (size != SmithLangArray_Sized) return;
    if (may_peek) {
        smith_lang_emit_code(lang, host, smith_lang_static_keyword(token));
        token->line_start = false;
        token->space_before = true;
    } else {
        *insert_static = true;
    }
}

// One token of a private declaration's head (the state Start is its first token), or Slow if it needs more. A Slow
// answer has changed nothing. With a look ahead allowed (the slow path), the `static` is written here, so that a
// name written by the lowering itself comes after it, and the token's layout is changed to follow.
static SmithLangAct smith_lang_head_step(SmithLang *lang, const SmithHost *host, SmithToken *token, bool may_peek,
                                         bool forced) {
    // A number's text is read where the host can look ahead: an array parameter's size is kept for countof.
    if (!may_peek && token->kind == SmithTokenKind_Number && lang->array_capture >= 0) {
        return smith_lang_act(SmithLangAct_Slow);
    }
    bool first = lang->state == SmithLangState_Start;
    bool seen_before = lang->seen_declaration;
    bool insert_static = false;
    if (first) {
        bool begun = false;
        SmithLangAct start = smith_lang_head_begin(lang, host, token, may_peek, forced, &begun);
        if (!begun) return start;
        insert_static = start.insert_static;
        if (insert_static && may_peek) {
            smith_lang_emit_code(lang, host, smith_lang_static_keyword(token));
            token->line_start = false;
            token->space_before = true;
            insert_static = false;
        }
    }
    bool after_attributes = !first && lang->decl.attribute_first && lang->bracket_depth == 0 &&
                            lang->paren_depth == 0 && !smith_lang_punct(token, "[");
    if (after_attributes) { // [[nodiscard]] int f(): the declaration begins here
        bool slow = false;
        bool wants = smith_lang_wants_static(lang, host, token, may_peek, &slow);
        if (slow) return smith_lang_act(SmithLangAct_Slow);
        lang->decl.attribute_first = false;
        lang->decl.static_inserted = wants;
        insert_static = wants;
        if (insert_static && may_peek) {
            smith_lang_emit_code(lang, host, smith_lang_static_keyword(token));
            token->line_start = false;
            token->space_before = true;
            insert_static = false;
        }
    }
    SmithLangAct act = smith_lang_act(SmithLangAct_Keep);
    bool after_name = lang->decl.prev_ident;
    uint32_t bracket_before = lang->bracket_depth;
    bool size_next = lang->decl.param_bracket;
    if (token->kind == SmithTokenKind_Identifier) {
        SmithLangWord word = smith_lang_word(token->text, token->length);
        if (word == SmithLangWord_Other) {
            act = smith_lang_head_name(lang, host, token, may_peek);
        } else {
            smith_lang_head_keyword(lang, token, word);
            if (smith_lang_ident(token, "static") && lang->decl.static_inserted && !first && lang->bracket_depth == 0) {
                act.kind = SmithLangAct_Drop;
            }
        }
    } else if (token->kind == SmithTokenKind_Punctuator) {
        smith_lang_head_punctuator(lang, host, token);
    } else {
        lang->decl.prev_star = false;
        lang->decl.prev_tag = false;
    }
    if (act.kind == SmithLangAct_Slow && first) { // the declaration did not begin after all
        lang->state = SmithLangState_Start;
        lang->seen_declaration = seen_before;
        return act;
    }
    if (act.kind != SmithLangAct_Slow) {
        smith_lang_array_step(lang, host, token, &act, may_peek, bracket_before, after_name, size_next, &insert_static);
    }
    act.insert_static = insert_static;
    return act;
}

// A sized type name where the ring decides it: Sized, Keep (a field or a tag), or Slow.
static SmithLangActKind smith_lang_sized_act(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                             const char **spelling) {
    const SmithLangSized *sized = smith_lang_find_sized(token->text, token->length);
    if (sized == nullptr) return SmithLangAct_Slow;
    if (lang->io_import && !lang->is_module && !lang->body_sparse &&
        smith_lang_equals(token->text, token->length, "i32")) {
        return SmithLangAct_Slow; // `i32 main(io::Io *io)` is read whole (runtime/lang_sparse.c)
    }
    SmithToken before;
    if (smith_lang_previous(lang, host, 1, &before)) {
        if (smith_lang_punct(&before, ".") || smith_lang_punct(&before, "->")) return SmithLangAct_Keep;
        if (before.kind == SmithTokenKind_Identifier &&
            smith_lang_word(before.text, before.length) == SmithLangWord_Tag) {
            return SmithLangAct_Keep;
        }
    }
    if (token->declared || (lang->statement_typedef && lang->brace_depth == lang->typedef_depth)) {
        return SmithLangAct_Slow; // the file's own name, perhaps
    }
    if (smith_lang_sparse_typeish(lang, host)) return SmithLangAct_Slow; // a declarator, perhaps
    if (lang->brace_depth > 0 && lang->braces[lang->brace_depth - 1] == SmithLangBrace_Enum &&
        (lang->prev_char == '{' || lang->prev_char == ',')) {
        return SmithLangAct_Slow; // an enumeration constant, perhaps
    }
    *spelling = sized->standard;
    return SmithLangAct_Sized;
}

static SmithLangAct smith_lang_sized_result(SmithLangActKind kind, const char *spelling) {
    SmithLangAct act = smith_lang_act(kind);
    if (kind == SmithLangAct_Sized) {
        act.text = spelling;
        act.length = (uint32_t)strlen(spelling);
    }
    return act;
}

// A token of a head, or the first of a declaration, when the host decides without looking ahead.
static SmithLangAct smith_lang_fast_declaring(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (lang->pending_export) return smith_lang_act(SmithLangAct_Slow);
    bool sized = token->kind == SmithTokenKind_Identifier && token->watched &&
                 smith_lang_find_sized(token->text, token->length) != nullptr &&
                 !(lang->state == SmithLangState_Declare && smith_lang_declarator_here(lang));
    if (sized && lang->decl.attribute_first && lang->bracket_depth == 0) return smith_lang_act(SmithLangAct_Slow);
    if (!sized) {
        SmithToken copy = *token;
        return smith_lang_head_step(lang, host, &copy, false, false);
    }
    const char *spelling = nullptr;
    SmithLangActKind kind = smith_lang_sized_act(lang, host, token, &spelling);
    if (kind != SmithLangAct_Sized) return smith_lang_act(SmithLangAct_Slow);
    SmithLangAct act = smith_lang_sized_result(kind, spelling);
    if (lang->state == SmithLangState_Start) {
        bool begun = false;
        SmithLangAct start = smith_lang_head_begin(lang, host, token, false, false, &begun);
        if (!begun) return start;
        act.insert_static = start.insert_static;
    }
    lang->decl.saw_type = true;
    lang->decl.prev_star = false;
    lang->decl.prev_tag = false;
    return act;
}

// The name the stream declares, without a look ahead: an enumeration's constant, or a declarator after an aggregate.
static SmithLangAct smith_lang_fast_stream_name(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangNameKind kind = SmithLangName_Value;
    bool top_enum = lang->brace_depth > 0 && lang->braces[lang->brace_depth - 1] == SmithLangBrace_Enum;
    if (top_enum && (lang->prev_char == '{' || lang->prev_char == ',') && lang->paren_depth == 0) {
        kind = SmithLangName_Constant;
    } else if (lang->brace_depth == 0 && lang->tail_declarator && lang->paren_depth == 0) {
        kind = lang->decl.is_typedef ? SmithLangName_Type : SmithLangName_Value;
        lang->tail_declarator = false;
    } else {
        smith_lang_track(lang, token, false);
        return smith_lang_act(SmithLangAct_Keep);
    }
    const char *prefixed = smith_lang_declare(lang, host, token->text, token->length, token->location, kind, false);
    smith_lang_track(lang, token, false);
    return prefixed != nullptr ? smith_lang_rename_act(prefixed, lang->tmp_length) : smith_lang_act(SmithLangAct_Keep);
}

// A token after a head (an aggregate's body, an initializer, what follows an aggregate), when the host decides it
// without a look ahead: the punctuation, the keywords, a name nothing watches, a sized type.
static SmithLangAct smith_lang_fast_stream(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (lang->decl.exported || lang->decl.checking || lang->pending_export || lang->in_function) {
        return smith_lang_act(SmithLangAct_Slow);
    }
    if (token->kind == SmithTokenKind_Punctuator) {
        char c = token->length == 1 ? token->text[0] : 0;
        if (c == '{') {
            smith_lang_open_state(lang, host, token);
        } else if (c == '}') {
            smith_lang_close_state(lang);
        } else {
            smith_lang_punctuator_state(lang, c);
            smith_lang_semicolon_state(lang, c);
        }
        smith_lang_track(lang, token, false);
        return smith_lang_act(SmithLangAct_Keep);
    }
    if (token->kind != SmithTokenKind_Identifier) {
        smith_lang_track(lang, token, false);
        return smith_lang_act(SmithLangAct_Keep);
    }
    SmithLangWord word = smith_lang_word(token->text, token->length);
    if (word != SmithLangWord_Other) {
        smith_lang_word_state(lang, token, word);
        smith_lang_track(lang, token, false);
        return smith_lang_act(SmithLangAct_Keep);
    }
    if (lang->prev_member) {
        smith_lang_track(lang, token, false);
        return smith_lang_act(SmithLangAct_Keep);
    }
    if (token->watched) {
        const char *spelling = nullptr;
        if (smith_lang_find_sized(token->text, token->length) == nullptr ||
            smith_lang_sized_act(lang, host, token, &spelling) != SmithLangAct_Sized) {
            return smith_lang_act(SmithLangAct_Slow);
        }
        smith_lang_track(lang, token, true);
        return smith_lang_sized_result(SmithLangAct_Sized, spelling);
    }
    if (lang->prev_tag) return smith_lang_act(SmithLangAct_Slow);
    return smith_lang_fast_stream_name(lang, host, token);
}

// What the host's fast path does with a token: Keep it, Rename it, make it a Sized type, Drop it (the second `static`),
// or Slow: not decided here, send it to smith_lang_feed.
SmithLangAct smith_lang_fast(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (lang->body_enabled) return smith_lang_act(SmithLangAct_Slow); // every token is written, for the body file
    // `option(` and `result(` are in scope in every module with no import: a file that writes one is read token by
    // token (runtime/lang_enum.c), as a file with a tuple is, because the fast path cannot see a use in the middle
    // of a declaration.
    if (host->maybe_shape && lang->is_module) return smith_lang_act(SmithLangAct_Slow);
    if (lang->broken || token->kind == SmithTokenKind_Eof || token->foreign) return smith_lang_act(SmithLangAct_Slow);
    lang->recent_count = 0;
    if (smith_lang_type_on(lang, host) && !lang->recording) return smith_lang_act(SmithLangAct_Slow); // lang_type.c
    if (token->kind == SmithTokenKind_Identifier && smith_lang_tuples_on(lang, host) &&
        (smith_lang_ident(token, "struct") || smith_lang_ident(token, "enum"))) {
        return smith_lang_act(SmithLangAct_Slow); // a tuple, an enum with payloads, is read ahead
    }
    if (lang->is_module && !lang->body_sparse) {
        if (lang->due_count > 0 && lang->state == SmithLangState_Start) return smith_lang_act(SmithLangAct_Slow);
        // An `if constexpr` around declarations, and every token of its branch (issue #335): the lowering counts the
        // braces and drops the branch's own.
        if (lang->ifc_count > 0 ||
            (host->maybe_if_constexpr && lang->state == SmithLangState_Start && smith_lang_ident(token, "if"))) {
            return smith_lang_act(SmithLangAct_Slow);
        }
        if (lang->state == SmithLangState_Stream) return smith_lang_fast_stream(lang, host, token);
        bool declaring = lang->state == SmithLangState_Start || lang->state == SmithLangState_Declare;
        return declaring ? smith_lang_fast_declaring(lang, host, token) : smith_lang_act(SmithLangAct_Slow);
    }
    if (lang->body_sparse && token->kind == SmithTokenKind_Punctuator) {
        if (!smith_lang_punct(token, "{") && !smith_lang_punct(token, "}")) return smith_lang_act(SmithLangAct_Slow);
        bool done = smith_lang_fast_brace(lang, host, smith_lang_punct(token, "{"));
        return smith_lang_act(done ? SmithLangAct_Keep : SmithLangAct_Slow);
    }
    if (token->kind != SmithTokenKind_Identifier) return smith_lang_act(SmithLangAct_Slow);
    if (lang->body_sparse) {
        if (smith_lang_previous_is(lang, host, 1, ".") || smith_lang_previous_is(lang, host, 1, "->")) {
            return smith_lang_act(SmithLangAct_Keep);
        }
        const SmithLangName *known = smith_lang_name_find(lang, token->text, token->length);
        if (known != nullptr && smith_lang_is_param(lang, token->text, token->length)) {
            return smith_lang_act(SmithLangAct_Keep); // the function's parameter, not the module's name
        }
        if (known != nullptr && known->generic != 0) return smith_lang_act(SmithLangAct_Slow); // a call: not a rename
        if (known != nullptr) {
            const char *prefixed = smith_lang_prefixed(lang, token->text, token->length);
            return smith_lang_rename_act(prefixed, lang->tmp_length);
        }
    }
    const char *spelling = nullptr;
    SmithLangActKind kind = smith_lang_sized_act(lang, host, token, &spelling);
    if (kind == SmithLangAct_Sized || kind == SmithLangAct_Keep) return smith_lang_sized_result(kind, spelling);
    return smith_lang_act(SmithLangAct_Slow);
}

// ---- The slow path: smith_lang_feed, with a look ahead allowed
// ------------------------------------------------------------

// A token of a private declaration's head when the host did not answer it (or it begins one).
static void smith_lang_declare_process(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithToken shown = *token;
    bool forced = lang->state == SmithLangState_Start;
    SmithLangAct act = smith_lang_head_step(lang, host, &shown, true, forced);
    SMITH_ASSERT(act.kind != SmithLangAct_Slow);
    if (act.kind == SmithLangAct_Rename) {
        smith_lang_emit(lang, host, smith_lang_synth(SmithTokenKind_Identifier, act.text, act.length, &shown));
    } else if (act.kind == SmithLangAct_Keep) {
        smith_lang_emit_kept(lang, host, &shown);
    }
}
