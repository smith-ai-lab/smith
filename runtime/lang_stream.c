// Everything that is not a head: function bodies, aggregate bodies, initializers and the declarators after an
// aggregate, plus the start of a declaration, the end of the file and the entry point. Included by runtime/lang.c,
// after runtime/lang_head.c.

// An import binding used as more than a prefix: a declaration of the same name, or a bare value.
static void smith_lang_misuse(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    const SmithToken *next = smith_lang_peek(lang, host, 0, false);
    if (smith_lang_typeish(lang, host) && smith_lang_follows_declarator(next)) {
        smith_lang_error(lang, host, token->location,
                         "`%.*s` is the name of an import in this file; a declaration cannot reuse it",
                         (int)token->length, token->text);
        smith_lang_binding_find(lang, token->text, token->length)->shadowed = true;
    } else {
        smith_lang_error(lang, host, token->location, "`%.*s` is an import; reach what it holds with `%.*s.name`",
                         (int)token->length, token->text, (int)token->length, token->text);
    }
}

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

static void smith_lang_stream_identifier(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangWord word = smith_lang_word(token->text, token->length);
    if (word == SmithLangWord_Other && lang->statement_typedef) smith_lang_note_own_word(lang, token);
    if (word != SmithLangWord_Other) {
        if (word == SmithLangWord_Typedef) {
            lang->statement_typedef = true;
            lang->typedef_depth = lang->brace_depth;
        }
        if (word == SmithLangWord_Tag) {
            lang->agg_pending = true;
            lang->enum_pending = smith_lang_ident(token, "enum");
        }
        smith_lang_emit_kept(lang, host, token);
        smith_lang_track(lang, token, false);
        return;
    }
    SmithToken resolved;
    if (lang->bindings_count > 0 && !lang->prev_member) {
        SmithLangBinding *binding = smith_lang_binding_find(lang, token->text, token->length);
        if (binding != nullptr) {
            if (smith_lang_namespace(lang, host, token, binding, &resolved)) {
                smith_lang_emit(lang, host, resolved);
                bool is_type = !binding->is_c &&
                               host->lookup(host->context, resolved.text, resolved.length, SmithLookup_Type).kind ==
                                   SmithLookup_Type;
                smith_lang_track(lang, &resolved, is_type);
                return;
            }
            smith_lang_misuse(lang, host, token);
            smith_lang_emit_kept(lang, host, token);
            smith_lang_track(lang, token, false);
            return;
        }
    }
    if (smith_lang_stream_declared(lang, host, token)) {
        smith_lang_track(lang, token, false);
        return;
    }
    SmithLangContext context = smith_lang_context(lang, host, true);
    SmithLangLowered lowered =
        smith_lang_lower(lang, host, token->text, token->length, token->location, &context, nullptr);
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
    SmithLangBrace kind = SmithLangBrace_Block;
    if (lang->agg_pending) {
        kind = lang->enum_pending ? SmithLangBrace_Enum : SmithLangBrace_Aggregate;
    } else if (lang->brace_depth == 0) {
        kind = lang->init_top ? SmithLangBrace_Init : SmithLangBrace_Function;
        if (kind == SmithLangBrace_Function) lang->in_function = true;
    }
    lang->agg_pending = false;
    smith_lang_push_brace(lang, host, kind, token->location);
    smith_lang_emit_kept(lang, host, token);
}

static void smith_lang_stream_close(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    smith_lang_emit_kept(lang, host, token);
    if (lang->brace_depth == 0) return;
    SmithLangBrace kind = lang->braces[--lang->brace_depth];
    if (lang->brace_depth != 0) return;
    if (kind == SmithLangBrace_Function) {
        lang->in_function = false;
        smith_lang_end_declaration(lang);
    } else if (kind == SmithLangBrace_Aggregate || kind == SmithLangBrace_Enum) {
        lang->tail_declarator = true;
    }
}

static void smith_lang_stream_punctuator(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    char c = token->length > 0 ? token->text[0] : 0;
    if (token->length == 1 && c == '{') {
        smith_lang_stream_open(lang, host, token);
    } else if (token->length == 1 && c == '}') {
        smith_lang_stream_close(lang, host, token);
    } else {
        if (token->length == 1 && c == '(') lang->paren_depth += 1;
        if (token->length == 1 && c == ')' && lang->paren_depth > 0) lang->paren_depth -= 1;
        if (token->length == 1 && c == '=' && lang->brace_depth == 0) {
            lang->init_top = true;
            lang->tail_declarator = false;
        }
        if (token->length == 1 && c == ',' && lang->brace_depth == 0 && lang->paren_depth == 0 && !lang->init_top) {
            lang->tail_declarator = true;
        }
        if (token->length == 1 && (c == '(' || c == ')' || c == '=')) lang->agg_pending = false;
        smith_lang_emit_kept(lang, host, token);
        if (token->length == 1 && c == ';' && lang->paren_depth == 0) {
            lang->statement_typedef = false;
            lang->agg_pending = false;
            if (lang->brace_depth == 0) smith_lang_end_declaration(lang);
        }
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

static void smith_lang_begin_declaration(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    lang->seen_declaration = true;
    lang->decl = (SmithLangDecl){0};
    lang->decl.exported = lang->pending_export;
    lang->decl.checking = lang->pending_export;
    lang->export_active = lang->pending_export;
    lang->pending_export = false;
    lang->head_count = 0;
    lang->scratch_used = 0;
    lang->pending = SIZE_MAX;
    lang->paren_depth = 0;
    lang->bracket_depth = 0;
    lang->init_top = false;
    lang->tail_declarator = false;
    lang->agg_pending = false;
    lang->state = lang->is_module ? SmithLangState_Head : SmithLangState_Stream;
    if (lang->state == SmithLangState_Head) {
        smith_lang_head(lang, host, token);
    } else {
        smith_lang_stream(lang, host, token);
    }
}

static void smith_lang_start(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
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
            lang->pending_export = true;
            lang->carry_line_start = token->line_start;
            return;
        }
    }
    if (smith_lang_punct(token, ";")) {
        smith_lang_emit_kept(lang, host, token);
        return;
    }
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

// The module's generated header: what C and C++ include to call it, and what importers read.
static void smith_lang_assemble_header(SmithLang *lang, const char *main_file) {
    char *out = smith_lang_header_full;
    size_t capacity = sizeof smith_lang_header_full;
    size_t used = 0;
    used = smith_lang_append(out, capacity, used, "// Generated by smith from %s. Do not edit.\n", main_file);
    used = smith_lang_append(out, capacity, used, "// smith-module: %s\n// smith-imports:", lang->module);
    for (size_t i = 0; i < lang->imports_count; i++) { // bounded by the table
        used = smith_lang_append(out, capacity, used, " %s", smith_lang_str(lang->smith_imports[i]));
    }
    used = smith_lang_append(out, capacity, used, "\n#pragma once\n#include <stddef.h>\n#include <stdint.h>\n");
    for (size_t i = 0; i < lang->imports_count; i++) { // bounded by the table
        char path[smith_lang_name_max];
        smith_lang_dotted_to_path(smith_lang_str(lang->smith_imports[i]), path, sizeof path);
        used = smith_lang_append(out, capacity, used, "#include \"%s\"\n", path);
    }
    for (size_t i = 0; i < lang->c_imports_count; i++) { // bounded by the table
        bool angled = lang->c_import_angled[i];
        used = smith_lang_append(out, capacity, used, "#include %c%s%c\n", angled ? '<' : '"',
                                 smith_lang_str(lang->c_imports[i]), angled ? '>' : '"');
    }
    used = smith_lang_append(out, capacity, used, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n");
    used = smith_lang_append(out, capacity, used, "%.*s\n", (int)lang->header_used, smith_lang_header_body);
    used = smith_lang_append(out, capacity, used, "#ifdef __cplusplus\n}\n#endif\n");
    lang->header_full_used = used;
}

static void smith_lang_write_file(const char *path, const char *bytes, size_t length) {
    FILE *file = fopen(path, "wb");
    if (file == nullptr) return;
    fwrite(bytes, 1, length, file);
    fclose(file);
}

void smith_lang_finish(SmithLang *lang) {
    if (lang->finished) return;
    lang->finished = true;
    if (lang->is_module) smith_lang_assemble_header(lang, lang->main_file);
    const char *dump = getenv("SMITH_LANG_DUMP");
    if (lang->dump_enabled && dump != nullptr) {
        if (lang->dump_used < smith_lang_dump_max) smith_lang_dump[lang->dump_used++] = '\n';
        smith_lang_write_file(dump, smith_lang_dump, lang->dump_used);
    }
    const char *header = getenv("SMITH_LANG_HEADER");
    if (lang->is_module && header != nullptr)
        smith_lang_write_file(header, smith_lang_header_full, lang->header_full_used);
}

const char *smith_lang_header(const SmithLang *lang, size_t *length) {
    *length = lang->header_full_used;
    return smith_lang_header_full;
}

// SMITH_LANG_ASK=Name,struct Tag: at the end of the file, what the parser says of those types (their size, alignment
// and fields). It exercises the questions that reflection will ask, and tests them.
static void smith_lang_ask(SmithLang *lang, const SmithHost *host) {
    const char *list = getenv("SMITH_LANG_ASK");
    if (list == nullptr || host->type_info == nullptr) return;
    for (const char *at = list; *at != 0;) { // each entry
        const char *end = strchr(at, ',');
        size_t length = end != nullptr ? (size_t)(end - at) : strlen(at);
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
        SmithTypeInfo info = {0};
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
    smith_lang_ask(lang, host);
    smith_lang_finish(lang);
    smith_lang_emit_kept(lang, host, token);
}

static void smith_lang_process(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
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
    case SmithLangState_Stream:
        smith_lang_stream(lang, host, token);
        break;
    }
}

// One lowering per process at a time: a fresh one for each file.
SmithLang *smith_lang_new(void) {
    SmithLang *lang = &smith_lang_instance;
    memset(lang, 0, sizeof *lang);
    smith_lang_word_buckets();
    lang->started = true;
    lang->pending = SIZE_MAX;
    lang->dump_enabled = getenv("SMITH_LANG_DUMP") != nullptr;
    return lang;
}

bool smith_lang_feed(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (lang->broken) return false;
    if (lang->main_file == nullptr) lang->main_file = host->main_file;
    lang->queue_head = 0;
    lang->queue_count = 0;
    lang->batch_in = 0;
    lang->out_count = 0;
    lang->changed = false;
    lang->pending_kept_set = false;
    lang->queue[0] = *token;
    lang->queue_count = 1;
    for (size_t guard = 0; lang->queue_count > 0 && guard < smith_lang_batch_max; guard++) { // bounded by the batch
        SmithToken next = smith_lang_take(lang, host);
        smith_lang_process(lang, host, &next);
    }
    bool replaced = lang->changed || lang->out_count != 1 || lang->batch_in != 1;
    if (!replaced) lang->pending_kept_set = false;
    if (replaced) smith_lang_flush_kept(lang, host);
    return replaced;
}

bool smith_lang_c_use_allowed(const SmithLang *lang, uint32_t location) {
    for (size_t i = 0; i < lang->allowed_count; i++) { // bounded by the table
        if (lang->allowed[i] == location) return true;
    }
    return false;
}

bool smith_lang_is_module(const SmithLang *lang) {
    return lang->is_module;
}

unsigned smith_lang_errors(const SmithLang *lang) {
    return lang->errors;
}
