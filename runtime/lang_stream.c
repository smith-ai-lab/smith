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
    lang->generic_next = false;
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

static void smith_lang_begin_declaration(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    // A raw run that writes a header keeps the private declarations (not the one that prints the lowering).
    lang->tee = host->previous == nullptr && host->expr_type == nullptr && !lang->dump_enabled;
    lang->generic_next = smith_lang_generic_candidate(lang, host, token) && smith_lang_generic_ahead(lang, host, token);
    SmithToken shown = *token;
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
           smith_lang_ident(smith_lang_peek(lang, host, at + 1, false), "smith") &&
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
    return true;
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
        memcpy(path + used, "../", 3);
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
    used = smith_lang_generic_lines(lang, out, capacity, used);
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
    used = smith_lang_target_header_prelude(lang, out, capacity, used);
    used = smith_lang_append(out, capacity, used, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n");
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

void smith_lang_finish(SmithLang *lang) {
    if (lang->finished) return;
    lang->finished = true;
    if (lang->is_module) smith_lang_assemble_header(lang, lang->main_file);
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

static void smith_lang_ask(SmithLang *lang, const SmithHost *host) {
    smith_lang_ask_imports(host);
    const char *list = getenv("SMITH_LANG_ASK");
    if (list == nullptr || host->type_info == nullptr) return;
    for (const char *at = list; *at != 0;) { // each entry
        const char *end = strchr(at, ',');
        size_t length = end != nullptr ? (size_t)(end - at) : strlen(at);
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

// What `if constexpr` and the constants it names do with a token (runtime/lang_target.c), then the token's own path.
static void smith_lang_process(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    bool closes = false;
    if (!token->foreign && !lang->recording && !smith_lang_capturing(lang)) {
        if (token->kind == SmithTokenKind_Identifier) {
            if (smith_lang_ident(token, "if") && smith_lang_ifc_begin(lang, host, token)) return;
            if (host->maybe_if_constexpr && smith_lang_ident(token, "for")) {
                smith_lang_for_constexpr_outside(lang, host, token);
            }
            if (host->maybe_if_constexpr && smith_lang_ident(token, "constexpr")) smith_lang_ifc_constant(lang, host);
        } else if (lang->ifc_count > 0 && token->kind == SmithTokenKind_Punctuator) {
            lang->ifc_braces += smith_lang_punct(token, "{") ? 1 : 0;
            lang->ifc_braces -= smith_lang_punct(token, "}") ? 1 : 0;
            closes = smith_lang_punct(token, "}") && lang->ifc_braces == lang->ifc[lang->ifc_count - 1].target;
        }
    }
    smith_lang_dispatch(lang, host, token);
    if (closes) smith_lang_ifc_close(lang, host);
}

// One lowering per process at a time: a fresh one for each file.
SmithLang *smith_lang_new(void) {
    SmithLang *lang = &smith_lang_instance;
    memset(lang, 0, sizeof *lang);
    smith_lang_word_buckets();
    lang->started = true;
    lang->pending = SIZE_MAX;
    lang->dump_enabled = getenv("SMITH_LANG_DUMP") != nullptr;
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
        smith_lang_generic_advance(lang, host, &next);
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

// What the keyword at text[start, end) is being used to name, read from the text around it: a function, variable,
// parameter, field or type (null: it is not being used as a name). The statement before it is scanned back to its
// start or to the `{` that encloses it, a bounded distance.
const char *smith_lang_keyword_name_use(const char *text, size_t size, size_t start, size_t end) {
    if (start > size || end > size) return nullptr;
    char next = smith_lang_next_char(text, size, end);
    // A name is declared after a type, a qualifier or a `*`: after `)`, `,`, `(` or an operator the keyword is the
    // keyword (`(u32)sizeof(x)`, `typeof(x) y` after a parameter).
    size_t before = start;
    while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t' || text[before - 1] == '\n'))
        before -= 1;
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
