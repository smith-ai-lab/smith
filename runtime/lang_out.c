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

static bool smith_lang_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$';
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

// ---- Tokens kept for a later time: a generic's, and the call being read
// ---------------------------------------------------

// A pool of tokens with their text. `smith_lang_gt` is a stack: the tokens of the generics, which stay for the whole
// file, and above them the calls being read, which are popped when they have been written. `smith_lang_it` only grows:
// the types the instances were asked for.
typedef struct {
    SmithLangGenToken *tokens;
    char *text;
    uint32_t count;
    uint32_t text_used;
    uint32_t capacity;
    uint32_t text_capacity;
    uint32_t keep; // smith_lang_it only: what an instance's types hold (a call typed inside another's arguments)
    uint32_t keep_text;
} SmithLangPool;

static SmithLangGenToken smith_lang_gt_tokens[smith_lang_gt_max];
static char smith_lang_gt_chars[smith_lang_gt_text_max];
static SmithLangGenToken smith_lang_it_tokens[smith_lang_it_max];
static char smith_lang_it_chars[smith_lang_it_text_max];
static SmithLangPool smith_lang_gt = {smith_lang_gt_tokens, smith_lang_gt_chars,    0, 0,
                                      smith_lang_gt_max,    smith_lang_gt_text_max, 0, 0};
static SmithLangPool smith_lang_it = {smith_lang_it_tokens, smith_lang_it_chars,    0, 0,
                                      smith_lang_it_max,    smith_lang_it_text_max, 0, 0};

constexpr unsigned smith_lang_gen_flag_line_start = 1U;
constexpr unsigned smith_lang_gen_flag_space = 2U;
constexpr unsigned smith_lang_gen_flag_expand = 4U;
constexpr unsigned smith_lang_gen_flag_name = 8U;
constexpr unsigned smith_lang_gen_flag_value = 16U; // an `auto` parameter's name in the result type's `typeof(...)`

// Adds a token, with its text; the index it got, or UINT32_MAX when the pool is full.
static uint32_t smith_lang_pool_add(SmithLangPool *pool, SmithLangGenToken token, const char *text, size_t length) {
    if (pool->count >= pool->capacity || pool->text_used + length + 1 > pool->text_capacity) return UINT32_MAX;
    memcpy(pool->text + pool->text_used, text, length);
    pool->text[pool->text_used + length] = 0;
    token.text = pool->text_used;
    token.length = (uint32_t)length;
    pool->text_used += (uint32_t)length + 1;
    pool->tokens[pool->count] = token;
    return pool->count++;
}

static const char *smith_lang_pool_text(const SmithLangPool *pool, const SmithLangGenToken *token) {
    return pool->text + token->text;
}

static SmithLangGenToken smith_lang_gt_from(const SmithToken *token) {
    SmithLangGenToken kept = {};
    kept.kind = (uint8_t)token->kind;
    kept.flags =
        (uint8_t)((token->line_start ? smith_lang_gen_flag_line_start : 0) |
                  (token->space_before ? smith_lang_gen_flag_space : 0) |
                  (token->expand ? smith_lang_gen_flag_expand : 0) | (token->name ? smith_lang_gen_flag_name : 0));
    kept.location = token->location;
    return kept;
}

// The token back as the lowering writes it: not from the source (the parser has no token of it).
static SmithToken smith_lang_gt_token(const SmithLangPool *pool, const SmithLangGenToken *kept) {
    SmithToken token = {};
    token.kind = (SmithTokenKind)kept->kind;
    token.handle = smith_token_no_handle;
    token.text = smith_lang_pool_text(pool, kept);
    token.length = kept->length;
    token.line_start = (kept->flags & smith_lang_gen_flag_line_start) != 0;
    token.space_before = (kept->flags & smith_lang_gen_flag_space) != 0;
    token.expand = (kept->flags & smith_lang_gen_flag_expand) != 0;
    token.location = kept->location;
    return token;
}

static bool smith_lang_capturing(const SmithLang *lang) {
    return lang->recording || lang->cap_frames > 0;
}

static void smith_lang_capture_mark(SmithLang *lang, uint32_t index);
static void smith_lang_capture_fold(SmithLang *lang);

// runtime/lang_generic.c: the token is part of an exported generic's record
static void smith_lang_generic_note_capture(SmithLang *lang, const SmithToken *token);

// A token that would go to the parser is kept instead.
static void smith_lang_capture(SmithLang *lang, const SmithToken *token) {
    if (token->via != 0) smith_lang_generic_note_capture(lang, token);
    uint32_t index = smith_lang_pool_add(&smith_lang_gt, smith_lang_gt_from(token), token->text, token->length);
    if (index == UINT32_MAX) {
        lang->broken = true;
        return;
    }
    if (lang->recording && token->kind == SmithTokenKind_Identifier) smith_lang_capture_mark(lang, index);
    if (lang->recording && token->kind == SmithTokenKind_Punctuator && token->length == 1 && token->text[0] == ')') {
        smith_lang_capture_fold(lang);
    }
}

// runtime/lang_lex.c: the next directive that `smith cc --smith-lower` keeps and that comes before `before`
static bool smith_raw_take_directive(uint32_t before, const char **text, size_t *length);

// Writes the directives before `before` (a place in the source) each on a line of its own, as written. Only the plain
// lexer offers any; a compiler's run has preprocessed them.
static void smith_lang_dump_directives(SmithLang *lang, uint32_t before) {
    const char *text = nullptr;
    size_t length = 0;
    for (size_t guard = 0; guard < smith_lang_dump_max && smith_raw_take_directive(before, &text, &length); guard++) {
        if (lang->dump_used + length + 2 > smith_lang_dump_max) return;
        if (lang->dump_used > 0 && smith_lang_dump[lang->dump_used - 1] != '\n') {
            smith_lang_dump[lang->dump_used++] = '\n';
        }
        memcpy(smith_lang_dump + lang->dump_used, text, length);
        lang->dump_used += length;
    }
}

static void smith_lang_dump_token(SmithLang *lang, const SmithToken *token) {
    if (!lang->dump_enabled || token->kind == SmithTokenKind_Eof) return;
    smith_lang_dump_directives(lang, token->location);
    // The tokens a generic's call and instances are made of carry no layout, and two words written side by side are
    // one word to the compiler that reads the lowered text (`extern double`).
    bool joined = lang->dump_used > 0 && token->length > 0 &&
                  smith_lang_word_char(smith_lang_dump[lang->dump_used - 1]) && smith_lang_word_char(token->text[0]);
    smith_lang_put(smith_lang_dump, smith_lang_dump_max, &lang->dump_used, token->text, token->length,
                   token->line_start, token->space_before || joined);
}

// runtime/lang_lex.c: the line and the column (from 1) of the source at a byte offset
static size_t smith_lang_line_of(uint32_t offset);
static size_t smith_lang_column_of(uint32_t offset);
static bool smith_lang_lines_known(void);

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
// runtime/lang_trivia.c: an exported declaration uses a name written `x::name`
static void smith_lang_note_use(SmithLang *lang, uint16_t via, const char *name, size_t length, bool tag);

// Whether the last word of `text` is struct, union or enum: the name that follows it is a tag.
static bool smith_lang_tag_before(const char *text, size_t used) {
    while (used > 0 && (text[used - 1] == ' ' || text[used - 1] == '\n' || text[used - 1] == '\t'))
        used -= 1;
    size_t end = used;
    while (used > 0 &&
           ((text[used - 1] >= 'a' && text[used - 1] <= 'z') || (text[used - 1] >= 'A' && text[used - 1] <= 'Z') ||
            (text[used - 1] >= '0' && text[used - 1] <= '9') || text[used - 1] == '_'))
        used -= 1;
    size_t length = end - used;
    return smith_lang_equals(text + used, length, "struct") || smith_lang_equals(text + used, length, "union") ||
           smith_lang_equals(text + used, length, "enum");
}

// runtime/lang_private.c
static void smith_lang_priv_write(SmithLang *lang, const SmithToken *token);
static void smith_lang_priv_begin(SmithLang *lang);
static void smith_lang_priv_end(SmithLang *lang);
static void smith_lang_priv_abandon(SmithLang *lang);
static void smith_lang_priv_form(SmithLang *lang, uint8_t form, bool linked);
static void smith_lang_priv_name(SmithLang *lang, const char *name, size_t length, const char *cname,
                                 size_t cname_length);
static void smith_lang_priv_resolve(SmithLang *lang);
static size_t smith_lang_priv_section(SmithLang *lang, char *out, size_t capacity, size_t used);
static size_t smith_lang_priv_linked_line(const SmithLang *lang, char *out, size_t capacity, size_t used);
static size_t smith_lang_priv_hidden_line(const SmithLang *lang, char *out, size_t capacity, size_t used);
static void smith_lang_hidden_read(SmithLang *lang, const char *text, size_t length, const char *prefix,
                                   size_t prefix_length);
static bool smith_lang_is_hidden(const SmithLang *lang, const char *lowered, size_t length);
static void smith_lang_linked_read(SmithLang *lang, const SmithHost *host);
static bool smith_lang_is_linked(const SmithLang *lang, const char *name, size_t length);

static void smith_lang_header_token(SmithLang *lang, const SmithToken *token) {
    if (lang->private_active) { // a private declaration the header pass keeps, for the instance section
        smith_lang_priv_write(lang, token);
        return;
    }
    if (!lang->is_module || !lang->export_active || token->kind == SmithTokenKind_Eof) return;
    if (token->via != 0) {
        smith_lang_note_use(lang, token->via, token->text, token->length,
                            smith_lang_tag_before(smith_lang_header_body, lang->header_used));
    }
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
    if (smith_lang_capturing(lang)) {
        smith_lang_capture(lang, &token);
        return;
    }
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
    if (smith_lang_capturing(lang)) {
        smith_lang_capture(lang, &token);
        return;
    }
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
    if (smith_lang_capturing(lang)) {
        smith_lang_capture(lang, &copy);
        return;
    }
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
    SmithToken made = {};
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
