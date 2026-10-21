// The lowering's output: what goes to the host, to the generated header and to the dump; the lookahead; errors.
// Included by runtime/lang.c.

[[gnu::format(printf, 4, 5)]] static void smith_lang_error(SmithLang *lang, const SmithHost *host, uint32_t location,
                                                           const char *format, ...) {
    char message[smith_lang_message_max];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);
    lang->errors += 1;
    host->error(host->context, location, message);
}

// Writes a token in the layout of the source: a new line where it began one, a space where it had one.
static void smith_lang_put(char *buffer, size_t capacity, size_t *used, const char *text, size_t length,
                           bool line_start, bool space) {
    size_t separator = *used > 0 && (line_start || space) ? 1 : 0;
    if (*used + separator + length + 2 > capacity) return;
    if (separator == 1) buffer[(*used)++] = line_start ? '\n' : ' ';
    memcpy(buffer + *used, text, length);
    *used += length;
}

static void smith_lang_dump_token(SmithLang *lang, const SmithToken *token) {
    if (!lang->dump_enabled || token->kind == SmithTokenKind_Eof) return;
    smith_lang_put(smith_lang_dump, smith_lang_dump_max, &lang->dump_used, token->text, token->length,
                   token->line_start, token->space_before);
}

// runtime/lang_lex.c: the line and the column (from 1) of the source at a byte offset
static size_t smith_lang_line_of(uint32_t offset);
static size_t smith_lang_column_of(uint32_t offset);

// Writes a token of the header body on the line the source has it on, a space after the token before where the source
// had one, and a name where the source has it on its line (spaces before it, as far as the text before allows), so
// that a diagnostic that points into the interface read in memory is at the declaration in the module, line and column.
static void smith_lang_put_aligned(SmithLang *lang, const SmithToken *token, const char *text, size_t length) {
    size_t line = smith_lang_line_of(token->location);
    size_t newlines = line > lang->aligned_line ? line - lang->aligned_line : 0;
    size_t base = newlines > 0 ? 1 : lang->aligned_column;
    size_t gap = newlines == 0 && lang->header_used > 0 && token->space_before ? 1 : 0;
    if (token->name) {
        size_t wanted = smith_lang_column_of(token->location);
        if (wanted > base + gap) gap = wanted - base;
    }
    if (lang->header_used + newlines + gap + length + 2 > smith_lang_header_max) return;
    for (size_t i = 0; i < newlines; i++)
        smith_lang_header_body[lang->header_used++] = '\n';
    for (size_t i = 0; i < gap; i++)
        smith_lang_header_body[lang->header_used++] = ' ';
    memcpy(smith_lang_header_body + lang->header_used, text, length);
    lang->header_used += length;
    lang->aligned_line += newlines;
    lang->aligned_column = base + gap + length;
}

static void smith_lang_trivia_to(SmithLang *lang, uint32_t location);

static void smith_lang_header_token(SmithLang *lang, const SmithToken *token) {
    if (!lang->is_module || !lang->export_active || token->kind == SmithTokenKind_Eof) return;
    smith_lang_trivia_to(lang, token->location); // the comments and directives before it
    const char *text = token->header_text != nullptr ? token->header_text : token->text;
    size_t length = token->header_text != nullptr ? token->header_length : token->length;
    if (lang->aligned) {
        smith_lang_put_aligned(lang, token, text, length);
        return;
    }
    bool fresh_line = lang->header_used > 0 && smith_lang_header_body[lang->header_used - 1] == '\n';
    smith_lang_put(smith_lang_header_body, smith_lang_header_max, &lang->header_used, text, length,
                   token->line_start && !fresh_line, token->space_before && !fresh_line);
}

static void smith_lang_flush_kept(SmithLang *lang, const SmithHost *host) {
    if (!lang->pending_kept_set) return;
    lang->pending_kept_set = false;
    host->emit(host->context, &lang->pending_kept);
}

static void smith_lang_apply_carry(SmithLang *lang, SmithToken *token) {
    if (lang->carry_line_start) token->line_start = true;
    lang->carry_line_start = false;
}

// A token the lowering made or changed: to the parser, the dump and, inside an exported declaration, the header.
static void smith_lang_emit(SmithLang *lang, const SmithHost *host, SmithToken token) {
    token.synthetic = token.synthetic || lang->emit_synthetic;
    token.copy = token.copy || lang->emit_synthetic;
    smith_lang_apply_carry(lang, &token);
    smith_lang_flush_kept(lang, host);
    smith_lang_dump_token(lang, &token);
    smith_lang_header_token(lang, &token);
    host->emit(host->context, &token);
    lang->out_count += 1;
    lang->changed = true;
}

static void smith_lang_emit_code(SmithLang *lang, const SmithHost *host, SmithToken token) {
    token.synthetic = token.synthetic || lang->emit_synthetic;
    token.copy = token.copy || lang->emit_synthetic;
    smith_lang_apply_carry(lang, &token);
    smith_lang_flush_kept(lang, host);
    smith_lang_dump_token(lang, &token);
    host->emit(host->context, &token);
    lang->out_count += 1;
    lang->changed = true;
}

static void smith_lang_emit_header(SmithLang *lang, SmithToken token) {
    bool active = lang->export_active;
    lang->export_active = true;
    smith_lang_header_token(lang, &token);
    lang->export_active = active;
}

// A token that goes through as it came. If it is the only one of the call the host is not even told, since it need
// not replace it.
static void smith_lang_emit_kept(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithToken copy = *token;
    smith_lang_apply_carry(lang, &copy);
    smith_lang_dump_token(lang, &copy);
    smith_lang_header_token(lang, &copy);
    if (!lang->changed && lang->out_count == 0 && lang->batch_in == 1 && lang->queue_count == 0 &&
        !lang->carry_line_start && copy.line_start == token->line_start) {
        lang->pending_kept = copy;
        lang->pending_kept_set = true;
        lang->out_count = 1;
        return;
    }
    smith_lang_flush_kept(lang, host);
    host->emit(host->context, &copy);
    lang->out_count += 1;
    lang->changed = true;
}

static SmithToken smith_lang_synth(SmithTokenKind kind, const char *text, size_t length, const SmithToken *like) {
    SmithToken made = {0};
    made.kind = kind;
    made.line_start = like->line_start;
    made.space_before = like->space_before;
    made.location = like->location;
    made.handle = smith_token_no_handle;
    made.text = text;
    made.length = (uint32_t)length;
    return made;
}

// The `static` the lowering writes before a private declaration: it has no token of its own in the source.
static SmithToken smith_lang_static_keyword(const SmithToken *like) {
    SmithToken keyword = smith_lang_synth(SmithTokenKind_Identifier, "static", 6, like);
    keyword.synthetic = true;
    return keyword;
}

// ---- Lookahead ----------------------------------------------------------------------------------------------------

// The token `index` places after the one being processed, read from the host if it has not been yet. `unexpanded` is
// for the name after `c.`, which must reach the stream as written so that a macro there can be expanded then.
static const SmithToken *smith_lang_peek(SmithLang *lang, const SmithHost *host, size_t index, bool unexpanded) {
    SMITH_ASSERT(index < smith_lang_queue_max);
    while (lang->queue_count <= index) { // bounded by the index
        size_t slot = (lang->queue_head + lang->queue_count) % smith_lang_queue_max;
        host->next(host->context, &lang->queue[slot], unexpanded && lang->queue_count == index);
        lang->queue_count += 1;
    }
    return &lang->queue[(lang->queue_head + index) % smith_lang_queue_max];
}

static void smith_lang_trivia_taken(SmithLang *lang, const SmithHost *host, const SmithToken *token);

static SmithToken smith_lang_take(SmithLang *lang, const SmithHost *host) {
    smith_lang_peek(lang, host, 0, false);
    SmithToken token = lang->queue[lang->queue_head];
    smith_lang_trivia_taken(lang, host, &token);
    lang->queue_head = (lang->queue_head + 1) % smith_lang_queue_max;
    lang->queue_count -= 1;
    lang->batch_in += 1;
    return token;
}

// ---- Text -----------------------------------------------------------------------------------------------------------

static SmithLangText smith_lang_scratch_store(SmithLang *lang, const char *text, size_t length) {
    if (lang->scratch_used + length + 1 > smith_lang_scratch_max) {
        lang->broken = true;
        return (SmithLangText){0, 0};
    }
    memcpy(smith_lang_scratch + lang->scratch_used, text, length);
    smith_lang_scratch[lang->scratch_used + length] = 0;
    SmithLangText stored = {(uint32_t)lang->scratch_used, (uint32_t)length};
    lang->scratch_used += length + 1;
    return stored;
}

static const char *smith_lang_scratch_str(SmithLangText text) {
    return smith_lang_scratch + text.offset;
}

static SmithLangName *smith_lang_name_find(SmithLang *lang, const char *text, size_t length);

// The module's prefix and a name, in lang->tmp; or the C name chosen for it, if it has one.
static const char *smith_lang_prefixed(SmithLang *lang, const char *name, size_t length) {
    if (lang->c_name_count > 0) {
        SmithLangName *known = smith_lang_name_find(lang, name, length);
        if (known != nullptr && known->c_name.length > 0 && known->c_name.length < sizeof lang->tmp) {
            memcpy(lang->tmp, smith_lang_str(known->c_name), known->c_name.length + 1);
            lang->tmp_length = known->c_name.length;
            return lang->tmp;
        }
    }
    size_t prefix = strlen(lang->prefix);
    if (prefix + length + 1 > sizeof lang->tmp) {
        lang->broken = true;
        lang->tmp_length = 0;
        lang->tmp[0] = 0;
        return lang->tmp;
    }
    memcpy(lang->tmp, lang->prefix, prefix);
    memcpy(lang->tmp + prefix, name, length);
    lang->tmp[prefix + length] = 0;
    lang->tmp_length = prefix + length;
    return lang->tmp;
}

// A header the file includes, for the generated header: its `import <h>;` and `#include` lines.
static void smith_lang_add_include(SmithLang *lang, const char *text, size_t length, bool angled) {
    if (lang->c_imports_count >= smith_lang_imports_max) return;
    lang->c_imports[lang->c_imports_count] = smith_lang_store(lang, text, length);
    lang->c_import_angled[lang->c_imports_count++] = angled;
}
