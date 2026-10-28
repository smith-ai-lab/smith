// Everything that is not a head: function bodies, aggregate bodies, initializers and the declarators after an
// aggregate, plus the start of a declaration, the end of the file and the entry point. Included by runtime/lang.c,
// after runtime/lang_head.c.

// The name of an enumeration constant, or of a declarator after an aggregate's closing brace: names the module
// introduces. Returns false when this identifier is neither.
static bool smith_lang_stream_declared(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!lang->is_module || lang->in_function || lang->paren_depth != 0) return false;
    SmithLangNameKind kind = SmithLangName_Value;
    if (lang->brace_depth > 0 && lang->braces[lang->brace_depth - 1] == SmithLangBrace_Enum &&
        (lang->prev_char == '{' || lang->prev_char == ',')) {
        kind = SmithLangName_Constant;
    } else if (lang->brace_depth == 0 && lang->tail_declarator) {
        const SmithToken *next = smith_lang_peek(lang, host, 0, false);
        if (!smith_lang_follows_declarator(next) && !smith_lang_punct(next, "(")) return false;
        kind = lang->decl.is_typedef ? SmithLangName_Type : SmithLangName_Value;
        lang->tail_declarator = false;
    } else {
        return false;
    }
    const char *prefixed =
        smith_lang_declare(lang, host, token->text, token->length, token->location, kind, lang->decl.exported);
    if (prefixed == nullptr) {
        smith_lang_emit_kept(lang, host, token);
    } else {
        SmithToken renamed = smith_lang_synth(SmithTokenKind_Identifier, prefixed, lang->tmp_length, token);
        smith_lang_emit(lang, host, renamed);
    }
    return true;
}

// runtime/lang_generic.c
static void smith_lang_generic_advance(SmithLang *lang, const SmithHost *host, const SmithToken *token);
static void smith_lang_generic_unclosed(SmithLang *lang, const SmithHost *host);
static bool smith_lang_generic_if_constexpr(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                            SmithLangWord word);
static bool smith_lang_generic_for_constexpr(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                             SmithLangWord word);
static void smith_lang_for_constexpr_outside(SmithLang *lang, const SmithHost *host, const SmithToken *token);

static void smith_lang_locals_write(SmithLang *lang, const SmithHost *host);

static void smith_lang_stream_identifier(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangWord word = smith_lang_word(token->text, token->length);
    if (word == SmithLangWord_Other && lang->statement_typedef) smith_lang_note_own_word(lang, token);
    if (word != SmithLangWord_Other) {
        if (lang->recording && smith_lang_ident(token, "if") &&
            smith_lang_generic_if_constexpr(lang, host, token, word)) {
            return;
        }
        if (lang->recording && host->maybe_if_constexpr && smith_lang_ident(token, "for") &&
            smith_lang_generic_for_constexpr(lang, host, token, word)) {
            return;
        }
        smith_lang_word_state(lang, token, word);
        smith_lang_emit_kept(lang, host, token);
        smith_lang_track(lang, token, false);
        return;
    }
    SmithToken resolved;
    if (lang->bindings_count > 0 && !lang->prev_member) {
        SmithLangBinding *binding = smith_lang_binding_find(lang, token->text, token->length);
        if (binding != nullptr) {
            if (smith_lang_namespace(lang, host, token, binding, &resolved)) {
                if (lang->namespace_done) { // a generic call, written with its arguments
                    lang->namespace_done = false;
                    return;
                }
                smith_lang_emit(lang, host, resolved);
                bool is_type = !binding->is_c &&
                               host->lookup(host->context, resolved.text, resolved.length, SmithLookup_Type).kind ==
                                   SmithLookup_Type;
                smith_lang_track(lang, &resolved, is_type);
                return;
            }
        }
    }
    if (lang->is_module && !lang->prev_member) {
        const SmithLangName *known = smith_lang_name_find(lang, token->text, token->length);
        if (known != nullptr && known->generic != 0 && smith_lang_generic_call_here(lang, host, token, known)) return;
    }
    if (smith_lang_stream_declared(lang, host, token)) {
        smith_lang_track(lang, token, false);
        return;
    }
    SmithLangContext context = smith_lang_context(lang, host, true);
    lang->generated_name = token->synthetic; // a name the lowering wrote (`key_Key`) is no C name of the user's
    SmithLangLowered lowered =
        smith_lang_lower(lang, host, token->text, token->length, token->location, &context, nullptr);
    lang->generated_name = false;
    if (lowered.replaced) {
        SmithToken renamed = smith_lang_synth(SmithTokenKind_Identifier, lowered.text, lowered.length, token);
        renamed.expand = lowered.expand;
        renamed.header_text = lowered.header;
        renamed.header_length = (uint32_t)lowered.header_length;
        smith_lang_emit(lang, host, renamed);
    } else {
        smith_lang_emit_kept(lang, host, token);
    }
    smith_lang_track(lang, token, lowered.is_type);
}

static void smith_lang_stream_open(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    smith_lang_open_state(lang, host, token);
    smith_lang_emit_kept(lang, host, token);
}

static void smith_lang_stream_close(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    smith_lang_emit_kept(lang, host, token);
    smith_lang_close_state(lang);
}

static void smith_lang_stream_punctuator(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    char c = token->length == 1 ? token->text[0] : 0;
    if (c == '{') {
        smith_lang_stream_open(lang, host, token);
    } else if (c == '}') {
        smith_lang_stream_close(lang, host, token);
    } else {
        smith_lang_punctuator_state(lang, c);
        smith_lang_emit_kept(lang, host, token);
        if (c == ';' && lang->local_count > 0 && lang->paren_depth == 0) smith_lang_locals_write(lang, host);
        smith_lang_semicolon_state(lang, c);
    }
    smith_lang_track(lang, token, false);
}

static void smith_lang_stream(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (token->kind == SmithTokenKind_Identifier) {
        smith_lang_stream_identifier(lang, host, token);
    } else if (token->kind == SmithTokenKind_Punctuator) {
        smith_lang_stream_punctuator(lang, host, token);
    } else {
        smith_lang_emit_kept(lang, host, token);
        smith_lang_track(lang, token, false);
    }
}

// ---- The start of a declaration
// ---------------------------------------------------------------------------------------------

static void smith_lang_reset_declaration(SmithLang *lang) {
    lang->decl = (SmithLangDecl){};
    lang->decl.exported = lang->pending_export;
    lang->decl.checking = lang->pending_export;
    lang->decl.generic = lang->generic_next; // a generic is not written to the header: no comment or #if goes for it
    lang->decl.plain = lang->generic_plain;
    lang->generic_next = false;
    lang->generic_plain = false;
    lang->decl.result_call = lang->generic_result_call;
    lang->generic_result_call = false;
    if (lang->pending_export) {
        lang->decl.c_name = lang->pending_c_name;
        lang->decl.c_name_location = lang->pending_c_name_location;
    }
    lang->pending_c_name = (SmithLangText){0, 0};
    lang->export_active = lang->pending_export && !lang->decl.generic;
    if (lang->decl.exported || lang->decl.generic) {
        lang->private_active = false;
        lang->priv_range = 0;
    } else {
        smith_lang_priv_begin(lang);
    }
    if (lang->pending_export && !lang->decl.generic) {
        smith_lang_trivia_export(lang);
    } else {
        lang->doc_pending = false;
    }
    lang->pending_export = false;
    lang->head_count = 0;
    lang->scratch_used = 0;
    lang->pending = SIZE_MAX;
    lang->paren_depth = 0;
    lang->bracket_depth = 0;
    lang->init_top = false;
    lang->tail_declarator = false;
    lang->agg_pending = false;
}

// ---- `main` in a module
// ----------------------------------------------------------------------------------------------------
//
// In a file with a `module` line `main` is `i32 main()` or `i32 main(str args[:])` (the io form is lang_head.c). The
// first is `int main(void)`. The second is `int main(int argc, char **argv)` whose body begins with the statements that
// make `args`: a stack array of argc slices that point into argv's own characters (nothing is allocated or copied, a
// slice's count is its strlen), written as source, so the rest of the lowering reads them like the program's own.

constexpr char smith_lang_main_wrong[] =
    "smith-main: main in a module takes no arguments or str args[:]; write i32 main(str args[:]) (the characters are "
    "not copied)";

// Whether the queue holds `main ( str NAME [ : ] ) {`.
static bool smith_lang_main_args_ahead(SmithLang *lang, const SmithHost *host) {
    return smith_lang_ident(smith_lang_peek(lang, host, 2, false), "str") &&
           smith_lang_peek(lang, host, 3, false)->kind == SmithTokenKind_Identifier &&
           smith_lang_punct(smith_lang_peek(lang, host, 4, false), "[") &&
           smith_lang_punct(smith_lang_peek(lang, host, 5, false), ":") &&
           smith_lang_punct(smith_lang_peek(lang, host, 6, false), "]") &&
           smith_lang_punct(smith_lang_peek(lang, host, 7, false), ")") &&
           smith_lang_punct(smith_lang_peek(lang, host, 8, false), "{");
}

// The words of the C head and of the statements that make `args`, without asking the compiler anything: it reads a
// batch only at its end, so the lowering cannot type a name this batch declares (a cut `slots[0:argc]` could not).
static const char *const smith_lang_main_head[] = {"main",
                                                   "(",
                                                   "int",
                                                   "smith_argc",
                                                   ",",
                                                   "char",
                                                   "*",
                                                   "*",
                                                   "smith_argv",
                                                   ")",
                                                   "{",
                                                   "typedef",
                                                   "typeof",
                                                   "(",
                                                   "const",
                                                   "char",
                                                   "[",
                                                   ":",
                                                   "]",
                                                   ")",
                                                   "SmithMainStr",
                                                   ";",
                                                   "@push",
                                                   "SmithMainStr",
                                                   "smith_main_slots",
                                                   "[",
                                                   "(",
                                                   "typeof",
                                                   "(",
                                                   "sizeof",
                                                   "0",
                                                   ")",
                                                   ")",
                                                   "smith_argc",
                                                   "+",
                                                   "1",
                                                   "]",
                                                   ";",
                                                   "for",
                                                   "(",
                                                   "int",
                                                   "smith_main_i",
                                                   "=",
                                                   "0",
                                                   ";",
                                                   "smith_main_i",
                                                   "<",
                                                   "smith_argc",
                                                   ";",
                                                   "smith_main_i",
                                                   "++",
                                                   ")",
                                                   "{",
                                                   "smith_main_slots",
                                                   "[",
                                                   "smith_main_i",
                                                   "]",
                                                   "=",
                                                   "(",
                                                   "SmithMainStr",
                                                   ")",
                                                   "{",
                                                   "smith_argv",
                                                   "[",
                                                   "smith_main_i",
                                                   "]",
                                                   ",",
                                                   "{",
                                                   "__builtin_strlen",
                                                   "(",
                                                   "smith_argv",
                                                   "[",
                                                   "smith_main_i",
                                                   "]",
                                                   ")",
                                                   "}",
                                                   "}",
                                                   ";",
                                                   "}",
                                                   "@pop",
                                                   "typeof",
                                                   "(",
                                                   "SmithMainStr",
                                                   "[",
                                                   ":",
                                                   "]",
                                                   ")",
                                                   "",
                                                   "=",
                                                   "{",
                                                   "smith_main_slots",
                                                   ",",
                                                   "{",
                                                   "(",
                                                   "typeof",
                                                   "(",
                                                   "sizeof",
                                                   "0",
                                                   ")",
                                                   ")",
                                                   "smith_argc",
                                                   "}",
                                                   "}",
                                                   ";"};

// The tokens of the words; the empty word is the program's name for `args`, `@push` and `@pop` bracket the one VLA with
// the warning off (`-Wvla` is on in a project that forbids variable-length arrays; this one is the lowering's).
static size_t smith_lang_main_make(const char *const *words, size_t count, const SmithToken *like,
                                   const SmithToken *name, SmithToken *made) {
    bool tight = true;
    size_t used = 0;
    for (size_t at = 0; at < count; at++) {
        const char *text = words[at];
        if (text[0] == '@') { // @push: the warning off; @pop: back
            used = smith_lang_enum_pragma(made, used, like,
                                          text[1] == 'p' && text[2] == 'u' ? "\"clang diagnostic push\""
                                                                           : "\"clang diagnostic pop\"");
            if (text[2] == 'u')
                used = smith_lang_enum_pragma(made, used, like, "\"clang diagnostic ignored \\\"-Wvla\\\"\"");
            continue;
        }
        if (text[0] == 0) {
            made[used] = *name;
            made[used++].space_before = true;
            tight = false;
            continue;
        }
        bool word = isalnum((unsigned char)text[0]) || text[0] == '_';
        made[used] = smith_lang_tuple_made(word ? SmithTokenKind_Identifier : SmithTokenKind_Punctuator, text,
                                           strlen(text), like, !tight && strchr("),;]:[", text[0]) == nullptr);
        if (isdigit((unsigned char)text[0])) made[used].kind = SmithTokenKind_Number;
        // Only `main` stands for a token of the source; counting the other ninety as the source's gave clangd's token
        // buffer a spelled token for each (issue #523: its walk of the expanded tokens never ended).
        made[used++].synthetic = strcmp(text, "main") != 0;
        tight = text[0] == '(' || text[0] == '[';
    }
    return used;
}

static void smith_lang_main_args_rewrite(SmithLang *lang, const SmithToken *name) {
    constexpr size_t count = sizeof smith_lang_main_head / sizeof smith_lang_main_head[0];
    static SmithToken made[count + 16]; // the pragmas are four tokens each
    SmithToken like = lang->queue[lang->queue_head];
    size_t used = smith_lang_main_make(smith_lang_main_head, count, &like, name, made);
    smith_lang_tuple_replace(lang, 9, made, used);
}

// `main ( )` becomes `main ( void )`.
static void smith_lang_main_void(SmithLang *lang, const SmithHost *host) {
    SmithToken made[4];
    for (size_t at = 0; at < 3; at++)
        made[at < 2 ? at : 3] = *smith_lang_peek(lang, host, at, false);
    made[2] = smith_lang_tuple_made(SmithTokenKind_Identifier, "void", 4, &made[1], false);
    smith_lang_tuple_replace(lang, 3, made, 4);
}

// The first token of a declaration: when it begins `main` of a module, checks the form and writes the C one.
static void smith_lang_module_main_begin(SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    if (!lang->is_module || (host->expr_type != nullptr && !host->maybe_main) || lang->brace_depth != 0 ||
        lang->recording)
        return;
    bool returns = smith_lang_ident(first, "i32") || smith_lang_ident(first, "int");
    if (!returns && !smith_lang_ident(first, "void")) return;
    const SmithToken *name = smith_lang_peek(lang, host, 0, false);
    if (!smith_lang_ident(name, "main") || !smith_lang_punct(smith_lang_peek(lang, host, 1, false), "(")) return;
    if (smith_lang_io_main_ahead(lang, host, first)) return; // the io form, until the io is gone
    if (!returns) {
        smith_lang_error(lang, host, first->location, "smith-main: main returns i32: write i32 main()");
        return;
    }
    const SmithToken *third = smith_lang_peek(lang, host, 2, false);
    if (smith_lang_punct(third, ")")) {
        smith_lang_main_void(lang, host);
    } else if (smith_lang_ident(third, "void") && smith_lang_punct(smith_lang_peek(lang, host, 3, false), ")")) {
        return;
    } else if (smith_lang_main_args_ahead(lang, host)) {
        SmithToken args = *smith_lang_peek(lang, host, 3, false);
        smith_lang_main_args_rewrite(lang, &args);
    } else {
        smith_lang_error(lang, host, name->location, "%s", smith_lang_main_wrong);
    }
}

static void smith_lang_begin_declaration(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    // A raw run that writes a header keeps the private declarations (not the one that prints the lowering).
    lang->tee = host->previous == nullptr && host->expr_type == nullptr && !lang->dump_enabled;
    lang->generic_result_call = false;
    lang->generic_plain = false;
    lang->generic_next = smith_lang_generic_candidate(lang, host, token) && smith_lang_generic_ahead(lang, host, token);
    SmithToken shown = *token;
    smith_lang_module_main_begin(lang, host, token);
    if (smith_lang_io_main_begin(lang, host, token) && !lang->is_module) { // a file's own functions are not static
        smith_lang_emit(lang, host, smith_lang_static_keyword(token));
        shown.line_start = false;
        shown.space_before = true;
        token = &shown;
    }
    // A module with private names that have a symbol holds every head (as an export's), to know which is one of them.
    if (lang->is_module && host->previous != nullptr && !lang->pending_export && !lang->generic_next &&
        lang->linked_count == 0) {
        smith_lang_declare_process(lang, host, token); // a private one streams
        return;
    }
    lang->seen_declaration = true;
    smith_lang_reset_declaration(lang);
    lang->state = lang->is_module ? SmithLangState_Head : SmithLangState_Stream;
    if (lang->state == SmithLangState_Head) {
        smith_lang_head(lang, host, token);
    } else {
        smith_lang_stream(lang, host, token);
    }
}

static bool smith_lang_name_char(char c);

// ---- [[smith::c_name("...")]]
// ---------------------------------------------------------------------------------------------------

// Whether the tokens from `at` on are `[smith::c_name`: the attribute that gives the export after it a C name.
static bool smith_lang_c_name_at(SmithLang *lang, const SmithHost *host, size_t at) {
    return smith_lang_punct(smith_lang_peek(lang, host, at, false), "[") &&
           (smith_lang_ident(smith_lang_peek(lang, host, at + 1, false), "smith") ||
            smith_lang_ident(smith_lang_peek(lang, host, at + 1, false), "std")) &&
           smith_lang_punct(smith_lang_peek(lang, host, at + 2, false), "::") &&
           smith_lang_ident(smith_lang_peek(lang, host, at + 3, false), "c_name");
}

// The name between the quotes, if it is a C identifier that is no keyword; else the error.
static bool smith_lang_c_name_text(SmithLang *lang, const SmithHost *host, uint32_t location, const char *quoted,
                                   size_t quoted_length, char *out, size_t capacity) {
    bool plain = quoted_length >= 3 && quoted[0] == '"' && quoted[quoted_length - 1] == '"';
    size_t length = plain ? quoted_length - 2 : 0;
    const char *text = quoted + 1;
    bool identifier = plain && length < capacity && !(text[0] >= '0' && text[0] <= '9');
    for (size_t i = 0; identifier && i < length; i++) { // the characters of the name
        identifier = smith_lang_name_char(text[i]) && text[i] != '$';
    }
    if (identifier) {
        memcpy(out, text, length);
        out[length] = 0;
        identifier = smith_lang_word(out, length) == SmithLangWord_Other;
    }
    if (!identifier) {
        smith_lang_error(lang, host, location,
                         "[[smith::c_name]] needs a C identifier that is no keyword, as in "
                         "[[smith::c_name(\"SmithArena\")]]");
    }
    return identifier;
}

// After `[[smith::c_name`: `(` string `)` `]` `]`. Gives the name through `name`; false, with the error reported and
// the attribute skipped, when it is some other shape.
static bool smith_lang_c_name_argument(SmithLang *lang, const SmithHost *host, uint32_t location, char *name,
                                       size_t capacity) {
    char quoted[smith_lang_name_max] = "";
    uint32_t string_location = location;
    bool shaped = true;
    for (size_t i = 0; i < 5; i++) { // `(` string `)` `]` `]`, read while they fit
        const SmithToken *part = smith_lang_peek(lang, host, 0, false);
        bool fits = i == 0   ? smith_lang_punct(part, "(")
                    : i == 1 ? part->kind == SmithTokenKind_String
                    : i == 2 ? smith_lang_punct(part, ")")
                             : smith_lang_punct(part, "]");
        if (!fits) {
            shaped = false;
            break;
        }
        if (i == 1) { // the host's text is valid only until it is asked for the next token
            string_location = part->location;
            if (part->length < sizeof quoted) snprintf(quoted, sizeof quoted, "%.*s", (int)part->length, part->text);
        }
        smith_lang_take(lang, host);
    }
    if (shaped) return smith_lang_c_name_text(lang, host, string_location, quoted, strlen(quoted), name, capacity);
    smith_lang_error(lang, host, location, "write [[smith::c_name(\"Name\")]]: the C name is one string");
    for (size_t guard = 0; guard < smith_lang_batch_max; guard++) { // to the end of the attribute
        const SmithToken *next = smith_lang_peek(lang, host, 0, false);
        if (next->kind == SmithTokenKind_Eof || smith_lang_punct(next, ";")) break;
        bool close = smith_lang_punct(next, "]") && smith_lang_punct(smith_lang_peek(lang, host, 1, false), "]");
        smith_lang_take(lang, host);
        if (close) {
            smith_lang_take(lang, host);
            break;
        }
    }
    return false;
}

// Reads `[[smith::c_name("Name")]]` (the first `[` is the token being processed) and keeps the name for the export
// that must follow. Returns false when these tokens are some other attribute, which then goes on as a declaration.
static bool smith_lang_c_name_attribute(SmithLang *lang, const SmithHost *host, const SmithToken *open) {
    if (!smith_lang_c_name_at(lang, host, 0)) return false;
    smith_lang_doc_capture(lang);
    uint32_t location = open->location;
    bool line_start = open->line_start;
    for (size_t attributes = 0; attributes < 8; attributes++) { // a second one is the error "one C name"
        for (int i = 0; i < 4; i++)                             // `[` `smith` `::` `c_name`
            smith_lang_take(lang, host);
        char name[smith_lang_name_max];
        bool good = smith_lang_c_name_argument(lang, host, location, name, sizeof name);
        if (good && lang->pending_c_name.length > 0) {
            smith_lang_error(lang, host, location, "an export has one C name");
        } else if (good) {
            lang->pending_c_name = smith_lang_store(lang, name, strlen(name));
            lang->pending_c_name_location = location;
            lang->pending_c_name_line = line_start; // the attribute began the line the header gives the export
        }
        if (!smith_lang_c_name_at(lang, host, 1)) break;
        line_start = smith_lang_peek(lang, host, 0, false)->line_start;
        location = smith_lang_peek(lang, host, 0, false)->location;
        smith_lang_take(lang, host); // the `[` of the next attribute
    }
    const SmithToken *after = smith_lang_peek(lang, host, 0, false);
    if (!smith_lang_ident(after, "export") || smith_lang_is_own_word(lang, host, after)) {
        smith_lang_error(lang, host, open->location,
                         "[[smith::c_name]] goes before `export`: it names an export's C API");
        lang->pending_c_name = (SmithLangText){0, 0};
    }
    smith_lang_ty_attribute_taken();
    return true;
}

// ---- A module's tags are type names
// ---------------------------------------------------------------------------------------------
//
// `struct Node { Node *next; };` in a module: the tag `Node` is a type name too (issue #333). It lowers to plain C: a
// tag declared at file scope has its typedef (`typedef struct m_Node m_Node;`) written before the next declaration,
// where the definition is complete (an enum cannot be named before), and in the generated header after an exported
// tag. Until then, in the declaration's own braces, the bare name is written `struct m_Node`.

static const char *smith_lang_tag_keyword(char word) {
    return word == 'u' ? "union" : word == 'e' ? "enum" : "struct";
}

static void smith_lang_typedefs_write(SmithLang *lang, const SmithHost *host) {
    bool was_exported = lang->export_active;
    for (uint32_t at = 0; at < lang->due_count; at++) { // each tag the last declaration defined
        SmithLangName *entry = &lang->names[lang->due[at]];
        entry->typedef_due = false;
        char name[smith_lang_name_max];
        char tag[smith_lang_name_max];
        const char *prefixed = smith_lang_prefixed(lang, smith_lang_str(entry->name), entry->name.length);
        snprintf(name, sizeof name, "%s", prefixed);
        snprintf(tag, sizeof tag, "%s", entry->tag_spelling.length > 0 ? smith_lang_str(entry->tag_spelling) : name);
        SmithToken like = {};
        like.location = entry->location;
        like.handle = smith_token_no_handle;
        SmithToken made[5];
        made[0] = smith_lang_tuple_made(SmithTokenKind_Identifier, "typedef", 7, &like, false);
        made[0].line_start = true;
        made[1] = smith_lang_tuple_made(SmithTokenKind_Identifier, smith_lang_tag_keyword(entry->tag_word),
                                        strlen(smith_lang_tag_keyword(entry->tag_word)), &like, true);
        made[2] = smith_lang_tuple_made(SmithTokenKind_Identifier, tag, strlen(tag), &like, true);
        made[3] = smith_lang_tuple_made(SmithTokenKind_Identifier, name, strlen(name), &like, true);
        made[4] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ";", 1, &like, false);
        lang->export_active = entry->exported;
        for (size_t i = 0; i < 5; i++) // the five tokens
            smith_lang_emit(lang, host, made[i]);
    }
    lang->export_active = was_exported;
    lang->due_count = 0;
}

// The typedef of a block's tag, after the `;` that ends its declaration: `typedef struct Local Local;`.
static void smith_lang_locals_write(SmithLang *lang, const SmithHost *host) {
    size_t kept = 0;
    for (uint32_t at = 0; at < lang->local_count; at++) {
        if (lang->locals[at].depth != lang->brace_depth) {
            lang->locals[kept++] = lang->locals[at];
            continue;
        }
        const char *name = smith_lang_str(lang->locals[at].name);
        SmithToken like = {};
        like.location = lang->locals[at].location;
        like.handle = smith_token_no_handle;
        const char *keyword = smith_lang_tag_keyword(lang->locals[at].word);
        SmithToken made[5];
        made[0] = smith_lang_tuple_made(SmithTokenKind_Identifier, "typedef", 7, &like, true);
        made[1] = smith_lang_tuple_made(SmithTokenKind_Identifier, keyword, strlen(keyword), &like, true);
        made[2] = smith_lang_tuple_made(SmithTokenKind_Identifier, name, strlen(name), &like, true);
        made[3] = smith_lang_tuple_made(SmithTokenKind_Identifier, name, strlen(name), &like, true);
        made[4] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ";", 1, &like, false);
        for (size_t i = 0; i < 5; i++) // the five tokens
            smith_lang_emit(lang, host, made[i]);
    }
    lang->local_count = (uint32_t)kept;
}

// A bare tag name inside the braces of the declaration that defined it, before its typedef is written: `struct Tag`.
static bool smith_lang_tag_bare(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (token->kind != SmithTokenKind_Identifier || token->synthetic || token->foreign || lang->prev_tag ||
        lang->prev_member || lang->brace_depth == 0 || lang->recording || smith_lang_capturing(lang)) {
        return false;
    }
    const SmithLangName *known = smith_lang_name_find(lang, token->text, token->length);
    char word = 0;
    if (known != nullptr && known->typedef_due && known->tag_spelling.length == 0) word = known->tag_word;
    for (uint32_t at = 0; at < lang->local_count && word == 0; at++) { // a block's tag, inside its own braces
        if (lang->brace_depth > lang->locals[at].depth &&
            smith_lang_equals(token->text, token->length, smith_lang_str(lang->locals[at].name))) {
            word = lang->locals[at].word;
        }
    }
    if (word == 0) return false;
    if (lang->braces[lang->brace_depth - 1] == SmithLangBrace_Aggregate) { // a member of that name is not the tag
        const SmithToken *next = smith_lang_peek(lang, host, 0, false);
        if (smith_lang_punct(next, ";") || smith_lang_punct(next, ",") || smith_lang_punct(next, "[") ||
            smith_lang_punct(next, ":")) {
            return false;
        }
    }
    SmithToken made[2];
    made[0] = smith_lang_tuple_made(SmithTokenKind_Identifier, smith_lang_tag_keyword(word),
                                    strlen(smith_lang_tag_keyword(word)), token, token->space_before);
    made[0].line_start = token->line_start;
    made[1] = *token;
    made[1].line_start = false;
    made[1].space_before = true;
    return smith_lang_tuple_replace(lang, 0, made, 2);
}

static void smith_lang_start(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (lang->due_count > 0) smith_lang_typedefs_write(lang, host);
    if (token->kind == SmithTokenKind_Identifier) {
        if (smith_lang_ident(token, "module") && smith_lang_is_module_line(lang, host, token)) {
            smith_lang_module_line(lang, host, token);
            return;
        }
        if (smith_lang_ident(token, "import") && smith_lang_is_import(lang, host, token)) {
            smith_lang_import(lang, host, token);
            return;
        }
        if (smith_lang_ident(token, "export") && !smith_lang_is_own_word(lang, host, token)) {
            if (!lang->is_module) {
                smith_lang_error(lang, host, token->location, "`export` needs a `module` line at the top of the file");
            }
            smith_lang_doc_capture(lang);
            lang->pending_export = true;
            lang->carry_line_start =
                token->line_start || (lang->pending_c_name.length > 0 && lang->pending_c_name_line);
            return;
        }
    }
    if (smith_lang_punct(token, ";")) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
    if (smith_lang_punct(token, "[") && smith_lang_c_name_attribute(lang, host, token)) return;
    smith_lang_begin_declaration(lang, host, token);
}

// ---- The header, the end of the file and the entry points
// ---------------------------------------------------------------------

static size_t smith_lang_append(char *buffer, size_t capacity, size_t used, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(buffer + used, capacity - used, format, arguments);
    va_end(arguments);
    return written > 0 && used + (size_t)written < capacity ? used + (size_t)written : used;
}

// The question to the compiler, answered by the program that runs the lowering when it can (smith.c, the header pass
// of a smith with clang inside): for each C name an export uses (lang->c_uses), the import that declared it as the
// module spells it (`<stdio.h>`), or null. False when it cannot be asked or the imports did not compile.
static bool (*smith_lang_import_asker)(const SmithLang *lang, const char **spellings) = nullptr;

// ---- What the header includes
//
// A module's header includes exactly what its exported declarations need, and nothing else: `<stddef.h>` and
// `<stdint.h>` (for the sized types), the imported modules whose names an export uses (`x::Name`), and the C headers
// imported (`import <h>;`) one of whose names an export uses (`c::Name`). An `#include`, and an import that only the
// bodies use, never reach it: a module whose exports need a C header's names `import`s it.

// The lines of the include body: the imports, and the conditional directives around them.
typedef enum { SmithLangLine_Import, SmithLangLine_Open, SmithLangLine_Branch, SmithLangLine_Close } SmithLangLineKind;

typedef struct {
    size_t start;
    size_t length;
    SmithLangLineKind kind;
    size_t
        chain; // Open: its own index; Branch and Close: the Open's; Import: the outermost chain it is in (or SIZE_MAX)
    size_t import;
} SmithLangLine;

constexpr size_t smith_lang_include_lines_max = 1024;

static size_t smith_lang_include_lines(const SmithLang *lang, SmithLangLine *lines) {
    size_t count = 0;
    size_t stack[smith_lang_cond_depth_max];
    size_t depth = 0;
    size_t import = 0;
    for (size_t at = 0; at < lang->include_used && count < smith_lang_include_lines_max;) { // each line
        size_t end = at;
        while (end < lang->include_used && lang->include_body[end] != '\n')
            end += 1;
        const char *text = lang->include_body + at;
        SmithLangLine *line = &lines[count++];
        *line = (SmithLangLine){.start = at, .length = end - at, .chain = SIZE_MAX};
        at = end + 1;
        if (line->length >= 8 && smith_lang_equals(text, 8, "#include")) {
            line->kind = SmithLangLine_Import;
            line->import = import++;
            line->chain = depth > 0 ? stack[0] : SIZE_MAX;
            continue;
        }
        size_t word = 1; // a directive: `#` and its word
        while (word < line->length && (text[word] == ' ' || text[word] == '\t'))
            word += 1;
        size_t stop = word;
        while (stop < line->length && text[stop] >= 'a' && text[stop] <= 'z')
            stop += 1;
        const char *name = text + word;
        size_t length = stop - word;
        if (smith_lang_equals(name, length, "endif")) {
            line->kind = SmithLangLine_Close;
            line->chain = depth > 0 ? stack[--depth] : SIZE_MAX;
        } else if (smith_lang_equals(name, length, "else") || smith_lang_equals(name, length, "elif") ||
                   smith_lang_equals(name, length, "elifdef") || smith_lang_equals(name, length, "elifndef")) {
            line->kind = SmithLangLine_Branch;
            line->chain = depth > 0 ? stack[depth - 1] : SIZE_MAX;
        } else {
            line->kind = SmithLangLine_Open;
            line->chain = count - 1;
            if (depth < smith_lang_cond_depth_max) stack[depth++] = count - 1;
        }
    }
    return count;
}

// Which of the module's C imports the header keeps. None if no export uses a C name. If all the imports stand in one
// place (one import, or the spellings of one header in the branches of a conditional) the header has them all.
// Otherwise the compiler is asked which import declared each name (the host program's asker, when it has one: the
// header pass of a smith with clang inside); an import that declared a name is kept, with the others of its
// conditional chain (they are other spellings of it). Without an answer every import is kept: more than needed, and
// always sound.
static void smith_lang_resolve_imports(SmithLang *lang) {
    if (lang->imports_resolved) return;
    lang->imports_resolved = true;
    size_t count = lang->cimports_count;
    for (size_t i = 0; i < count; i++)
        lang->cimport_keep[i] = false;
    if (count == 0 || lang->c_uses_count == 0) return;
    static SmithLangLine lines[smith_lang_include_lines_max];
    size_t line_count = smith_lang_include_lines(lang, lines);
    size_t group[smith_lang_cimports_max]; // per import: its outermost chain, or its own line
    size_t groups = 0;
    for (size_t i = 0; i < line_count; i++) { // the imports
        if (lines[i].kind != SmithLangLine_Import || lines[i].import >= count) continue;
        size_t mine = lines[i].chain != SIZE_MAX ? lines[i].chain : i;
        group[lines[i].import] = mine;
        bool seen = false;
        for (size_t j = 0; j < lines[i].import; j++)
            seen = seen || group[j] == mine;
        if (!seen) groups += 1;
    }
    const char *spellings[smith_lang_cuses_max] = {nullptr};
    if (groups > 1 && smith_lang_import_asker != nullptr && smith_lang_import_asker(lang, spellings)) {
        for (size_t use = 0; use < lang->c_uses_count; use++) { // the imports that declared a name
            if (spellings[use] == nullptr) continue;
            for (size_t i = 0; i < count; i++) {
                char spelled[smith_lang_name_max + 4];
                snprintf(spelled, sizeof spelled, lang->cimport_angled[i] ? "<%s>" : "\"%s\"",
                         smith_lang_str(lang->cimport_text[i]));
                if (strcmp(spelled, spellings[use]) != 0) continue;
                for (size_t j = 0; j < count; j++)
                    lang->cimport_keep[j] = lang->cimport_keep[j] || group[j] == group[i];
            }
        }
        return;
    }
    for (size_t i = 0; i < count; i++)
        lang->cimport_keep[i] = true;
}

// The include lines of the header: the kept imports, and the conditionals that still hold one.
static size_t smith_lang_kept_includes(SmithLang *lang, char *out, size_t capacity, size_t used) {
    smith_lang_resolve_imports(lang);
    static SmithLangLine lines[smith_lang_include_lines_max];
    size_t line_count = smith_lang_include_lines(lang, lines);
    bool chain_kept[smith_lang_include_lines_max] = {false};
    size_t open[smith_lang_cond_depth_max];
    size_t depth = 0;
    for (size_t i = 0; i < line_count; i++) { // a chain is kept if a kept import lies in it, at any depth
        if (lines[i].kind == SmithLangLine_Open && depth < smith_lang_cond_depth_max) open[depth++] = i;
        if (lines[i].kind == SmithLangLine_Close && depth > 0) depth -= 1;
        if (lines[i].kind == SmithLangLine_Import && lines[i].import < lang->cimports_count &&
            lang->cimport_keep[lines[i].import]) {
            for (size_t j = 0; j < depth; j++)
                chain_kept[open[j]] = true;
        }
    }
    bool any_chain = false;
    for (size_t i = 0; i < line_count; i++)
        any_chain = any_chain || (lines[i].kind == SmithLangLine_Open && chain_kept[i]);
    if (any_chain) {
        used = smith_lang_target_include_prelude(lang, out, capacity, used);
        lang->include_prelude = lang->include_target_used;
    }
    for (size_t i = 0; i < line_count; i++) {
        bool keep = lines[i].kind == SmithLangLine_Import
                        ? lines[i].import < lang->cimports_count && lang->cimport_keep[lines[i].import]
                        : lines[i].chain != SIZE_MAX && chain_kept[lines[i].chain];
        if (keep) {
            used = smith_lang_append(out, capacity, used, "%.*s\n", (int)lines[i].length,
                                     lang->include_body + lines[i].start);
        }
    }
    return used;
}

// The path by which a header finds another module's header from its own folder: for smith::random importing
// smith::arena, `arena.h`; for game::main importing lib::text, `../lib/text.h`. A quoted include looks in the
// including file's folder first, so a header that names its imports this way needs no include path of its own, and
// the generated headers of one tree (include/smith, .smith/include/smith, an installed copy) find each other.
static void smith_lang_relative_header(const char *from, const char *to, char *path, size_t capacity) {
    size_t common = 0;  // the bytes of `to` that are folders `from` is in too
    size_t folders = 0; // the folders `from` has below the common ones
    for (size_t at = 0; from[at] != 0 && to[at] == from[at]; at++) { // bounded by the names
        if (from[at] == ':' && from[at + 1] == ':') common = at + 2;
    }
    for (size_t i = common; from[i] != 0; i++) { // bounded by the name
        if (from[i] == ':' && from[i + 1] == ':') {
            folders += 1;
            i += 1;
        }
    }
    size_t used = 0;
    for (size_t i = 0; i < folders && used + 4 < capacity; i++) { // bounded by the name
        smith_copy_bytes(path + used, "../", 3);
        used += 3;
    }
    char rest[smith_lang_name_max];
    smith_lang_module_to_path(to + common, rest, sizeof rest);
    snprintf(path + used, capacity - used, "%s", rest);
}

// The `#include` lines a module's interface needs. For the header file, a module is included relative to the header
// itself; for the preface an importer reads an interface in memory by, from the include path.
static size_t smith_lang_assemble_includes(SmithLang *lang, char *out, size_t capacity, bool relative) {
    size_t used = smith_lang_append(out, capacity, 0, "#include <stddef.h>\n#include <stdint.h>\n");
    for (size_t i = 0; i < lang->imports_count; i++) { // bounded by the table: the modules an export names
        const char *module = smith_lang_str(lang->smith_imports[i]);
        bool named = false;
        for (size_t j = 0; j < lang->bindings_count; j++) {
            const SmithLangBinding *binding = &lang->bindings[j];
            named =
                named || (!binding->is_c && binding->in_header && strcmp(smith_lang_str(binding->module), module) == 0);
        }
        if (!named) continue;
        char path[smith_lang_name_max];
        if (relative)
            smith_lang_relative_header(lang->module, module, path, sizeof path);
        else
            smith_lang_module_to_path(module, path, sizeof path);
        used = smith_lang_append(out, capacity, used, "#include \"%s\"\n", path);
    }
    return smith_lang_kept_includes(lang, out, capacity, used);
}

// What an importer reads of a module without reading its declarations: the module's name, the modules it imports
// (which is how a cycle is found) and the names it exports (which is how they are written back), as comment lines.
static size_t smith_lang_assemble_meta(const SmithLang *lang, char *out, size_t capacity) {
    size_t used = smith_lang_append(out, capacity, 0, "// smith-module: %s\n// smith-imports:", lang->module);
    for (size_t i = 0; i < lang->imports_count; i++) { // bounded by the table
        used = smith_lang_append(out, capacity, used, " %s", smith_lang_str(lang->smith_imports[i]));
    }
    used = smith_lang_append(out, capacity, used, "\n// smith-names:");
    for (size_t i = 0; i < lang->names_count; i++) { // the exports: what an importer reads as the names it can write
        const SmithLangName *name = &lang->names[i];
        if (!name->exported) continue;
        used = smith_lang_append(out, capacity, used, " %s", smith_lang_str(name->name));
        if (name->c_name.length > 0) used = smith_lang_append(out, capacity, used, "=%s", smith_lang_str(name->c_name));
    }
    used = smith_lang_append(out, capacity, used, "\n");
    if (lang->tuples_seen)
        used = smith_lang_append(out, capacity, used, "// smith-tuples\n"); // an importer indexes them
    if (lang->slices_seen) used = smith_lang_append(out, capacity, used, "// smith-slices\n");
    if (lang->enums_seen) used = smith_lang_append(out, capacity, used, "// smith-enums\n");
    used = smith_lang_types_line(lang, out, capacity, used);
    for (size_t i = 0; i < lang->body_names_count; i++) // the exported functions with a body: an importer may run them
        used = smith_lang_append(out, capacity, used, i == 0 ? "// smith-bodies: %s" : " %s",
                                 smith_lang_str(lang->body_names[i]));
    if (lang->body_names_count > 0) used = smith_lang_append(out, capacity, used, "\n");
    used = smith_lang_generic_lines(lang, out, capacity, used);
    used = smith_lang_eshape_lines(lang, out, capacity, used);
    used = smith_lang_priv_linked_line(lang, out, capacity, used);
    return smith_lang_priv_hidden_line(lang, out, capacity, used);
}

// The lines of the header that an importer reads are held whole against a formatter (`// clang-format off` before
// them, `on` after): `smith fmt` would reflow the long // smith-names: line. The readers of the first line
// (shim/hook.cpp, smith.c) skip this one.
static constexpr char smith_clang_format_off[] = "// clang-format off\n";

// The module's generated header: what C and C++ include to call it, and what importers read.
static void smith_lang_assemble_header(SmithLang *lang, const char *main_file) {
    char *out = smith_lang_header_full;
    size_t capacity = sizeof smith_lang_header_full;
    size_t used = smith_lang_append(out, capacity, 0, "%s// Generated by smith from %s. Do not edit.\n",
                                    smith_clang_format_off, main_file);
    smith_lang_priv_resolve(lang); // before the includes: what the section names through imports
    used += smith_lang_assemble_meta(lang, out + used, capacity - used);
    used = smith_lang_append(out, capacity, used, "// clang-format on\n");
    used = smith_lang_append(out, capacity, used, "#pragma once\n");
    used += smith_lang_assemble_includes(lang, out + used, capacity - used, true);
    lang->header_target_used &= ~lang->include_prelude; // the include lines defined these above
    used = smith_lang_target_header_prelude(lang, out, capacity, used);
    used = smith_lang_append(out, capacity, used, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n");
    smith_lang_tuple_hoist(smith_lang_header_body, &lang->header_used, smith_lang_header_max);
    used = smith_lang_append(out, capacity, used, "%.*s\n", (int)lang->header_used, smith_lang_header_body);
    used = smith_lang_priv_section(lang, out, capacity, used);
    used = smith_lang_append(out, capacity, used, "#ifdef __cplusplus\n}\n#endif\n");
    lang->header_full_used = used;
}

// The interface an importer reads when it finds the module by its source (runtime/lang_lex.c): the declarations on
// the lines the module has them (the first line is `#pragma once`), with no other line. The includes it needs and the
// comment lines are the host's to use, apart from it.
static void smith_lang_assemble_aligned(SmithLang *lang) {
    // An exported inline function whose `if constexpr` asks the target needs the facts defined above its #if lines.
    // They take the blank lines before the first declaration (the comments and the imports of the module's top), so the
    // declarations stay on their lines.
    char prelude[2048];
    size_t prelude_used = smith_lang_target_header_prelude(lang, prelude, sizeof prelude, 0);
    size_t lines = 0;
    for (size_t i = 0; i < prelude_used; i++)
        lines += prelude[i] == '\n';
    size_t blank = 0;
    while (blank < lang->header_used && smith_lang_header_body[blank] == '\n')
        blank += 1;
    size_t skip = blank >= lines ? lines : 0;
    lang->header_full_used =
        smith_lang_append(smith_lang_header_full, sizeof smith_lang_header_full, 0, "#pragma once\n%.*s%.*s\n",
                          (int)prelude_used, prelude, (int)(lang->header_used - skip), smith_lang_header_body + skip);
    lang->header_full_used =
        smith_lang_priv_section(lang, smith_lang_header_full, sizeof smith_lang_header_full, lang->header_full_used);
}

static void smith_lang_write_file(const char *path, const char *bytes, size_t length) {
    FILE *file = fopen(path, "wb");
    if (file == nullptr) return;
    fwrite(bytes, 1, length, file);
    fclose(file);
}

static void smith_lang_body_write(SmithLang *lang); // runtime/lang_body.c

void smith_lang_finish(SmithLang *lang) {
    if (lang->finished) return;
    lang->finished = true;
    if (lang->is_module) smith_lang_assemble_header(lang, lang->main_file);
    smith_lang_body_write(lang);
    const char *dump = getenv("SMITH_LANG_DUMP");
    if (lang->dump_enabled && dump != nullptr) {
        if (lang->dump_used < smith_lang_dump_max) smith_lang_dump[lang->dump_used++] = '\n';
        smith_lang_write_file(dump, smith_lang_dump, lang->dump_used);
    }
    const char *generics = getenv("SMITH_LANG_GENERICS");
    if (lang->gen_dump_on && generics != nullptr) {
        smith_lang_write_file(generics, smith_lang_gen_dump, lang->gen_dump_used);
    }
    const char *header = getenv("SMITH_LANG_HEADER");
    if (lang->is_module && header != nullptr)
        smith_lang_write_file(header, smith_lang_header_full, lang->header_full_used);
}

const char *smith_lang_header(const SmithLang *lang, size_t *length) {
    *length = lang->header_full_used;
    return smith_lang_header_full;
}

constexpr uint32_t smith_lang_ask_nodes = 4096;

// `?const T *` in SMITH_LANG_ASK: what the parser says of a type written as tokens (type_query).
static void smith_lang_ask_query(const SmithHost *host, const char *spelling, size_t length) {
    SmithTypeQuery query = {};
    if (host->type_query == nullptr || !host->type_query(host->context, spelling, (uint32_t)length, &query)) {
        fprintf(stderr, "query %.*s: unknown\n", (int)length, spelling);
        return;
    }
    fprintf(stderr, "query %.*s: %.*s", (int)length, spelling, (int)query.canonical_length, query.canonical);
    if (query.complete) {
        fprintf(stderr, ", size %llu alignment %llu", (unsigned long long)query.size,
                (unsigned long long)query.alignment);
    }
    static const char *const names[] = {"integer", "float", "signed", "pointer", "bool", "enum", "record"};
    const bool kinds[] = {query.is_integer, query.is_float, query.is_signed, query.is_pointer,
                          query.is_bool,    query.is_enum,  query.is_record};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) { // 7 kinds
        if (kinds[i]) fprintf(stderr, ", %s", names[i]);
    }
    fputc('\n', stderr);
}

// SMITH_LANG_ASK=Name,struct Tag: at the end of the file, what the parser says of those types (their size, alignment
// and fields). It exercises the questions that reflection will ask, and tests them.
// SMITH_LANG_ASK_IMPORTS=FILE,struct stat,EOF and SMITH_LANG_IMPORTS_OUT=path: at the end of the file, which `import`
// declared each name (the header as the import spells it, `<stdio.h>`), one `name<TAB>spelling` line each, to the
// path. The header pass asks this of a compile of the imports alone, to keep in a module's header only the imports its
// exports use (smith_lang_resolve_imports).
static void smith_lang_ask_imports(const SmithHost *host) {
    const char *list = getenv("SMITH_LANG_ASK_IMPORTS");
    const char *path = getenv("SMITH_LANG_IMPORTS_OUT");
    if (list == nullptr || path == nullptr || host->declared_by == nullptr) return;
    FILE *file = fopen(path, "w");
    if (file == nullptr) return;
    for (const char *at = list; *at != 0;) { // each entry
        const char *end = strchr(at, ',');
        size_t length = end != nullptr ? (size_t)(end - at) : strlen(at);
        const char *name = at;
        size_t name_length = length;
        SmithLookupKind space = SmithLookup_Value;
        static const char *const tags[] = {"struct ", "union ", "enum "};
        for (size_t i = 0; i < sizeof tags / sizeof tags[0]; i++) { // 3 entries
            size_t tag_length = strlen(tags[i]);
            if (length > tag_length && memcmp(at, tags[i], tag_length) == 0) {
                space = SmithLookup_Tag;
                name = at + tag_length;
                name_length = length - tag_length;
            }
        }
        const char *by = host->declared_by(host->context, name, (uint32_t)name_length, space);
        if (by == nullptr && space == SmithLookup_Value)
            by = host->declared_by(host->context, name, (uint32_t)name_length, SmithLookup_Type);
        fprintf(file, "%.*s\t%s\n", (int)length, at, by != nullptr ? by : "");
        at = end != nullptr ? end + 1 : at + length;
    }
    fclose(file);
}

// One leaf of a value, as SMITH_LANG_ASK shows it: `i32 -5`, `u8 255`, `f32 1.5`.
static void smith_lang_ask_leaf(const SmithValue *value) {
    if (value->kind == SmithValue_Float) {
        fprintf(stderr, "%s %.17g", value->is_f32 ? "f32" : "f64", value->real);
        return;
    }
    fprintf(stderr, "%c%u %s%llu", value->is_signed ? 'i' : 'u', value->bits, value->negative ? "-" : "",
            (unsigned long long)value->magnitude);
}

// `!expression` in SMITH_LANG_ASK: what the compiler evaluates the expression to (value_query), a struct as
// `{a, b}` and an array as `[a, b]`. Walks the tree with a stack of its own: smith's code has no recursion.
static void smith_lang_ask_value(const SmithHost *host, const char *text, size_t length) {
    static SmithValue nodes[smith_lang_ask_nodes];
    SmithValueQuery query = {.nodes = nodes, .capacity = smith_lang_ask_nodes};
    char expression[512];
    snprintf(expression, sizeof expression, "%.*s", (int)length, text);
    if (host->value_query == nullptr ||
        !host->value_query(host->context, expression, (uint32_t)strlen(expression), &query)) {
        fprintf(stderr, "value %s: error: %s\n", expression, query.why != nullptr ? query.why : "unknown");
        return;
    }
    fprintf(stderr, "value %s: ", expression);
    const SmithValue *stack[32];
    uint32_t next[32];
    size_t depth = 0;
    stack[0] = &query.root;
    next[0] = UINT32_MAX; // not yet opened
    depth = 1;
    for (size_t guard = 0; depth > 0 && guard < (size_t)smith_lang_ask_nodes * 4; guard++) { // bounded by the tree
        const SmithValue *value = stack[depth - 1];
        bool aggregate = value->kind == SmithValue_Struct || value->kind == SmithValue_Array;
        if (!aggregate) {
            smith_lang_ask_leaf(value);
            depth -= 1;
            continue;
        }
        if (next[depth - 1] == UINT32_MAX) {
            fputc(value->kind == SmithValue_Struct ? '{' : '[', stderr);
            next[depth - 1] = 0;
        }
        if (next[depth - 1] == value->count) {
            fputc(value->kind == SmithValue_Struct ? '}' : ']', stderr);
            depth -= 1;
            continue;
        }
        if (next[depth - 1] > 0) fputs(", ", stderr);
        if (depth == 32) return;
        stack[depth] = &value->items[next[depth - 1]++];
        next[depth] = UINT32_MAX;
        depth += 1;
    }
    fputc('\n', stderr);
}

static void smith_lang_ask(SmithLang *lang, const SmithHost *host) {
    smith_lang_ask_imports(host);
    const char *list = getenv("SMITH_LANG_ASK");
    if (list == nullptr || host->type_info == nullptr) return;
    for (const char *at = list; *at != 0;) { // each entry
        const char *end = strchr(at, ',');
        size_t length = end != nullptr ? (size_t)(end - at) : strlen(at);
        if (at[0] == '!') {
            smith_lang_ask_value(host, at + 1, length - 1);
            at = end != nullptr ? end + 1 : at + length;
            continue;
        }
        if (at[0] == '?') {
            smith_lang_ask_query(host, at + 1, length - 1);
            at = end != nullptr ? end + 1 : at + length;
            continue;
        }
        SmithLookupKind space = SmithLookup_Type;
        const char *name = at;
        static const char *const tags[] = {"struct ", "union ", "enum "};
        for (size_t i = 0; i < sizeof tags / sizeof tags[0]; i++) { // 3 entries
            size_t tag_length = strlen(tags[i]);
            if (length > tag_length && memcmp(at, tags[i], tag_length) == 0) {
                space = SmithLookup_Tag;
                name = at + tag_length;
            }
        }
        size_t name_length = length - (size_t)(name - at);
        SmithTypeInfo info = {};
        if (!host->type_info(host->context, name, (uint32_t)name_length, space, &info)) {
            fprintf(stderr, "ask %.*s: unknown\n", (int)length, at);
        } else {
            fprintf(stderr, "ask %.*s: size %llu alignment %llu fields %u\n", (int)length, at,
                    (unsigned long long)info.size, (unsigned long long)info.alignment, info.fields);
        }
        for (uint32_t i = 0; i < info.fields && host->field != nullptr; i++) { // the fields
            const char *field_name = "";
            const char *field_type = "";
            if (host->field(host->context, name, (uint32_t)name_length, space, i, &field_name, &field_type)) {
                fprintf(stderr, "ask   %s: %s\n", field_name, field_type);
            }
        }
        at = end != nullptr ? end + 1 : at + length;
    }
    (void)lang;
}

static void smith_lang_end_of_file(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (lang->state == SmithLangState_Head) {
        smith_lang_resolve(lang, host, token);
        smith_lang_emit_head(lang, host, true, false);
        smith_lang_end_declaration(lang);
    }
    if (lang->due_count > 0) smith_lang_typedefs_write(lang, host);
    smith_lang_body_note(lang);
    lang->body_closed = true; // what follows is the instances and the io main: not sections
    smith_lang_generic_instances(lang, host);
    smith_lang_finish_io_main(lang, host);
    smith_lang_ask(lang, host);
    smith_lang_finish(lang);
    smith_lang_emit_kept(lang, host, token);
}

static void smith_lang_dispatch(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (lang->body_sparse) {
        smith_lang_body(lang, host, token);
        smith_lang_remember(lang, token);
        return;
    }
    if (host->previous != nullptr && !lang->is_module) {
        smith_lang_sparse(lang, host, token);
        smith_lang_remember(lang, token);
        return;
    }
    if (token->foreign) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
    if (token->kind == SmithTokenKind_Eof) {
        smith_lang_end_of_file(lang, host, token);
        return;
    }
    switch (lang->state) {
    case SmithLangState_Start:
        smith_lang_start(lang, host, token);
        break;
    case SmithLangState_Head:
        smith_lang_head(lang, host, token);
        break;
    case SmithLangState_Declare:
        smith_lang_declare_process(lang, host, token);
        break;
    case SmithLangState_Stream:
        smith_lang_stream(lang, host, token);
        break;
    }
}

// `arithmetic x = 5;` (a concept where `auto` goes, issue #193): the declaration is `auto x = 5;` once the type of its
// initializer is known to be of the concept, else the error is at the name. True when the token was replaced.
static bool smith_lang_concept_local(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    bool judge = !lang->recording; // a generic's body is kept as it is written; its locals are judged by no one yet
    if (!smith_lang_concept_word(token) || token->synthetic || (judge && host->expr_type == nullptr)) return false;
    const SmithToken *name = smith_lang_peek(lang, host, 0, false);
    if (name->kind != SmithTokenKind_Identifier || smith_lang_is_word(name) ||
        !smith_lang_punct(smith_lang_peek(lang, host, 1, false), "=")) {
        return false;
    }
    SmithToken before;
    if (!judge) { // the token before is the last one kept
        const SmithLangGenToken *last = &smith_lang_gt.tokens[smith_lang_gt.count - 1];
        if (!(smith_lang_gt_punct(last, ";") || smith_lang_gt_punct(last, "{") || smith_lang_gt_punct(last, "}"))) {
            return false;
        }
    } else if (smith_lang_previous(lang, host, 1, &before) &&
               !(smith_lang_punct(&before, ";") || smith_lang_punct(&before, "{") || smith_lang_punct(&before, "}"))) {
        return false;
    }
    if (host->lookup(host->context, token->text, token->length, SmithLookup_Type).kind != SmithLookup_None ||
        host->lookup(host->context, token->text, token->length, SmithLookup_Value).kind != SmithLookup_None) {
        return false; // a name of the program's, which shadows the concept as it shadows any name
    }
    if (!judge) {
        SmithToken automatic = smith_lang_tuple_made(SmithTokenKind_Identifier, "auto", 4, token, token->space_before);
        automatic.line_start = token->line_start;
        return smith_lang_tuple_replace(lang, 0, &automatic, 1);
    }
    static SmithToken init[64];
    size_t count = 0;
    int depth = 0;
    for (size_t at = 2; at < 66; at++) { // the initializer, up to the `;`
        const SmithToken *next = smith_lang_peek(lang, host, at, false);
        if (next->kind == SmithTokenKind_Eof || (depth == 0 && smith_lang_punct(next, ";"))) break;
        depth += smith_lang_punct(next, "(") || smith_lang_punct(next, "[") || smith_lang_punct(next, "{") ? 1 : 0;
        depth -= smith_lang_punct(next, ")") || smith_lang_punct(next, "]") || smith_lang_punct(next, "}") ? 1 : 0;
        init[count++] = *next;
    }
    char text[smith_lang_slice_text_max];
    SmithExprType found = {};
    if (count == 0 || count == 64 || !smith_lang_slice_text(lang, init, count, text, sizeof text) ||
        !host->expr_type(host->context, text, (uint32_t)strlen(text), &found)) {
        return false; // not read here: the declaration stays, and the compiler says what it does not know
    }
    unsigned id = 0;
    for (size_t i = 0; i < sizeof smith_lang_concepts / sizeof smith_lang_concepts[0]; i++) { // 11 concepts
        if (smith_lang_equals(token->text, token->length, smith_lang_concepts[i])) id = (unsigned)i + 1;
    }
    if (!smith_lang_concept_holds(host, id, found.canonical)) {
        uint32_t place = init[0].location != 0 ? init[0].location : token->location; // at the initializer
        smith_lang_concept_why(lang, host, place, id, found.canonical);
        smith_lang_error(lang, host, place, "`%s` is not `%s`: the initializer of `%.*s` has that type", found.written,
                         smith_lang_concepts[id - 1], (int)name->length, name->text);
    } // and the declaration goes on as `auto`, so that nothing else is said about the name
    SmithToken automatic = smith_lang_tuple_made(SmithTokenKind_Identifier, "auto", 4, token, token->space_before);
    automatic.line_start = token->line_start;
    return smith_lang_tuple_replace(lang, 0, &automatic, 1);
}

// A token of a file with slices (runtime/lang_slice.c): true when a slice's rewrite took it.
static bool smith_lang_slice_process(SmithLang *lang, const SmithHost *host, const SmithToken *token, bool capturing) {
    if (smith_lang_punct(token, ";") && !token->synthetic) {
        lang->slice.armed[0] = 0;
        lang->slice.typeof_tag[0] = 0;
    }
    bool at_start = lang->slice.after_boundary;
    lang->slice.after_boundary = smith_lang_slice_boundary(token);
    if (lang->enums_seen && smith_lang_enum_token(lang, host, token)) return true;
    if (smith_lang_slice_str(lang, host, token)) return true;
    if (at_start && smith_lang_slice_begin(lang, host, token)) return true;
    smith_lang_slice_equals(lang, host, token);
    if (smith_lang_slice_init_compound(lang, host, token)) return true;
    if (token->kind == SmithTokenKind_Identifier && smith_lang_ident(token, "typeof") &&
        smith_lang_slice_typeof(lang, host, token)) {
        return true;
    }
    if (smith_lang_slice_chain(lang, host, token)) return true;
    if (!capturing && smith_lang_ident(token, "return")) smith_lang_slice_return(lang, host, token);
    return false;
}

// What `if constexpr` and the constants it names do with a token (runtime/lang_target.c), then the token's own path.
static void smith_lang_process_token(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    bool closes = false;
    if (!token->replay) smith_lang_effects_type(lang, host, token);
    if (token->replay) { // a function over `type` of an import, defined here: only its type values are lowered
        if (token->synthetic || !smith_lang_ty_token(lang, host, token)) smith_lang_emit_code(lang, host, *token);
        return;
    }
    if (smith_lang_try_token(lang, host, token)) return;
    if (smith_lang_unused_token(lang, host, token)) return;
    if ((lang->due_count > 0 || lang->local_count > 0) && smith_lang_tag_bare(lang, host, token)) return;
    if (smith_lang_enum_function(lang, host, token)) return; // before the type values: it reads `type f(...)` itself
    if (smith_lang_type_on(lang, host) && !token->foreign && !lang->recording && !lang->decl.generic &&
        smith_lang_ty_token(lang, host, token)) {
        return;
    }
    if (!token->foreign && token->kind == SmithTokenKind_Identifier &&
        (lang->recording || !smith_lang_capturing(lang)) && smith_lang_concept_local(lang, host, token)) {
        return;
    }
    if (!token->foreign && !token->synthetic && smith_lang_ident(token, "for") && lang->cap_frames == 0 &&
        smith_lang_slice_active(lang, host) && smith_lang_rfor(lang, host, token)) {
        return;
    }
    if (!token->foreign && lang->recording && smith_lang_ident(token, "struct"))
        smith_lang_tuple_in_generic(lang, host, token);
    if (!token->foreign && !lang->recording) {
        bool capturing = smith_lang_capturing(lang); // the arguments of a call to a generic are kept, and typed later
        if (smith_lang_slice_active(lang, host) && smith_lang_slice_process(lang, host, token, capturing)) return;
        if (token->kind == SmithTokenKind_Identifier) {
            if (smith_lang_ident(token, "enum") && !capturing && smith_lang_enum_begin(lang, host, token)) return;
            if (smith_lang_ident(token, "struct")) smith_lang_tuple_begin(lang, host, token);
            if (smith_lang_tuples_on(lang, host) && host->previous != nullptr && smith_lang_ident(token, "countof") &&
                !smith_lang_sparse_member(lang, host) && smith_lang_tuple_countof(lang, host, token)) {
                return;
            }
            if (!capturing && smith_lang_ident(token, "return") && smith_lang_tuples_on(lang, host)) {
                smith_lang_tuple_return(lang, host, token);
            }
            if (!capturing && smith_lang_ident(token, "if") && smith_lang_ifc_begin(lang, host, token)) return;
            if (!capturing && host->maybe_if_constexpr && smith_lang_ident(token, "for")) {
                smith_lang_for_constexpr_outside(lang, host, token);
            }
            if (!capturing && host->maybe_if_constexpr && smith_lang_ident(token, "constexpr")) {
                smith_lang_ifc_constant(lang, host);
            }
        } else if (smith_lang_punct(token, "[") && smith_lang_tuples_on(lang, host) &&
                   smith_lang_tuple_subscript(lang, host, token)) {
            return;
        } else if (!capturing && lang->ifc_count > 0 && token->kind == SmithTokenKind_Punctuator) {
            lang->ifc_braces += smith_lang_punct(token, "{") ? 1 : 0;
            lang->ifc_braces -= smith_lang_punct(token, "}") ? 1 : 0;
            closes = smith_lang_punct(token, "}") && lang->ifc_braces == lang->ifc[lang->ifc_count - 1].target;
        }
    }
    if (closes && lang->ifc[lang->ifc_count - 1].file_scope) { // a branch's `}` around declarations is dropped
        smith_lang_ifc_close(lang, host);
        if (lang->ifc_count == 0) smith_lang_field_after_imports(lang, host);
        return;
    }
    smith_lang_dispatch(lang, host, token);
    if (!token->foreign && (smith_lang_ident(token, "module") || smith_lang_ident(token, "import"))) {
        smith_lang_field_after_imports(lang, host);
        lang->slice.after_boundary = true; // a module or import line ends in a `;` that no token carries on
        smith_lang_eshape_start = true;
    }
    if (host->previous != nullptr && smith_lang_tuples_on(lang, host) &&
        (lang->recent_count == 0 || lang->recent[0].text != token->text)) {
        smith_lang_remember(lang, token); // `t[0]` asks for the tokens before it, and the host may not have them yet
    }
    if (closes) smith_lang_ifc_close(lang, host);
}

// A token, then the `}` a slice's range `for` adds when its body's ended (runtime/lang_rfor.c).
static void smith_lang_process(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    bool ends = smith_lang_rfor_count(lang, token);
    smith_lang_process_token(lang, host, token);
    if (ends) smith_lang_rfor_close(lang, token);
}

// One lowering per process at a time: a fresh one for each file.
SmithLang *smith_lang_new(void) {
    SmithLang *lang = &smith_lang_instance;
    memset(lang, 0, sizeof *lang);
    smith_lang_tuple_used = 0;
    smith_lang_enum_reset();
    smith_lang_unused_serial = 0;
    smith_lang_unused_has_last = false;
    smith_lang_effects_paren = false;
    smith_lang_brackets_extern_seen = false;
    smith_lang_ty_reset();
    smith_lang_word_buckets();
    lang->started = true;
    lang->slice.after_boundary = true;
    lang->pending = SIZE_MAX;
    lang->body_enabled = getenv("SMITH_LANG_BODY") != nullptr;
    lang->dump_enabled = getenv("SMITH_LANG_DUMP") != nullptr || lang->body_enabled;
    lang->gen_dump_on = getenv("SMITH_LANG_GENERICS") != nullptr;
    smith_lang_generics_reset();
    return lang;
}

bool smith_lang_feed(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (lang->broken) return false;
    if (lang->main_file == nullptr) lang->main_file = host->main_file;
    lang->host_now = host;
    lang->queue_head = 0;
    lang->queue_count = 0;
    lang->batch_in = 0;
    lang->recent_count = 0;
    lang->out_count = 0;
    lang->changed = false;
    lang->pending_kept_set = false;
    if (lang->unnamed_c_name) {
        lang->unnamed_c_name = false;
        smith_lang_error(lang, host, lang->unnamed_c_name_location,
                         "[[smith::c_name]] names a typedef, a tag, a function or a variable; this export declares "
                         "none");
    }
    lang->queue[0] = *token;
    lang->queue_count = 1;
    for (size_t guard = 0;
         (lang->queue_count > 0 || lang->call_top > 0) && guard < smith_lang_batch_max + smith_lang_gt_max;
         guard++) { // bounded by the batch and by a call's arguments
        SmithToken next = smith_lang_take(lang, host);
        if (next.kind == SmithTokenKind_Eof) smith_lang_generic_unclosed(lang, host);
        smith_lang_process(lang, host, &next);
        if (lang->advance_skip) {
            lang->advance_skip = false;
        } else {
            smith_lang_generic_advance(lang, host, &next);
        }
    }
    if (lang->broken && !lang->broken_reported) { // a table filled: say so, once, since the rest is passed through
        lang->broken_reported = true;
        smith_lang_error(lang, host, token->location,
                         "this file is larger than the lowering's tables (names, text, a declaration's head or the "
                         "nesting of braces); the rest of it is not lowered. Split the module");
    }
    bool replaced = lang->changed || lang->out_count != 1 || lang->batch_in != 1;
    if (!replaced) lang->pending_kept_set = false;
    if (replaced) smith_lang_flush_kept(lang, host);
    return replaced;
}

// ---- A C keyword where a name goes
// -----------------------------------------------------------------------------------------------
//
// `i32 double(i32 x)` names a function with a keyword. clang reads `double` as one more type specifier and reports
// "cannot combine with previous 'int'", then errors on everything after it. A keyword that follows a complete type
// (a sized type name, maybe with `*` or a qualifier) and cannot continue its specifiers is the program's name for
// something, and the host says so itself, once, at the name.

bool smith_lang_misused_keyword(const char *text, size_t length) {
    smith_lang_word_buckets();
    SmithLangWord word = smith_lang_word(text, length);
    if (word == SmithLangWord_Other || word == SmithLangWord_Qualifier || word == SmithLangWord_Storage) return false;
    // These follow a type legitimately: an attribute, an alignment, an assembler label, `__extension__`.
    static const char *const follows[] = {"_Alignas", "alignas",       "asm",          "__asm",
                                          "__asm__",  "__attribute__", "__extension__"};
    for (size_t i = 0; i < sizeof follows / sizeof follows[0]; i++) { // 7 entries
        if (smith_lang_equals(text, length, follows[i])) return false;
    }
    return true;
}

// What a keyword names, from the character after it and where it stands; null when that character cannot follow a
// declarator's name (then the keyword is not being used as one).
static const char *smith_lang_keyword_noun(char next, bool parameters, bool aggregate, bool typedef_statement) {
    if (next == '(') return "function";
    if (next != '=' && next != ';' && next != ',' && next != '[' && next != ')' && next != ':') return nullptr;
    if (typedef_statement) return "type";
    if (parameters && (next == ',' || next == ')')) return "parameter";
    if (aggregate) return "field";
    return "variable";
}

static bool smith_lang_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
}

// The character that follows, past spaces and comments.
static char smith_lang_next_char(const char *text, size_t size, size_t at) {
    for (size_t guard = 0; at < size && guard < size; guard++) { // each pass moves on
        char c = text[at];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            at += 1;
        } else if (c == '/' && at + 1 < size && text[at + 1] == '/') {
            while (at < size && text[at] != '\n')
                at += 1;
        } else if (c == '/' && at + 1 < size && text[at + 1] == '*') {
            at += 2;
            while (at + 1 < size && !(text[at] == '*' && text[at + 1] == '/'))
                at += 1;
            at += 2;
        } else {
            return c;
        }
    }
    return 0;
}

// Whether the word that ends at `end` (spaces before it allowed) is struct, union or enum, or such a word and a tag
// name come before it: the `{` after is an aggregate's body.
static bool smith_lang_aggregate_before(const char *text, size_t end) {
    for (int words = 0; words < 2; words++) {
        while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\n' || text[end - 1] == '\t'))
            end -= 1;
        size_t start = end;
        while (start > 0 && smith_lang_name_char(text[start - 1]))
            start -= 1;
        if (start == end) return false;
        SmithLangWord word = smith_lang_word(text + start, end - start);
        if (word == SmithLangWord_Tag) return true;
        if (word != SmithLangWord_Other) return false;
        end = start;
    }
    return false;
}

// The offset just after the last character of code before `start`: spaces and comments (a trailing `//` comment on a
// line before, a `/* */` block) are not code, so a keyword that begins a line after a comment is read as a statement.
static size_t smith_lang_code_before(const char *text, size_t start) {
    size_t before = start;
    while (before > 0) { // bounded by the text
        char c = text[before - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            before -= 1;
            continue;
        }
        if (c == '/' && before >= 2 && text[before - 2] == '*') { // the end of a block comment
            size_t at = before - 2;
            while (at >= 2 && !(text[at - 2] == '/' && text[at - 1] == '*'))
                at -= 1;
            if (at < 2) return before;
            before = at - 2;
            continue;
        }
        size_t line = before; // the start of this line, then a `//` in it
        while (line > 0 && text[line - 1] != '\n')
            line -= 1;
        size_t comment = before;
        for (size_t at = line; at + 1 < before; at++) { // bounded by the line
            if (text[at] == '/' && text[at + 1] == '/') {
                comment = at;
                break;
            }
        }
        if (comment == before) return before;
        before = comment;
    }
    return before;
}

// What the keyword at text[start, end) is being used to name, read from the text around it: a function, variable,
// parameter, field or type (null: it is not being used as a name). The statement before it is scanned back to its
// start or to the `{` that encloses it, a bounded distance.
const char *smith_lang_keyword_name_use(const char *text, size_t size, size_t start, size_t end) {
    if (start > size || end > size) return nullptr;
    char next = smith_lang_next_char(text, size, end);
    // A name is declared after a type, a qualifier or a `*`: after `)`, `,`, `(` or an operator the keyword is the
    // keyword (`(u32)sizeof(x)`, `typeof(x) y` after a parameter).
    size_t before = smith_lang_code_before(text, start);
    if (before > 0 && !smith_lang_name_char(text[before - 1]) && text[before - 1] != '*') return nullptr;
    bool parameters = false;
    bool aggregate = false;
    bool typedef_statement = false;
    int parens = 0;
    int braces = 0;
    size_t limit = start > 8192 ? start - 8192 : 0;
    bool statement = true; // still in the statement the keyword is in; after its start only the enclosing `{` matters
    for (size_t at = start; at > limit; at--) { // bounded by the window
        char c = text[at - 1];
        if (c == '}') braces += 1;
        if (c == '{' && braces > 0) braces -= 1;
        if (c == '{' && braces == 0) {
            aggregate = !parameters && smith_lang_aggregate_before(text, at - 1);
            break;
        }
        if (!statement) continue;
        if (c == ')') parens += 1;
        if (c == '(' && parens > 0) parens -= 1;
        if (c == '(' && parens == 0) parameters = true;
        if (c == ';' && parens == 0 && braces == 0) statement = false;
        if (c == 't' && braces == 0 && at - 1 + 7 <= size && memcmp(text + at - 1, "typedef", 7) == 0 &&
            (at == 1 || !smith_lang_name_char(text[at - 2])) && !smith_lang_name_char(text[at - 1 + 7])) {
            typedef_statement = true;
        }
    }
    return smith_lang_keyword_noun(next, parameters, aggregate, typedef_statement);
}

size_t smith_lang_keyword_message(char *out, size_t capacity, const char *keyword, size_t length, const char *noun) {
    int written = snprintf(out, capacity, "`%.*s` is a C keyword; it cannot name a %s", (int)length, keyword, noun);
    return written > 0 && (size_t)written < capacity ? (size_t)written : 0;
}

const char *smith_lang_module_name(const SmithLang *lang) {
    return lang->is_module ? lang->module : nullptr;
}

bool smith_lang_c_use_allowed(const SmithLang *lang, uint32_t location) {
    if (lang->ifc_dropping) return true;                  // a branch not taken names nothing
    for (size_t i = 0; i < smith_lang_allowed_max; i++) { // bounded by the table
        if (lang->allowed[i] == location) return true;
    }
    return false;
}

bool smith_lang_holds(const SmithLang *lang) {
    return lang->head_count > 0;
}

bool smith_lang_is_module(const SmithLang *lang) {
    return lang->is_module;
}

bool smith_lang_io_import(const SmithLang *lang) {
    return lang->io_import;
}

unsigned smith_lang_errors(const SmithLang *lang) {
    return lang->errors;
}
