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

static void smith_lang_header_token(SmithLang *lang, const SmithToken *token) {
    if (!lang->is_module || !lang->export_active || token->kind == SmithTokenKind_Eof) return;
    const char *text = token->header_text != nullptr ? token->header_text : token->text;
    size_t length = token->header_text != nullptr ? token->header_length : token->length;
    smith_lang_put(smith_lang_header_body, smith_lang_header_max, &lang->header_used, text, length, token->line_start,
                   token->space_before);
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
    smith_lang_apply_carry(lang, &token);
    smith_lang_flush_kept(lang, host);
    smith_lang_dump_token(lang, &token);
    smith_lang_header_token(lang, &token);
    host->emit(host->context, &token);
    lang->out_count += 1;
    lang->changed = true;
}

static void smith_lang_emit_code(SmithLang *lang, const SmithHost *host, SmithToken token) {
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

static SmithToken smith_lang_take(SmithLang *lang, const SmithHost *host) {
    smith_lang_peek(lang, host, 0, false);
    SmithToken token = lang->queue[lang->queue_head];
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

// The module's prefix and a name, in lang->tmp.
static const char *smith_lang_prefixed(SmithLang *lang, const char *name, size_t length) {
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
