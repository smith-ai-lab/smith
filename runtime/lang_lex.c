// The lowering without a compiler: a plain lexer over one file, for `smith --smith-header` (the module's generated
// C header, written without compiling the module, so that importers need only the headers) and `smith --smith-lower`
// (the lowered text, for reading). It has no macros and no parser: `#include` lines are noted for the header, other
// directives are skipped, and the questions about names are answered with "unknown". Included by runtime/lang.c.

constexpr size_t smith_lang_source_max = (size_t)1 << 23;

static char smith_lang_source[smith_lang_source_max + 1];

typedef struct {
    const char *file;
    size_t length;
    size_t position;
    bool line_start;
    bool space;
    SmithLang *lang;
} SmithLangRaw;

static bool smith_raw_is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}

static bool smith_raw_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool smith_raw_is_name(char c) {
    return smith_raw_is_name_start(c) || smith_raw_is_digit(c);
}

// Skips spaces, comments and line splices; notes whether a new line began.
static void smith_raw_skip(SmithLangRaw *raw) {
    const char *s = smith_lang_source;
    for (size_t guard = 0; raw->position < raw->length && guard < raw->length; guard++) { // each pass moves on
        char c = s[raw->position];
        if (c == '\n') {
            raw->line_start = true;
            raw->space = false;
            raw->position += 1;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            raw->space = true;
            raw->position += 1;
        } else if (c == '\\' && raw->position + 1 < raw->length && s[raw->position + 1] == '\n') {
            raw->position += 2;
        } else if (c == '/' && raw->position + 1 < raw->length && s[raw->position + 1] == '/') {
            while (raw->position < raw->length && s[raw->position] != '\n')
                raw->position += 1;
        } else if (c == '/' && raw->position + 1 < raw->length && s[raw->position + 1] == '*') {
            raw->position += 2;
            while (raw->position + 1 < raw->length && !(s[raw->position] == '*' && s[raw->position + 1] == '/')) {
                if (s[raw->position] == '\n') raw->line_start = true;
                raw->position += 1;
            }
            raw->position = raw->position + 2 < raw->length ? raw->position + 2 : raw->length;
            raw->space = true;
        } else {
            return;
        }
    }
}

// A directive line: `#include` is noted for the header, anything else is skipped.
static void smith_raw_directive(SmithLangRaw *raw) {
    const char *s = smith_lang_source;
    size_t start = raw->position + 1;
    while (start < raw->length && (s[start] == ' ' || s[start] == '\t'))
        start += 1;
    size_t end = start;
    while (end < raw->length && s[end] >= 'a' && s[end] <= 'z')
        end += 1;
    bool include = smith_lang_equals(s + start, end - start, "include");
    size_t line_end = end;
    for (size_t guard = 0; line_end < raw->length && guard < raw->length; guard++) { // to the end of the logical line
        if (s[line_end] == '\n' && s[line_end - 1] != '\\') break;
        line_end += 1;
    }
    if (include) {
        size_t open = end;
        while (open < line_end && (s[open] == ' ' || s[open] == '\t'))
            open += 1;
        char closing = '\0';
        if (s[open] == '<') closing = '>';
        if (s[open] == '"') closing = '"';
        size_t close = open + 1;
        while (closing != 0 && close < line_end && s[close] != closing)
            close += 1;
        if (closing != 0 && close < line_end) {
            smith_lang_add_include(raw->lang, s + open + 1, close - open - 1, closing == '>');
        }
    }
    raw->position = line_end;
}

static size_t smith_raw_quoted(const SmithLangRaw *raw, size_t at, char quote) {
    const char *s = smith_lang_source;
    size_t i = at + 1;
    for (; i < raw->length && s[i] != quote && s[i] != '\n'; i++) { // bounded by the file
        if (s[i] == '\\' && i + 1 < raw->length) i += 1;
    }
    return i < raw->length && s[i] == quote ? i + 1 : i;
}

static size_t smith_raw_number(const SmithLangRaw *raw, size_t at) {
    const char *s = smith_lang_source;
    size_t i = at;
    for (; i < raw->length; i++) { // bounded by the file
        char c = s[i];
        bool sign = (c == '+' || c == '-') && i > at &&
                    (s[i - 1] == 'e' || s[i - 1] == 'E' || s[i - 1] == 'p' || s[i - 1] == 'P');
        if (!(smith_raw_is_name(c) || c == '.' || c == '\'' || sign)) break;
    }
    return i;
}

static size_t smith_raw_punctuator(const SmithLangRaw *raw, size_t at) {
    static const char *const three[] = {"...", "<<=", ">>="};
    static const char *const two[] = {"->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
                                      "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "##", "::"};
    const char *s = smith_lang_source + at;
    size_t left = raw->length - at;
    for (size_t i = 0; left >= 3 && i < sizeof three / sizeof three[0]; i++) { // 3 entries
        if (memcmp(s, three[i], 3) == 0) return 3;
    }
    for (size_t i = 0; left >= 2 && i < sizeof two / sizeof two[0]; i++) { // 21 entries
        if (memcmp(s, two[i], 2) == 0) return 2;
    }
    return 1;
}

static void smith_raw_next(void *context, SmithToken *token, bool unexpanded) {
    (void)unexpanded;
    SmithLangRaw *raw = context;
    const char *s = smith_lang_source;
    for (size_t guard = 0; guard < raw->length + 1; guard++) { // directives are skipped; each one moves on
        smith_raw_skip(raw);
        if (raw->position < raw->length && s[raw->position] == '#' && raw->line_start) {
            smith_raw_directive(raw);
            continue;
        }
        break;
    }
    *token = (SmithToken){0};
    token->handle = smith_token_no_handle;
    token->line_start = raw->line_start;
    token->space_before = raw->space;
    token->location = (uint32_t)raw->position;
    raw->line_start = false;
    raw->space = false;
    if (raw->position >= raw->length) {
        token->kind = SmithTokenKind_Eof;
        token->text = "";
        return;
    }
    size_t at = raw->position;
    char c = s[at];
    size_t end = at + 1;
    if (smith_raw_is_name_start(c)) {
        while (end < raw->length && smith_raw_is_name(s[end]))
            end += 1;
        token->kind = SmithTokenKind_Identifier;
        bool prefix = end < raw->length && (s[end] == '"' || s[end] == '\'') &&
                      (smith_lang_equals(s + at, end - at, "L") || smith_lang_equals(s + at, end - at, "u") ||
                       smith_lang_equals(s + at, end - at, "U") || smith_lang_equals(s + at, end - at, "u8"));
        if (prefix) {
            token->kind = s[end] == '"' ? SmithTokenKind_String : SmithTokenKind_Character;
            end = smith_raw_quoted(raw, end, s[end]);
        }
    } else if (smith_raw_is_digit(c) || (c == '.' && at + 1 < raw->length && smith_raw_is_digit(s[at + 1]))) {
        token->kind = SmithTokenKind_Number;
        end = smith_raw_number(raw, at);
    } else if (c == '"' || c == '\'') {
        token->kind = c == '"' ? SmithTokenKind_String : SmithTokenKind_Character;
        end = smith_raw_quoted(raw, at, c);
    } else {
        token->kind = SmithTokenKind_Punctuator;
        end = at + smith_raw_punctuator(raw, at);
    }
    token->text = s + at;
    token->length = (uint32_t)(end - at);
    raw->position = end;
}

static void smith_raw_emit(void *context, const SmithToken *token) {
    (void)context;
    (void)token;
}

static void smith_raw_include(void *context, const char *header, bool angled) {
    (void)context;
    (void)header;
    (void)angled;
}

static void smith_raw_error(void *context, uint32_t location, const char *message) {
    SmithLangRaw *raw = context;
    unsigned line = 1;
    size_t column = 1;
    for (size_t i = 0; i < location && i < raw->length; i++) { // to the error
        if (smith_lang_source[i] == '\n') {
            line += 1;
            column = 1;
        } else {
            column += 1;
        }
    }
    fprintf(stderr, "%s:%u:%zu: error: %s\n", raw->file, line, column, message);
}

static SmithLookup smith_raw_lookup(void *context, const char *name, uint32_t length, SmithLookupKind space) {
    (void)context;
    (void)name;
    (void)length;
    (void)space;
    return (SmithLookup){0};
}

static bool smith_raw_read_source(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return false;
    size_t got = fread(smith_lang_source, 1, smith_lang_source_max, file);
    fclose(file);
    smith_lang_source[got] = 0;
    *length = got;
    return true;
}

// Feeds a file to the lowering with no compiler. Returns the exit status.
static int smith_lang_run_raw(const char *path, const char *header_path, bool print_lowered) {
    SmithLangRaw raw = {.file = path, .line_start = true};
    if (!smith_raw_read_source(path, &raw.length)) smith_die(smith_exit_missing, "cannot read", path);
    SmithLang *lang = smith_lang_new();
    raw.lang = lang;
    lang->dump_enabled = print_lowered;
    SmithHost host = {
        .context = &raw,
        .next = smith_raw_next,
        .emit = smith_raw_emit,
        .include = smith_raw_include,
        .error = smith_raw_error,
        .lookup = smith_raw_lookup,
        .read_header = nullptr,
        .main_file = path,
    };
    for (size_t guard = 0; guard < raw.length + 2; guard++) { // one pass per token at most
        SmithToken token;
        smith_raw_next(&raw, &token, false);
        if (guard == 0 && header_path != nullptr && !smith_lang_ident(&token, "module")) {
            fprintf(stderr, "%s: not a module (it has no `module` line), so it has no header\n", path);
            return 1;
        }
        smith_lang_feed(lang, &host, &token);
        if (token.kind == SmithTokenKind_Eof) break;
    }
    smith_lang_finish(lang);
    if (print_lowered) {
        fwrite(smith_lang_dump, 1, lang->dump_used, stdout);
        fputc('\n', stdout);
    }
    if (lang->errors > 0) return 1;
    if (header_path != nullptr && lang->is_module) {
        smith_lang_write_file(header_path, smith_lang_header_full, lang->header_full_used);
    } else if (header_path != nullptr) {
        fprintf(stderr, "%s: not a module (it has no `module` line), so it has no header\n", path);
        return 1;
    }
    return 0;
}

// smith --smith-header file.smith [-o header.h] and smith --smith-lower file.smith
static int smith_lang_main(int count, char **args, bool header) {
    const char *file = nullptr;
    const char *output = nullptr;
    for (int i = 0; i < count; i++) { // bounded by the arguments
        if (strcmp(args[i], "-o") == 0 && i + 1 < count) {
            output = args[++i];
        } else if (file == nullptr) {
            file = args[i];
        } else {
            smith_die(smith_exit_usage, "usage: smith --smith-header file.smith [-o header.h]", nullptr);
        }
    }
    if (file == nullptr) smith_die(smith_exit_usage, "usage: smith --smith-header file.smith [-o header.h]", nullptr);
    if (header && output == nullptr) {
        char fallback[PATH_MAX];
        snprintf(fallback, sizeof fallback, "%s.h", file);
        return smith_lang_run_raw(file, fallback, false);
    }
    return smith_lang_run_raw(file, header ? output : nullptr, !header);
}
