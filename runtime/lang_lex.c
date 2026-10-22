// The lowering without a compiler: a plain lexer over one file, for `smith cc --smith-header` (the module's generated
// C header, written without compiling the module, so that importers need only the headers) and `smith cc --smith-lower`
// (the lowered text, for reading). It has no macros and no parser: `#include` lines are noted for the header, other
// directives are skipped, and the questions about names are answered with "unknown". Included by runtime/lang.c.

constexpr size_t smith_lang_source_max = (size_t)1 << 23;

static char smith_lang_source[smith_lang_source_max + 1];

// The comments and conditional directives of the file, in order (SmithTrivia in runtime/lang.h). A nested run (an
// interface read in memory) starts the list again: the run it interrupts is the compiler's, which offers none.
constexpr size_t smith_raw_trivia_max = (size_t)1 << 17;

static SmithTrivia smith_raw_trivia[smith_raw_trivia_max];

// The directives `smith cc --smith-lower` keeps, in order (runtime/lang_out.c writes each before the first token that
// comes after it). Only that command fills the list: the compiler's run has preprocessed them already.
typedef struct {
    uint32_t location;
    uint32_t length;
    const char *text;
} SmithRawDirective;

constexpr size_t smith_raw_directives_max = (size_t)1 << 14;
constexpr size_t smith_raw_directive_pool_max = (size_t)1 << 16;

static SmithRawDirective smith_raw_directives[smith_raw_directives_max];
static size_t smith_raw_directives_count;
static size_t smith_raw_directives_next;
static char smith_raw_directive_pool[smith_raw_directive_pool_max]; // the `#if B` that an `#elif B` is turned into
static size_t smith_raw_directive_pool_used;

typedef struct {
    const char *file;
    size_t length;
    size_t position;
    bool keep;                // keep the directives (smith cc --smith-lower)
    unsigned live_depth;      // conditionals kept and still open
    bool zero_else;           // the `#else` of an `#if 0` was dropped, so its `#endif` is too
    unsigned zero_else_depth; // live_depth there
    bool line_start;
    bool space;
    bool skipping;       // inside an `#if 0`
    unsigned skip_depth; // conditionals opened inside it
    SmithLang *lang;
    size_t trivia_count;
    SmithTrivia *trivia_last;  // the last entry made
    SmithTrivia *trivia_fresh; // the first entry made since the last token
    bool trivia_reported;
    char *capture; // else of printing the first error, it is kept here as `line:column: message`
    size_t capture_capacity;
    void *names; // the keyword-as-name state (SmithRawNames) every token read passes through, looked-at ones too
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

static void smith_raw_error(void *context, uint32_t location, const char *message);

// Whether a blank line follows `at` before the next thing that is not white space.
static bool smith_raw_blank_after(const SmithLangRaw *raw, size_t at) {
    unsigned newlines = 0;
    for (; at < raw->length; at++) { // bounded by the file
        char c = smith_lang_source[at];
        if (c == '\n') {
            newlines += 1;
        } else if (c != ' ' && c != '\t' && c != '\r') {
            break;
        }
    }
    return newlines >= 2;
}

// Adds an entry to the list: the text from `start` to `end`.
static SmithTrivia *smith_raw_note(SmithLangRaw *raw, SmithTriviaKind kind, size_t start, size_t end) {
    if (raw->trivia_count >= smith_raw_trivia_max) {
        if (!raw->trivia_reported) {
            raw->trivia_reported = true;
            smith_raw_error(raw, (uint32_t)start, "this file has more comments and directives than smith keeps");
            if (raw->lang != nullptr) raw->lang->errors += 1;
        }
        return nullptr;
    }
    while (end > start && (smith_lang_source[end - 1] == '\r' || smith_lang_source[end - 1] == ' ' ||
                           smith_lang_source[end - 1] == '\t')) {
        end -= 1;
    }
    SmithTrivia *entry = &smith_raw_trivia[raw->trivia_count++];
    *entry = (SmithTrivia){.kind = kind,
                           .location = (uint32_t)start,
                           .length = (uint32_t)(end - start),
                           .text = smith_lang_source + start};
    if (raw->trivia_last != nullptr) raw->trivia_last->next = entry;
    raw->trivia_last = entry;
    if (raw->trivia_fresh == nullptr) raw->trivia_fresh = entry;
    return entry;
}

// Skips spaces, comments and line splices; notes whether a new line began, and keeps the comments.
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
            size_t start = raw->position;
            while (raw->position < raw->length && s[raw->position] != '\n')
                raw->position += 1;
            SmithTrivia *entry =
                smith_raw_note(raw, raw->line_start ? SmithTrivia_Leading : SmithTrivia_Trailing, start, raw->position);
            if (entry != nullptr) entry->blank_after = smith_raw_blank_after(raw, raw->position);
        } else if (c == '/' && raw->position + 1 < raw->length && s[raw->position + 1] == '*') {
            size_t start = raw->position;
            bool leading = raw->line_start;
            raw->position += 2;
            while (raw->position + 1 < raw->length && !(s[raw->position] == '*' && s[raw->position + 1] == '/')) {
                if (s[raw->position] == '\n') raw->line_start = true;
                raw->position += 1;
            }
            raw->position = raw->position + 2 < raw->length ? raw->position + 2 : raw->length;
            raw->space = true;
            SmithTrivia *entry =
                smith_raw_note(raw, leading ? SmithTrivia_Leading : SmithTrivia_Trailing, start, raw->position);
            if (entry != nullptr) entry->blank_after = smith_raw_blank_after(raw, raw->position);
        } else {
            return;
        }
    }
}

// `#if 0` hides its text; the other conditionals are not evaluated, so every branch is read.
static void smith_raw_conditional(SmithLangRaw *raw, const char *name, size_t length, const char *rest, size_t size) {
    bool opens = smith_lang_equals(name, length, "if") || smith_lang_equals(name, length, "ifdef") ||
                 smith_lang_equals(name, length, "ifndef");
    if (opens && raw->skipping) {
        raw->skip_depth += 1;
    } else if (opens && smith_lang_equals(name, length, "if")) {
        size_t first = 0;
        while (first < size && (rest[first] == ' ' || rest[first] == '\t'))
            first += 1;
        size_t last = size;
        while (last > first && (rest[last - 1] == ' ' || rest[last - 1] == '\t' || rest[last - 1] == '\r'))
            last -= 1;
        if (last - first == 1 && rest[first] == '0') raw->skipping = true;
    } else if ((smith_lang_equals(name, length, "else") || smith_lang_equals(name, length, "elif")) && raw->skipping &&
               raw->skip_depth == 0) {
        raw->skipping = false;
    } else if (smith_lang_equals(name, length, "endif") && raw->skipping) {
        if (raw->skip_depth == 0) {
            raw->skipping = false;
        } else {
            raw->skip_depth -= 1;
        }
    }
}

// A conditional directive of the module, kept for the header unless it is inside the text of an `#if 0` (the `#else` or
// `#endif` of that `#if 0` itself is kept, to say where the live text begins and ends).
static void smith_raw_note_directive(SmithLangRaw *raw, const char *name, size_t length, bool was_skipping,
                                     unsigned depth_before, size_t line_end) {
    SmithTriviaKind kind = SmithTrivia_Close;
    bool opens = smith_lang_equals(name, length, "if") || smith_lang_equals(name, length, "ifdef") ||
                 smith_lang_equals(name, length, "ifndef");
    bool branches = smith_lang_equals(name, length, "else") || smith_lang_equals(name, length, "elif") ||
                    smith_lang_equals(name, length, "elifdef") || smith_lang_equals(name, length, "elifndef");
    if (opens) {
        kind = SmithTrivia_Open;
    } else if (branches) {
        kind = SmithTrivia_Branch;
    } else if (!smith_lang_equals(name, length, "endif")) {
        return;
    }
    if (was_skipping && (opens || depth_before > 0)) return; // inside the text that is skipped
    SmithTrivia *entry = smith_raw_note(raw, kind, raw->position, line_end);
    if (entry != nullptr && opens) entry->zero = raw->skipping;
}

static void smith_raw_keep_text(SmithLangRaw *raw, size_t start, size_t end, bool as_open) {
    const char *s = smith_lang_source;
    while (end > start && (s[end - 1] == '\r' || s[end - 1] == ' ' || s[end - 1] == '\t'))
        end -= 1;
    const char *text = s + start;
    size_t length = end - start;
    if (as_open) { // "#elif B" is "#if B"
        size_t word = 1;
        while (word < length && (text[word] == ' ' || text[word] == '\t'))
            word += 1;
        size_t rest = length - (word + 2);
        if (smith_raw_directive_pool_used + rest + 2 > smith_raw_directive_pool_max) as_open = false;
        if (as_open) {
            char *out = smith_raw_directive_pool + smith_raw_directive_pool_used;
            out[0] = '#';
            memcpy(out + 1, text + word + 2, rest);
            smith_raw_directive_pool_used += rest + 1;
            text = out;
            length = rest + 1;
        }
    }
    if (smith_raw_directives_count >= smith_raw_directives_max) {
        smith_raw_error(raw, (uint32_t)start, "this file has more directives than smith keeps");
        if (raw->lang != nullptr) raw->lang->errors += 1;
        return;
    }
    smith_raw_directives[smith_raw_directives_count++] =
        (SmithRawDirective){.location = (uint32_t)start, .length = (uint32_t)length, .text = text};
}

// A `#define` is kept as written, so its body must be C: a `::` or a sized type name in it would reach the compiler as
// it is (the compiler's own run lowers the expansion, where it is used).
static void smith_raw_check_define(SmithLangRaw *raw, size_t at, size_t end) {
    const char *s = smith_lang_source;
    for (size_t i = at; i < end; i++) { // bounded by the line
        bool name_start = smith_raw_is_name_start(s[i]) && (i == at || !smith_raw_is_name(s[i - 1]));
        size_t stop = i;
        while (name_start && stop < end && smith_raw_is_name(s[stop]))
            stop += 1;
        bool bad = (s[i] == ':' && i + 1 < end && s[i + 1] == ':') ||
                   (name_start && smith_lang_find_sized(s + i, stop - i) != nullptr);
        if (bad) {
            smith_raw_error(raw, (uint32_t)i,
                            "a macro that uses `::` or a sized type cannot be kept as written; make it a function");
            if (raw->lang != nullptr) raw->lang->errors += 1;
            return;
        }
        if (name_start) i = stop - 1;
    }
}

// Decides whether a directive is kept, and keeps it. A conditional chain that starts with `#if 0` is left out: its text
// is not read, its `#else` branch stands alone, and an `#elif B` after it becomes the `#if B` it stands for.
static void smith_raw_keep_directive(SmithLangRaw *raw, const char *name, size_t length, bool was_skipping,
                                     size_t line_end) {
    if (length == 0) return; // a null directive
    size_t start = raw->position;
    bool opens = smith_lang_equals(name, length, "if") || smith_lang_equals(name, length, "ifdef") ||
                 smith_lang_equals(name, length, "ifndef");
    bool elif = smith_lang_equals(name, length, "elif") || smith_lang_equals(name, length, "elifdef") ||
                smith_lang_equals(name, length, "elifndef");
    bool closes = smith_lang_equals(name, length, "endif");
    if (was_skipping) {
        if (raw->skipping) return;                     // inside the text that is not read
        if (smith_lang_equals(name, length, "else")) { // the `#if 0` ended here
            raw->zero_else = true;
            raw->zero_else_depth = raw->live_depth;
        } else if (elif) {
            raw->live_depth += 1;
            smith_raw_keep_text(raw, start, line_end, true);
        }
        return;
    }
    if (opens && raw->skipping) return; // `#if 0` itself
    if (smith_lang_equals(name, length, "define")) {
        size_t at = (size_t)(name - smith_lang_source) + length;
        while (at < line_end && (smith_lang_source[at] == ' ' || smith_lang_source[at] == '\t'))
            at += 1;
        smith_raw_check_define(raw, at, line_end);
    }
    if (closes && raw->zero_else && raw->live_depth == raw->zero_else_depth) {
        raw->zero_else = false;
        return;
    }
    if (opens) raw->live_depth += 1;
    if (closes && raw->live_depth > 0) raw->live_depth -= 1;
    smith_raw_keep_text(raw, start, line_end, false);
}

// The next kept directive that comes before `before`, for the dump; false when there is none.
static bool smith_raw_take_directive(uint32_t before, const char **text, size_t *length) {
    if (smith_raw_directives_next >= smith_raw_directives_count) return false;
    const SmithRawDirective *entry = &smith_raw_directives[smith_raw_directives_next];
    if (entry->location >= before) return false;
    smith_raw_directives_next += 1;
    *text = entry->text;
    *length = entry->length;
    return true;
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
    bool include = smith_lang_equals(s + start, end - start, "include") && !raw->skipping;
    size_t line_end = end;
    for (size_t guard = 0; line_end < raw->length && guard < raw->length; guard++) { // to the end of the logical line
        if (s[line_end] == '\n' && s[line_end - 1] != '\\') break;
        line_end += 1;
    }
    bool was_skipping = raw->skipping;
    unsigned depth_before = raw->skip_depth;
    smith_raw_conditional(raw, s + start, end - start, s + end, line_end - end);
    smith_raw_note_directive(raw, s + start, end - start, was_skipping, depth_before, line_end);
    if (raw->keep) smith_raw_keep_directive(raw, s + start, end - start, was_skipping, line_end);
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
            SmithTrivia *entry = smith_raw_note(raw, SmithTrivia_Include, open + 1, close);
            if (entry != nullptr) entry->angled = closing == '>';
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
        if (raw->skipping && raw->position < raw->length) { // the text of an #if 0: to the end of the line
            while (raw->position < raw->length && s[raw->position] != '\n')
                raw->position += 1;
            continue;
        }
        break;
    }
    *token = (SmithToken){0};
    token->trivia = raw->trivia_fresh;
    raw->trivia_fresh = nullptr;
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

// An import is an `#include` in the lowered text (`smith cc --smith-lower`): the module's bodies need what it imports,
// and the header, which has only what the exports need, no longer has it for them (scripts/bootstrap.sh).
static void smith_raw_include(void *context, const char *header, bool angled) {
    SmithLangRaw *raw = context;
    SmithLang *lang = raw->lang;
    if (lang == nullptr || !lang->dump_enabled) return;
    smith_lang_dump_directives(lang, (uint32_t)raw->position);
    char line[smith_lang_name_max + 16];
    int length = snprintf(line, sizeof line, angled ? "#include <%s>" : "#include \"%s\"", header);
    if (length <= 0 || lang->dump_used + (size_t)length + 2 > smith_lang_dump_max) return;
    if (lang->dump_used > 0 && smith_lang_dump[lang->dump_used - 1] != '\n') smith_lang_dump[lang->dump_used++] = '\n';
    memcpy(smith_lang_dump + lang->dump_used, line, (size_t)length);
    lang->dump_used += (size_t)length;
    smith_lang_dump[lang->dump_used++] = '\n';
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
    if (raw->capture != nullptr) {
        if (raw->capture[0] == 0) snprintf(raw->capture, raw->capture_capacity, "%u:%zu: %s", line, column, message);
        return;
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

// Writes the header unless the file already holds these bytes, so that an unchanged header keeps its time and nothing
// that depends on it is rebuilt. Returns the exit status.
static int smith_lang_write_header(const char *path, const char *bytes, size_t length) {
    FILE *old = fopen(path, "rb");
    if (old != nullptr) {
        static char current[sizeof smith_lang_header_full];
        size_t got = fread(current, 1, sizeof current, old);
        bool same = got == length && feof(old) && !ferror(old) && memcmp(current, bytes, length) == 0;
        fclose(old);
        if (same) return 0;
    }
    FILE *file = fopen(path, "wb");
    if (file == nullptr || fwrite(bytes, 1, length, file) != length || fclose(file) != 0) {
        fprintf(stderr, "smith: cannot write %s\n", path);
        return 1;
    }
    return 0;
}

// The keyword-as-name check of a file read without a compiler (the compiler's host does the same by itself): a keyword
// that follows a sized type name (and `*` or a qualifier after it) is reported at the name and fed as an identifier
// instead, so that nothing after it is read wrongly.
typedef struct {
    unsigned after_sized; // tokens left to look at: the previous ones were a sized type name, `*` and qualifiers
    SmithToken before;    // the token before
    char renamed[128];
    unsigned errors;
} SmithRawNames;

static void smith_raw_names(SmithLangRaw *raw, SmithRawNames *names, SmithToken *token) {
    bool after_member = smith_lang_punct(&names->before, ".") || smith_lang_punct(&names->before, "->") ||
                        (names->before.kind == SmithTokenKind_Identifier &&
                         smith_lang_word(names->before.text, names->before.length) == SmithLangWord_Tag);
    if (token->kind != SmithTokenKind_Identifier) {
        names->after_sized = smith_lang_punct(token, "*") && names->after_sized > 1 ? names->after_sized - 1 : 0;
        names->before = *token;
        return;
    }
    SmithLangWord word = smith_lang_word(token->text, token->length);
    if (names->after_sized > 0 && smith_lang_misused_keyword(token->text, token->length)) {
        size_t start = (size_t)token->location;
        const char *noun = smith_lang_keyword_name_use(smith_lang_source, raw->length, start, start + token->length);
        if (noun != nullptr && token->length + 2 < sizeof names->renamed) {
            char message[smith_lang_message_max];
            if (smith_lang_keyword_message(message, sizeof message, token->text, token->length, noun) > 0) {
                smith_raw_error(raw, token->location, message);
            }
            names->errors += 1;
            memcpy(names->renamed, token->text, token->length);
            names->renamed[token->length] = '_';
            names->renamed[token->length + 1] = 0;
            // kept for good: tokens read ahead are still held when the next one is renamed
            token->text = smith_lang_str(smith_lang_store(raw->lang, names->renamed, token->length + 1));
            token->length += 1;
        }
    }
    names->after_sized = names->after_sized > 1 && word == SmithLangWord_Qualifier ? names->after_sized - 1 : 0;
    bool type = smith_lang_find_sized(token->text, token->length) != nullptr ||
                (raw->lang != nullptr && smith_lang_type_name(raw->lang, token->text, token->length));
    if (type && !after_member) names->after_sized = 8;
    names->before = *token;
}

// ---- A module's interface, in memory
// ----------------------------------------------------------------------------------------

// The lowering is one set of tables for the whole process, and an importer's lowering is in the middle of its own when
// it asks for another module's interface, so the tables are kept aside and put back.
static SmithLang smith_lang_saved_instance;
static char smith_lang_saved_chars[smith_lang_chars_max];
static char smith_lang_saved_scratch[smith_lang_scratch_max];
static char smith_lang_saved_header_body[smith_lang_header_max];
static char smith_lang_saved_header_full[smith_lang_header_max + 4096];

size_t smith_lang_interface_capacity(void) {
    return sizeof smith_lang_header_full;
}

static bool smith_lang_aside_held; // a state is set aside and not yet restored

// Holds one saved state, not a stack: a second save before the restore would overwrite the first, and the caller's
// tables would be lost without a word, so it stops instead. Read imported modules before setting the run aside
// (smith_raw_prefetch), never inside it.
static void smith_lang_keep_aside(bool save) {
    SMITH_ASSERT(save != smith_lang_aside_held &&
                 "smith_lang_keep_aside: the lowering set its state aside twice (it holds one state, not a stack), "
                 "or restored one never saved; a module's interface was read while another was being read, so read "
                 "imported modules first with smith_raw_prefetch");
    smith_lang_aside_held = save;
    if (save) {
        memcpy(&smith_lang_saved_instance, &smith_lang_instance, sizeof smith_lang_instance);
        memcpy(smith_lang_saved_chars, smith_lang_chars, sizeof smith_lang_chars);
        memcpy(smith_lang_saved_scratch, smith_lang_scratch, sizeof smith_lang_scratch);
        memcpy(smith_lang_saved_header_body, smith_lang_header_body, sizeof smith_lang_header_body);
        memcpy(smith_lang_saved_header_full, smith_lang_header_full, sizeof smith_lang_header_full);
        smith_lang_generics_aside(true, &smith_lang_instance);
    } else {
        memcpy(&smith_lang_instance, &smith_lang_saved_instance, sizeof smith_lang_instance);
        memcpy(smith_lang_chars, smith_lang_saved_chars, sizeof smith_lang_chars);
        memcpy(smith_lang_scratch, smith_lang_saved_scratch, sizeof smith_lang_scratch);
        memcpy(smith_lang_header_body, smith_lang_saved_header_body, sizeof smith_lang_header_body);
        memcpy(smith_lang_header_full, smith_lang_saved_header_full, sizeof smith_lang_header_full);
        smith_lang_generics_aside(false, &smith_lang_instance);
    }
}

// The lines of the source being lowered, by where each starts, for a header that keeps them.
constexpr size_t smith_lang_lines_max = (size_t)1 << 20;
static uint32_t smith_lang_line_starts[smith_lang_lines_max];
static size_t smith_lang_line_count;

static bool smith_lang_lines_known(void) {
    return smith_lang_line_count > 0;
}

static void smith_lang_index_lines(size_t length) {
    smith_lang_line_count = 0;
    smith_lang_line_starts[smith_lang_line_count++] = 0;
    for (size_t i = 0; i < length && smith_lang_line_count < smith_lang_lines_max; i++) { // bounded by the source
        if (smith_lang_source[i] == '\n') smith_lang_line_starts[smith_lang_line_count++] = (uint32_t)i + 1;
    }
}

// The line (from 1) of the source at a byte offset.
static size_t smith_lang_line_of(uint32_t offset) {
    size_t low = 0;
    size_t high = smith_lang_line_count;
    for (size_t step = 0; high - low > 1 && step < 64; step++) { // halves the lines
        size_t middle = (low + high) / 2;
        if (smith_lang_line_starts[middle] <= offset) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low + 1;
}

static size_t smith_lang_column_of(uint32_t offset) {
    return offset - smith_lang_line_starts[smith_lang_line_of(offset) - 1] + 1;
}

// The next token, checked for a keyword used as a name. Every token the lowering reads, ahead or not, comes through
// here once and in order.
static void smith_raw_next_named(void *context, SmithToken *token, bool unexpanded) {
    SmithLangRaw *raw = context;
    smith_raw_next(context, token, unexpanded);
    smith_raw_names(raw, raw->names, token);
}

// The headers of the modules an interface imports are read as `smith cc --smith-header` reads them (below), so that the
// names an imported module chose with [[smith::c_name]] are the ones its importer's interface writes. The cache of
// modules read starts empty for each interface asked of the compiler (a long-lived editor process sees edits), and is
// the run's own when an interface is asked by `--smith-header` or `--smith-lower`.
static bool smith_raw_read_header(void *context, const char *relative, const char **text, uint32_t *length);
static void smith_raw_interface_enter(void);
static void smith_raw_prefetch(const char *file, const char *text, size_t length);
static bool smith_lang_interface_work(const char *file, const char *text, size_t length, SmithInterface *result,
                                      char *error, size_t error_capacity);
static bool smith_raw_cache_only;

// Lowers the module's source with the plain lexer. The tables are the run's when it returns, and smith_lang_keep_aside
// has put the caller's away: the caller reads what it wants and then puts them back.
static SmithLang *smith_lang_run_interface(const char *file, const char *text, size_t length, char *error,
                                           size_t error_capacity, bool aligned, bool *clean) {
    smith_lang_keep_aside(true);
    memcpy(smith_lang_source, text, length);
    smith_lang_source[length] = 0;
    smith_lang_index_lines(length);
    SmithLangRaw raw = {
        .file = file, .length = length, .line_start = true, .capture = error, .capture_capacity = error_capacity};
    SmithLang *lang = smith_lang_new();
    raw.lang = lang;
    lang->dump_enabled = false;
    lang->aligned = aligned;
    lang->aligned_line = 2; // the first line is `#pragma once`
    lang->aligned_column = 1;
    SmithHost host = {
        .context = &raw,
        .next = smith_raw_next_named,
        .emit = smith_raw_emit,
        .include = smith_raw_include,
        .error = smith_raw_error,
        .lookup = smith_raw_lookup,
        .read_header = smith_raw_read_header,
        .main_file = file,
        .maybe_generic = true,
        .maybe_if_constexpr = true,
    };
    SmithRawNames names = {0};
    raw.names = &names;
    smith_raw_cache_only = true;
    for (size_t guard = 0; guard < raw.length + 2; guard++) { // one pass per token at most
        SmithToken token;
        smith_raw_next_named(&raw, &token, false);
        smith_lang_feed(lang, &host, &token);
        if (token.kind == SmithTokenKind_Eof) break;
    }
    smith_raw_cache_only = false;
    lang->finished =
        true; // not smith_lang_finish: what it writes (SMITH_LANG_DUMP, SMITH_LANG_HEADER) is the importer's
    *clean = lang->errors == 0 && names.errors == 0 && lang->is_module;
    return lang;
}

static bool smith_lang_interface_work(const char *file, const char *text, size_t length, SmithInterface *result,
                                      char *error, size_t error_capacity) {
    if (length > smith_lang_source_max || result->text_capacity < sizeof smith_lang_header_full ||
        error_capacity == 0) {
        return false;
    }
    error[0] = 0;
    bool good = false;
    SmithLang *lang = smith_lang_run_interface(file, text, length, error, error_capacity, true, &good);
    if (lang->is_module) smith_lang_assemble_aligned(lang);
    if (good) {
        memcpy(result->text, smith_lang_header_full, lang->header_full_used);
        result->text_used = lang->header_full_used;
        result->includes_used = smith_lang_assemble_includes(lang, result->includes, result->includes_capacity, false);
        result->meta_used = smith_lang_assemble_meta(lang, result->meta, result->meta_capacity);
    } else if (error[0] == 0) {
        snprintf(error, error_capacity, "1:1: not a module (it has no `module` line), so it has no interface");
    }
    smith_lang_keep_aside(false);
    return good;
}

bool smith_lang_interface(const char *file, const char *text, size_t length, SmithInterface *result, char *error,
                          size_t error_capacity) {
    smith_raw_interface_enter();
    smith_raw_prefetch(file, text, length);
    return smith_lang_interface_work(file, text, length, result, error, error_capacity);
}

size_t smith_lang_declarations(const char *file, const char *text, size_t length, char *out, size_t capacity) {
    if (length > smith_lang_source_max || capacity == 0) return 0;
    char error[512] = "";
    bool clean = false;
    smith_raw_interface_enter();
    smith_raw_prefetch(file, text, length);
    SmithLang *lang = smith_lang_run_interface(file, text, length, error, sizeof error, false, &clean);
    size_t used = 0;
    for (size_t i = 0; lang->is_module && i < lang->names_count; i++) { // the exports, in the order declared
        const SmithLangName *name = &lang->names[i];
        if (!name->exported) continue;
        used = smith_lang_append(out, capacity, used, "%s %zu %zu\n", smith_lang_str(name->name),
                                 smith_lang_line_of(name->location), smith_lang_column_of(name->location));
    }
    smith_lang_keep_aside(false);
    return used;
}

// The compile flags the header pass was given beside the file (`smith cc --smith-header f.smith -o f.h -I x -DY`): the
// ones that decide which header a name is found in. Only the question of smith_lang_ask_compiler uses them.
constexpr size_t smith_header_flags_max = 64;
static const char *smith_header_flags[smith_header_flags_max];
static size_t smith_header_flag_count;
static const char *smith_header_source;

// smith.c: asks a compile of the module's imports alone which import declared each C name the exports use.
static bool smith_lang_ask_compiler(const SmithLang *lang, const char **spellings);

// ---- The headers of imported modules, for the lowering without a compiler
//
// `import smith::arena;` is lowered by what the module's header says of its names (`Arena=SmithArena`, the generics),
// so the run reads it as the compiler's does: the header if the include paths given beside the file (`-I dir`) hold it,
// else the module's source, found at the path its name implies under the root of the importing module (the same rule
// as smith cc), and read as its interface (`meta`: the comment lines of its header, which is all an importer reads of
// it). A module found neither way is accepted unread, as before, so its names are lowered by the prefix rule alone.
// The nested read borrows the tables the run is in the middle of (the source, the comments), so they are kept aside.

constexpr size_t smith_raw_modules_max = 8;
constexpr size_t smith_raw_meta_max = (size_t)1 << 17;

static struct {
    char path[smith_lang_name_max];
    char *text;
    size_t length;
} smith_raw_modules[smith_raw_modules_max];
static size_t smith_raw_modules_count;
static bool smith_raw_run_active; // `--smith-header` or `--smith-lower` is lowering: its cache is its own

static void smith_raw_interface_enter(void) {
    if (!smith_raw_run_active) smith_raw_modules_count = 0;
}

static char smith_raw_module_text[smith_raw_modules_max][smith_raw_meta_max];
static char smith_raw_saved_source[smith_lang_source_max + 1];
static SmithTrivia smith_raw_saved_trivia[smith_raw_trivia_max];

// The root the module's path starts at: for module a::b in x/a/b.smith, x; for a file with no module line, its folder.
static void smith_raw_root(const SmithLangRaw *raw, char *root, size_t capacity) {
    const char *module = smith_lang_module_name(raw->lang);
    char tail[smith_lang_name_max] = "";
    if (module != nullptr) {
        size_t used = 0;
        for (size_t i = 0; module[i] != 0 && used + 8 < sizeof tail; i++) { // bounded by the name
            if (module[i] == ':' && module[i + 1] == ':') {
                tail[used++] = '/';
                i += 1;
            } else {
                tail[used++] = module[i];
            }
        }
        memcpy(tail + used, ".smith", 7);
    }
    size_t file_length = strlen(raw->file);
    size_t tail_length = strlen(tail);
    if (module != nullptr && file_length >= tail_length && strcmp(raw->file + file_length - tail_length, tail) == 0 &&
        (file_length == tail_length || raw->file[file_length - tail_length - 1] == '/')) {
        size_t length = file_length > tail_length ? file_length - tail_length - 1 : 0;
        snprintf(root, capacity, "%.*s", (int)length, raw->file);
        if (length == 0) snprintf(root, capacity, ".");
        return;
    }
    const char *slash = strrchr(raw->file, '/');
    snprintf(root, capacity, "%.*s", slash != nullptr ? (int)(slash - raw->file) : 1,
             slash != nullptr ? raw->file : ".");
}

// The root of a module's path, from the file and the text alone (no lowering has read its module line yet).
static void smith_raw_text_root(const char *file, const char *text, size_t length, char *root, size_t capacity) {
    char tail[smith_lang_name_max] = "";
    for (size_t at = 0; at < length;) { // the `module a::b;` line
        size_t end = at;
        while (end < length && text[end] != '\n')
            end += 1;
        if (end - at > 7 && memcmp(text + at, "module ", 7) == 0) {
            size_t used = 0;
            for (size_t i = at + 7; i < end && text[i] != ';' && used + 8 < sizeof tail; i++) { // a::b is a/b
                if (text[i] == ':' && i + 1 < end && text[i + 1] == ':') {
                    tail[used++] = '/';
                    i += 1;
                } else if (text[i] != ' ') {
                    tail[used++] = text[i];
                }
            }
            memcpy(tail + used, ".smith", 7);
            break;
        }
        at = end + 1;
    }
    size_t file_length = strlen(file);
    size_t tail_length = strlen(tail);
    if (tail_length > 0 && file_length >= tail_length && strcmp(file + file_length - tail_length, tail) == 0 &&
        (file_length == tail_length || file[file_length - tail_length - 1] == '/')) {
        size_t folder = file_length > tail_length ? file_length - tail_length - 1 : 0;
        snprintf(root, capacity, "%.*s", (int)folder, file);
        if (folder == 0) snprintf(root, capacity, ".");
        return;
    }
    const char *slash = strrchr(file, '/');
    snprintf(root, capacity, "%.*s", slash != nullptr ? (int)(slash - file) : 1, slash != nullptr ? file : ".");
}

static size_t smith_raw_read_file(const char *path, char *into, size_t capacity) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return SIZE_MAX;
    size_t got = fread(into, 1, capacity - 1, file);
    fclose(file);
    into[got] = 0;
    return got;
}

// The interface of the module whose source is `source`, from the file `path`: the comment lines of its header, into
// `into`. The caller's own lowering is not touched here; smith_lang_interface_work keeps it aside.
static size_t smith_raw_interface_work(const char *path, const char *source, size_t length, char *into,
                                       size_t capacity) {
    static char text[smith_lang_header_max + 4096];
    static char includes[(size_t)1 << 16];
    char error[512];
    SmithInterface result = {text, sizeof text, 0, includes, sizeof includes, 0, into, capacity, 0};
    if (!smith_lang_interface_work(path, source, length, &result, error, sizeof error)) return SIZE_MAX;
    into[result.meta_used] = 0;
    return result.meta_used;
}

// What the imports of a module that is not in the cache need, found by the text alone: the modules it imports, and the
// ones they import, each once, nearest first.
typedef struct {
    char relative[smith_lang_name_max]; // a/b.h
    char path[PATH_MAX];                // the source
} SmithRawWanted;

static void smith_raw_want(const char *file, const char *text, size_t length, SmithRawWanted *wanted, size_t *count) {
    char root[PATH_MAX];
    smith_raw_text_root(file, text, length, root, sizeof root);
    for (size_t at = 0; at < length;) { // each line of the text
        size_t end = at;
        while (end < length && text[end] != '\n')
            end += 1;
        size_t cursor = at;
        while (cursor < end && (text[cursor] == ' ' || text[cursor] == '\t'))
            cursor += 1;
        if (end - cursor > 7 && memcmp(text + cursor, "import ", 7) == 0) {
            cursor += 7;
            while (cursor < end && text[cursor] == ' ')
                cursor += 1;
            size_t name = cursor;
            while (cursor < end && (smith_raw_is_name(text[cursor]) || text[cursor] == ':'))
                cursor += 1;
            char relative[smith_lang_name_max];
            size_t used = 0;
            for (size_t i = name; i < cursor && used + 4 < sizeof relative; i++) { // a::b is a/b.h
                if (text[i] == ':' && i + 1 < cursor && text[i + 1] == ':') {
                    relative[used++] = '/';
                    i += 1;
                } else {
                    relative[used++] = text[i];
                }
            }
            memcpy(relative + used, ".h", 3);
            bool known = cursor == name || strcmp(relative, "target.h") != 0 ? false : true;
            for (size_t i = 0; i < smith_raw_modules_count; i++) // read already
                known = known || strcmp(smith_raw_modules[i].path, relative) == 0;
            for (size_t i = 0; i < *count; i++) // wanted already
                known = known || strcmp(wanted[i].relative, relative) == 0;
            char path[PATH_MAX];
            bool fits = (size_t)snprintf(path, sizeof path, "%s/%.*s.smith", root, (int)(strlen(relative) - 2),
                                         relative) < sizeof path;
            if (!known && cursor > name && fits && *count < smith_raw_modules_max - smith_raw_modules_count) {
                snprintf(wanted[*count].relative, sizeof wanted[0].relative, "%s", relative);
                snprintf(wanted[*count].path, sizeof wanted[0].path, "%s", path);
                *count += 1;
            }
        }
        at = end + 1;
    }
}

// Before an interface is made the modules it imports are read, and the ones they import, the deepest first, so that
// the lowering finds each module's header in the cache and writes the names that module chose: it cannot read one
// itself, because it has borrowed the tables of the lowering that asked for it and a second borrower would lose them.
// A module not found is accepted unread, as in a run.
static void smith_raw_prefetch(const char *file, const char *text, size_t length) {
    static SmithRawWanted wanted[smith_raw_modules_max];
    static char source[smith_lang_source_max + 1];
    size_t count = 0;
    smith_raw_want(file, text, length, wanted, &count);
    for (size_t at = 0; at < count; at++) { // the imports of the modules found, which are found in turn
        size_t got = smith_raw_read_file(wanted[at].path, source, sizeof source);
        if (got != SIZE_MAX) smith_raw_want(wanted[at].path, source, got, wanted, &count);
    }
    for (size_t at = count; at > 0; at--) { // each after what it imports
        if (smith_raw_modules_count >= smith_raw_modules_max) return;
        size_t slot = smith_raw_modules_count++;
        snprintf(smith_raw_modules[slot].path, sizeof smith_raw_modules[0].path, "%s", wanted[at - 1].relative);
        smith_raw_modules[slot].text = smith_raw_module_text[slot];
        smith_raw_modules[slot].length = 0;
        smith_raw_module_text[slot][0] = 0;
        size_t got = smith_raw_read_file(wanted[at - 1].path, source, sizeof source);
        if (got == SIZE_MAX) continue;
        size_t meta =
            smith_raw_interface_work(wanted[at - 1].path, source, got, smith_raw_module_text[slot], smith_raw_meta_max);
        smith_raw_modules[slot].length = meta == SIZE_MAX ? 0 : meta;
        if (meta == SIZE_MAX) smith_raw_module_text[slot][0] = 0;
    }
}

// The interface of the module whose source is at `path`, for a run that is in the middle of its own source and
// comments, which the interface borrows and puts back.
static size_t smith_raw_interface_meta(SmithLangRaw *raw, const char *path, char *into, size_t capacity) {
    static char source[smith_lang_source_max + 1];
    size_t length = smith_raw_read_file(path, source, sizeof source);
    if (length == SIZE_MAX) return SIZE_MAX;
    memcpy(smith_raw_saved_source, smith_lang_source, raw->length + 1);
    memcpy(smith_raw_saved_trivia, smith_raw_trivia, raw->trivia_count * sizeof *smith_raw_trivia);
    smith_raw_prefetch(path, source, length);
    size_t got = smith_raw_interface_work(path, source, length, into, capacity);
    memcpy(smith_lang_source, smith_raw_saved_source, raw->length + 1);
    memcpy(smith_raw_trivia, smith_raw_saved_trivia, raw->trivia_count * sizeof *smith_raw_trivia);
    smith_lang_index_lines(raw->length);
    return got;
}

static bool smith_raw_read_header(void *context, const char *relative, const char **text, uint32_t *length) {
    SmithLangRaw *raw = context;
    size_t slot = SIZE_MAX;
    for (size_t i = 0; i < smith_raw_modules_count; i++) { // the modules read already
        if (strcmp(smith_raw_modules[i].path, relative) == 0) slot = i;
    }
    if (slot == SIZE_MAX && smith_raw_cache_only) { // an interface being made reads what was read before it began
        *text = "";
        *length = 0;
        return true;
    }
    if (slot == SIZE_MAX) {
        if (smith_raw_modules_count >= smith_raw_modules_max) return false;
        slot = smith_raw_modules_count++;
        char *into = smith_raw_module_text[slot];
        snprintf(smith_raw_modules[slot].path, sizeof smith_raw_modules[0].path, "%s", relative);
        smith_raw_modules[slot].text = into;
        smith_raw_modules[slot].length = 0;
        into[0] = 0;
        size_t got = SIZE_MAX;
        for (size_t i = 0; i < smith_header_flag_count && got == SIZE_MAX; i++) { // the header on an include path
            const char *flag = smith_header_flags[i];
            const char *directory = nullptr;
            if (strcmp(flag, "-I") == 0 && i + 1 < smith_header_flag_count) {
                directory = smith_header_flags[++i];
            } else if (strncmp(flag, "-I", 2) == 0) {
                directory = flag + 2;
            }
            char path[PATH_MAX];
            if (directory != nullptr &&
                (size_t)snprintf(path, sizeof path, "%s/%s", directory, relative) < sizeof path) {
                got = smith_raw_read_file(path, into, smith_raw_meta_max);
            }
        }
        if (got == SIZE_MAX) { // else the module's source
            char root[PATH_MAX];
            smith_raw_root(raw, root, sizeof root);
            char path[PATH_MAX];
            size_t stem = strlen(relative) - 2; // without the .h
            if ((size_t)snprintf(path, sizeof path, "%s/%.*s.smith", root, (int)stem, relative) < sizeof path) {
                got = smith_raw_interface_meta(raw, path, into, smith_raw_meta_max);
            }
        }
        smith_raw_modules[slot].length = got == SIZE_MAX ? 0 : got;
        if (got == SIZE_MAX) into[0] = 0;
    }
    *text = smith_raw_modules[slot].text;
    *length = (uint32_t)smith_raw_modules[slot].length;
    return true;
}

// Feeds a file to the lowering with no compiler. Returns the exit status.
static int smith_lang_run_raw(const char *path, const char *header_path, bool print_lowered) {
    SmithLangRaw raw = {.file = path, .line_start = true, .keep = print_lowered};
    smith_raw_run_active = true;
    smith_raw_directives_count = 0;
    smith_raw_directives_next = 0;
    smith_raw_directive_pool_used = 0;
    if (!smith_raw_read_source(path, &raw.length)) smith_die(smith_exit_missing, "cannot read", path);
    smith_lang_index_lines(raw.length);
    SmithLang *lang = smith_lang_new();
    raw.lang = lang;
    lang->dump_enabled = print_lowered;
    SmithHost host = {
        .context = &raw,
        .next = smith_raw_next_named,
        .emit = smith_raw_emit,
        .include = smith_raw_include,
        .error = smith_raw_error,
        .lookup = smith_raw_lookup,
        .type_info = nullptr,
        .field = nullptr,
        .read_header = smith_raw_read_header,
        .main_file = path,
        .maybe_generic = true,
        .maybe_if_constexpr = true,
    };
    SmithRawNames names = {0};
    raw.names = &names;
    smith_header_source = path;
    smith_lang_import_asker = header_path != nullptr ? smith_lang_ask_compiler : nullptr;
    for (size_t guard = 0; guard < raw.length + 2; guard++) { // one pass per token at most
        SmithToken token;
        smith_raw_next_named(&raw, &token, false);
        if (guard == 0 && header_path != nullptr && !smith_lang_ident(&token, "module")) {
            fprintf(stderr, "%s: not a module (it has no `module` line), so it has no header\n", path);
            return 1;
        }
        smith_lang_feed(lang, &host, &token);
        if (token.kind == SmithTokenKind_Eof) break;
    }
    if (print_lowered) smith_lang_dump_directives(lang, UINT32_MAX); // those after the last token
    smith_lang_finish(lang);
    smith_lang_import_asker = nullptr;
    if (print_lowered) {
        smith_lang_target_prelude(lang, stdout);
        fwrite(smith_lang_dump, 1, lang->dump_used, stdout);
        fputc('\n', stdout);
    }
    if (lang->errors > 0 || names.errors > 0) return 1;
    if (header_path != nullptr && lang->is_module) {
        return smith_lang_write_header(header_path, smith_lang_header_full, lang->header_full_used);
    } else if (header_path != nullptr) {
        fprintf(stderr, "%s: not a module (it has no `module` line), so it has no header\n", path);
        return 1;
    }
    return 0;
}

// smith cc --smith-header file.smith [-o header.h] and smith cc --smith-lower file.smith
static int smith_lang_main(int count, char **args, bool header) {
    const char *usage = header ? "usage: smith cc --smith-header file.smith [-o header.h] [compile flags]"
                               : "usage: smith cc --smith-lower file.smith";
    const char *file = nullptr;
    const char *output = nullptr;
    for (int i = 0; i < count; i++) { // bounded by the arguments
        if (strcmp(args[i], "-o") == 0 && i + 1 < count) {
            output = args[++i];
        } else if (header && args[i][0] == '-') { // a compile flag: -I dir, -Dname, -std=...
            static const char *const separate[] = {"-I", "-isystem", "-iquote", "-include", "-isysroot", "-D", "-U"};
            bool pair = false;
            for (size_t j = 0; j < sizeof separate / sizeof separate[0]; j++)
                pair = pair || strcmp(args[i], separate[j]) == 0;
            size_t need = pair && i + 1 < count ? 2 : 1;
            if (smith_header_flag_count + need > smith_header_flags_max) smith_die(smith_exit_usage, usage, nullptr);
            smith_header_flags[smith_header_flag_count++] = args[i];
            if (need == 2) smith_header_flags[smith_header_flag_count++] = args[++i];
        } else if (file == nullptr) {
            file = args[i];
        } else {
            smith_die(smith_exit_usage, usage, nullptr);
        }
    }
    if (file == nullptr) smith_die(smith_exit_usage, usage, nullptr);
    if (header && output == nullptr) {
        char fallback[PATH_MAX];
        snprintf(fallback, sizeof fallback, "%s.h", file);
        return smith_lang_run_raw(file, fallback, false);
    }
    return smith_lang_run_raw(file, header ? output : nullptr, !header);
}
