// What a module's header keeps of the source between tokens: doc comments, the comments inside an export, and the
// conditional directives around exports, copied verbatim (the header is never preprocessed, so the includer's flags
// choose). The plain lexer (runtime/lang_lex.c) offers them as one list (SmithTrivia in runtime/lang.h); a host that
// runs the compiler's preprocessor offers none. Included by runtime/lang.c, after runtime/lang_out.c.
//
// The list is applied in source order. A token is "taken" (smith_lang_take) before it is processed: if no export is
// being written, the entries before it are applied then; if one is, the entries wait until a token is written to the
// header (smith_lang_header_token), because an export's head is held and written later. Either way an entry is applied
// once, in order, by the first call that reaches it.

static void smith_lang_header_room_error(SmithLang *lang) {
    lang->broken = true;
}

static bool smith_lang_header_append(SmithLang *lang, const char *text, size_t length) {
    if (lang->header_used + length + 2 > smith_lang_header_max) {
        smith_lang_header_room_error(lang);
        return false;
    }
    memcpy(smith_lang_header_body + lang->header_used, text, length);
    lang->header_used += length;
    return true;
}

static bool smith_lang_header_at_line_start(const SmithLang *lang) {
    return lang->header_used == 0 || smith_lang_header_body[lang->header_used - 1] == '\n';
}

// A blank line between one export and the next, so that the header reads like one written by hand. (In an interface
// read in memory the lines are the source's own, and nothing is added.)
static void smith_lang_header_separate(SmithLang *lang) {
    if (!lang->is_module || lang->aligned || lang->header_used == 0) return;
    if (!smith_lang_header_at_line_start(lang)) smith_lang_header_append(lang, "\n", 1);
    bool blank = lang->header_used >= 2 && smith_lang_header_body[lang->header_used - 2] == '\n';
    if (!blank) smith_lang_header_append(lang, "\n", 1);
}

// A whole line of the header: a directive, or a comment above a declaration. In an interface read in memory the line is
// the one the source has it on (so that a diagnostic points at the module), and a comment is not kept there.
static void smith_lang_header_line(SmithLang *lang, const char *text, size_t length, uint32_t location) {
    if (lang->aligned) {
        size_t line = smith_lang_line_of(location);
        size_t newlines = line > lang->aligned_line ? line - lang->aligned_line : 0;
        if (newlines == 0 && lang->aligned_column > 1) newlines = 1;
        for (size_t i = 0; i < newlines; i++) {
            if (!smith_lang_header_append(lang, "\n", 1)) return;
        }
        if (!smith_lang_header_append(lang, text, length)) return;
        lang->aligned_line += newlines;
        size_t column = newlines > 0 ? 1 : lang->aligned_column;
        for (size_t i = 0; i < length; i++) { // the text, which may continue on more lines
            if (text[i] == '\n') {
                lang->aligned_line += 1;
                column = 1;
            } else {
                column += 1;
            }
        }
        lang->aligned_column = column;
        return;
    }
    if (!smith_lang_header_at_line_start(lang) && !smith_lang_header_append(lang, "\n", 1)) return;
    if (smith_lang_header_append(lang, text, length)) smith_lang_header_append(lang, "\n", 1);
}

// A comment: above a declaration on a line of its own, or after a token on that token's line.
static void smith_lang_header_comment(SmithLang *lang, const SmithTrivia *entry) {
    if (!lang->is_module || lang->aligned) return;
    if (entry->kind == SmithTrivia_Leading) {
        smith_lang_header_line(lang, entry->text, entry->length, entry->location);
        return;
    }
    if (smith_lang_header_at_line_start(lang)) return; // nothing to follow
    bool line_comment = entry->length >= 2 && entry->text[1] == '/';
    if (smith_lang_header_append(lang, " ", 1) && smith_lang_header_append(lang, entry->text, entry->length) &&
        line_comment) {
        smith_lang_header_append(lang, "\n", 1);
    }
}

// The text of a directive line as written, or, with `as_open`, an #elif (the first live branch after `#if 0`) as the
// #if it stands for.
static void smith_lang_directive_text(const SmithTrivia *entry, bool as_open, char *converted, size_t capacity,
                                      const char **text, size_t *length) {
    *text = entry->text;
    *length = entry->length;
    if (!as_open || *length <= 1 || *length >= capacity - 2) return;
    size_t word = 1;
    while (word < *length && ((*text)[word] == ' ' || (*text)[word] == '\t'))
        word += 1;
    if (word + 2 < *length && (*text)[word] == 'e' && (*text)[word + 1] == 'l') { // "#elif B" is "#if B"
        converted[0] = '#';
        memcpy(converted + 1, *text + word + 2, *length - word - 2);
        *text = converted;
        *length = *length - word - 1;
    }
}

static void smith_lang_header_directive(SmithLang *lang, const SmithTrivia *entry, bool as_open) {
    if (!lang->is_module) return;
    char converted[1024];
    const char *text = nullptr;
    size_t length = 0;
    smith_lang_directive_text(entry, as_open, converted, sizeof converted, &text, &length);
    smith_lang_header_line(lang, text, length, entry->location);
}

// The same, into the header's include lines.
static void smith_lang_include_append(SmithLang *lang, const char *text, size_t length) {
    if (lang->include_used + length + 2 > sizeof lang->include_body) {
        lang->broken = true;
        return;
    }
    memcpy(lang->include_body + lang->include_used, text, length);
    lang->include_used += length;
    lang->include_body[lang->include_used++] = '\n';
}

static void smith_lang_include_directive(SmithLang *lang, const SmithTrivia *entry, bool as_open) {
    char converted[1024];
    const char *text = nullptr;
    size_t length = 0;
    smith_lang_directive_text(entry, as_open, converted, sizeof converted, &text, &length);
    smith_lang_include_append(lang, text, length);
}

static bool smith_lang_directive_is(const SmithTrivia *entry, const char *name) {
    size_t at = 1;
    while (at < entry->length && (entry->text[at] == ' ' || entry->text[at] == '\t'))
        at += 1;
    size_t end = at;
    while (end < entry->length && entry->text[end] >= 'a' && entry->text[end] <= 'z')
        end += 1;
    return smith_lang_equals(entry->text + at, end - at, name);
}

// ---- The conditionals of the module

static void smith_lang_cond_open(SmithLang *lang, const SmithTrivia *entry) {
    if (lang->cond_depth >= smith_lang_cond_depth_max) {
        lang->cond_overflow += 1;
        if (!lang->cond_reported && lang->host_now != nullptr) {
            lang->cond_reported = true;
            smith_lang_error(lang, lang->host_now, entry->location,
                             "conditional directives nest more deeply than smith reads (%zu)",
                             smith_lang_cond_depth_max);
        }
        return;
    }
    SmithLangCond *cond = &lang->conds[lang->cond_depth++];
    *cond = (SmithLangCond){0};
    if (entry->zero) {
        cond->zero = true;
    } else {
        cond->lines[0] = entry;
        cond->count = 1;
    }
}

static void smith_lang_cond_branch(SmithLang *lang, const SmithTrivia *entry) {
    if (lang->cond_overflow > 0 || lang->cond_depth == 0) return;
    SmithLangCond *cond = &lang->conds[lang->cond_depth - 1];
    if (cond->zero) {
        cond->zero = false;
        if (smith_lang_directive_is(entry, "else")) { // the live branch is plain text
            cond->transparent = true;
            return;
        }
        cond->first_open = true; // `#if 0 ... #elif X` is `#if X`
    }
    if (cond->transparent) return;
    if (cond->count >= smith_lang_cond_lines_max) {
        if (lang->host_now != nullptr && !lang->cond_reported) {
            lang->cond_reported = true;
            smith_lang_error(lang, lang->host_now, entry->location,
                             "a conditional has more branches than smith reads (%zu)", smith_lang_cond_lines_max);
        }
        return;
    }
    cond->lines[cond->count] = entry;
    cond->count += 1;
    if (cond->emitted) smith_lang_header_directive(lang, entry, false);
    if (cond->inc_emitted) smith_lang_include_directive(lang, entry, false);
}

static void smith_lang_cond_close(SmithLang *lang, const SmithTrivia *entry) {
    if (lang->cond_overflow > 0) {
        lang->cond_overflow -= 1;
        return;
    }
    if (lang->cond_depth == 0) return;
    SmithLangCond *cond = &lang->conds[--lang->cond_depth];
    if (cond->emitted && !cond->transparent) smith_lang_header_directive(lang, entry, false);
    if (cond->inc_emitted && !cond->transparent) smith_lang_include_directive(lang, entry, false);
}

// The conditionals an export is inside are written, outermost first, with the branches before it.
static void smith_lang_cond_emit(SmithLang *lang) {
    for (uint32_t i = 0; i < lang->cond_depth; i++) { // bounded by the table
        SmithLangCond *cond = &lang->conds[i];
        if (cond->emitted || cond->transparent || cond->zero) continue;
        for (uint8_t line = 0; line < cond->count; line++)
            smith_lang_header_directive(lang, cond->lines[line], line == 0 && cond->first_open);
        cond->emitted = true;
    }
}

// An `import <h>;` or `import "h";`, for the generated header: its line, inside the conditionals the source has it in,
// so that a module can choose between two spellings of a header. Whether the header keeps it is decided at the end
// (smith_lang_resolve_imports): only an import that an exported declaration uses a name of reaches it. An `#include`
// never does.
static void smith_lang_add_include(SmithLang *lang, const char *text, size_t length, bool angled) {
    for (uint32_t i = 0; i < lang->cond_depth; i++) { // bounded by the table
        SmithLangCond *cond = &lang->conds[i];
        if (cond->inc_emitted || cond->transparent || cond->zero) continue;
        for (uint8_t line = 0; line < cond->count; line++)
            smith_lang_include_directive(lang, cond->lines[line], line == 0 && cond->first_open);
        cond->inc_emitted = true;
    }
    char line[smith_lang_name_max + 16];
    int written =
        snprintf(line, sizeof line, "#include %c%.*s%c", angled ? '<' : '"', (int)length, text, angled ? '>' : '"');
    if (written > 0 && (size_t)written < sizeof line) smith_lang_include_append(lang, line, (size_t)written);
    if (lang->cimports_count >= smith_lang_cimports_max) {
        lang->broken = true;
        return;
    }
    lang->cimport_text[lang->cimports_count] = smith_lang_store(lang, text, length);
    lang->cimport_angled[lang->cimports_count] = angled;
    lang->cimports_count += 1;
}

// An exported declaration (an export's head, an exported inline function's body, an exported generic) uses a name
// written through the binding `via` (1 + its index): `a::Name` of an imported module, which the header must include,
// or `c::Name`, which it needs from one of the C headers imported (`struct c::Name` is a tag).
static void smith_lang_note_use(SmithLang *lang, uint16_t via, const char *name, size_t length, bool tag) {
    if (via == 0 || via > lang->bindings_count) return;
    SmithLangBinding *binding = &lang->bindings[via - 1];
    binding->in_header = true;
    if (!binding->is_c) return;
    for (size_t i = 0; i < lang->c_uses_count; i++) { // each name once
        const SmithLangCUse *use = &lang->c_uses[i];
        if (use->tag == tag && use->name.length == length && memcmp(smith_lang_str(use->name), name, length) == 0)
            return;
    }
    if (lang->c_uses_count >= smith_lang_cuses_max) {
        lang->broken = true;
        return;
    }
    lang->c_uses[lang->c_uses_count].name = smith_lang_store(lang, name, length);
    lang->c_uses[lang->c_uses_count].tag = tag;
    lang->c_uses_count += 1;
}

// ---- Applying the list

static void smith_lang_doc_note(SmithLang *lang, const SmithTrivia *entry) {
    if (lang->doc_to != nullptr && !lang->doc_to->blank_after) {
        lang->doc_to = entry; // the block goes on
    } else {
        lang->doc_from = entry;
        lang->doc_to = entry;
    }
}

static void smith_lang_trivia_apply(SmithLang *lang, const SmithTrivia *entry) {
    bool active = lang->export_active;
    if (entry->kind == SmithTrivia_Leading) {
        smith_lang_doc_note(lang, entry);
        if (active) smith_lang_header_comment(lang, entry);
    } else if (entry->kind == SmithTrivia_Trailing) {
        if (active || lang->tail_open) smith_lang_header_comment(lang, entry);
    } else if (entry->kind == SmithTrivia_Include) {
        lang->doc_from = nullptr; // an `#include` is no part of the header: only an `import` is (see above)
        lang->doc_to = nullptr;
    } else {
        lang->doc_from = nullptr;
        lang->doc_to = nullptr;
        if (entry->kind == SmithTrivia_Open) {
            smith_lang_cond_open(lang, entry);
        } else if (entry->kind == SmithTrivia_Branch) {
            smith_lang_cond_branch(lang, entry);
        } else {
            smith_lang_cond_close(lang, entry);
        }
    }
    lang->tail_open = false;
}

// Applies the entries before `location` that no call has applied yet.
static void smith_lang_trivia_to(SmithLang *lang, uint32_t location) {
    const SmithTrivia *entry = lang->trivia_last != nullptr ? lang->trivia_last->next : lang->trivia_first;
    for (size_t guard = 0; entry != nullptr && entry->location < location && guard < ((size_t)1 << 24); guard++) {
        smith_lang_trivia_apply(lang, entry);
        lang->trivia_last = entry;
        entry = entry->next;
    }
}

// A token was taken: the entries before it are applied unless an export is being written (then its tokens do it as
// they reach the header), and the comments just above it are kept in case it begins an export.
static void smith_lang_trivia_taken(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (lang->trivia_first == nullptr && lang->trivia_last == nullptr) {
        if (token->trivia == nullptr) return;
        lang->trivia_first = token->trivia;
    }
    lang->host_now = host;
    lang->doc_taken_from = nullptr;
    lang->doc_taken_to = nullptr;
    if (!lang->export_active) {
        smith_lang_trivia_to(lang, token->location);
        if (lang->doc_to != nullptr && !lang->doc_to->blank_after) {
            lang->doc_taken_from = lang->doc_from;
            lang->doc_taken_to = lang->doc_to;
        }
    }
    lang->doc_from = nullptr;
    lang->doc_to = nullptr;
}

// The token taken last is `export` (or the attribute before it): the comments just above it are its doc comment.
static void smith_lang_doc_capture(SmithLang *lang) {
    if (lang->doc_pending || lang->doc_taken_to == nullptr) return;
    lang->doc_pending = true;
    lang->doc_export_from = lang->doc_taken_from;
    lang->doc_export_to = lang->doc_taken_to;
}

// An export begins: the conditionals around it, then its doc comment.
static void smith_lang_trivia_export(SmithLang *lang) {
    smith_lang_header_separate(lang);
    smith_lang_cond_emit(lang);
    if (lang->doc_pending) {
        const SmithTrivia *entry = lang->doc_export_from;
        for (size_t guard = 0; entry != nullptr && guard < ((size_t)1 << 20); guard++) { // the block
            if (entry->kind == SmithTrivia_Leading) smith_lang_header_comment(lang, entry);
            if (entry == lang->doc_export_to) break;
            entry = entry->next;
        }
    }
    lang->doc_pending = false;
    lang->tail_open = false;
}
