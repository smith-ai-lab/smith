// Named arguments (issue #317). Included by runtime/lang.c, after runtime/lang_tuple.c.
//
// A call's trailing `.name = value` arguments are the designated initializer of the function's last parameter, which is
// a struct (C's zero fills the fields they leave out):
//
//   html::div(&page, .class = "card");   =>   html_div(&page, (DivOptions){.class = "card"});
//
// `.name = value` is invalid C as an argument, so the C rule holds. The lowering reads the call at its callee: it
// looks ahead to the closing parenthesis for an argument that starts `. name =` at the call's own depth, asks the
// compiler the callee's type, takes the spelling of the last parameter from it and writes `(T){` before the first named
// argument and `}` before the `)`. Positional arguments come first; a positional one after a named one, a named one
// that would not be the last parameter and a last parameter that is no struct are errors that say so. A misspelled name
// is the compiler's own error ("field designator ... does not refer to any field").

constexpr size_t smith_lang_named_span_max = smith_lang_queue_max - 16;

typedef struct {
    size_t open;       // the span index of the call's `(`
    size_t close;      // of its `)`
    size_t first;      // of the first named argument's `.`
    size_t positional; // arguments before the first named one
    size_t stray;      // a positional argument after a named one (its first token), else 0
    size_t callee_end; // the tokens of the callee: 0 to callee_end
    bool found_named;
} SmithLangNamed;

// Whether the span holds `. name =` at `at`.
static bool smith_lang_named_starts(const SmithToken *span, size_t at) {
    return smith_lang_punct(&span[at], ".") && span[at + 1].kind == SmithTokenKind_Identifier &&
           smith_lang_punct(&span[at + 2], "=");
}

// Makes span[0 .. to) hold the token and what follows it: one more token is peeked for each. The queue ends early at
// the end of the file or at a token from another file. Never reads past what the call needs: a question to the
// compiler must not meet a statement the parser has not reached.
static size_t smith_lang_named_fill(SmithLang *lang, const SmithHost *host, SmithToken *span, size_t filled,
                                    size_t to) {
    while (filled < to && filled < smith_lang_named_span_max) {
        const SmithToken *next = smith_lang_peek(lang, host, filled - 1, false);
        if (next->kind == SmithTokenKind_Eof || next->foreign || next->length == 0) break;
        span[filled] = *next;
        filled += 1;
    }
    return filled;
}

// The callee's chain (`a::b::f`) and its `(`: false when the tokens are no call of a name.
static bool smith_lang_named_callee(SmithLang *lang, const SmithHost *host, SmithToken *span, size_t *filled,
                                    SmithLangNamed *call) {
    size_t at = 0;
    *filled = smith_lang_named_fill(lang, host, span, 1, 2);
    while (at + 1 < *filled && smith_lang_punct(&span[at + 1], "::")) {
        *filled = smith_lang_named_fill(lang, host, span, *filled, at + 4);
        if (at + 3 > *filled || span[at + 2].kind != SmithTokenKind_Identifier) return false;
        at += 2;
    }
    if (at + 2 > *filled || !smith_lang_punct(&span[at + 1], "(")) return false;
    call->callee_end = at;
    call->open = at + 1;
    return true;
}

// Reads the arguments of the call: where the named ones start, the closing parenthesis. False when the call is not
// closed in what the queue holds.
static bool smith_lang_named_scan(SmithLang *lang, const SmithHost *host, SmithToken *span, size_t filled,
                                  SmithLangNamed *call) {
    int depth = 1;
    size_t start = call->open + 1; // the first token of the argument being read
    size_t at = call->open + 1;
    for (; at < smith_lang_named_span_max; at++) {
        filled = smith_lang_named_fill(lang, host, span, filled, at + 1);
        if (at >= filled) return false;
        const SmithToken *token = &span[at];
        if (smith_lang_punct(token, "(") || smith_lang_punct(token, "[") || smith_lang_punct(token, "{")) depth += 1;
        if (smith_lang_punct(token, ")") || smith_lang_punct(token, "]") || smith_lang_punct(token, "}")) depth -= 1;
        if (depth == 0) break;
        if (depth == 1 && at == start && call->found_named && !smith_lang_punct(token, ".") && call->stray == 0) {
            call->stray = at; // a positional argument after a named one
        }
        if (depth == 1 && at == start + 2 && smith_lang_named_starts(span, start) && !call->found_named) {
            call->found_named = true;
            call->first = start;
        }
        if (depth == 1 && smith_lang_punct(token, ",")) {
            if (!call->found_named) call->positional += 1;
            start = at + 1;
        }
    }
    if (depth != 0 || at >= smith_lang_named_span_max || !smith_lang_punct(&span[at], ")")) return false;
    call->close = at;
    return true;
}

// The spelling of a function type's last parameter (`void (Page *, DivOptions)` has `DivOptions`) and how many
// parameters there are. False for a type that is no prototype.
static bool smith_lang_named_last(const char *type, char *last, size_t capacity, size_t *count) {
    size_t length = strlen(type);
    while (length > 0 && type[length - 1] == ' ')
        length -= 1;
    if (length == 0 || type[length - 1] != ')') return false;
    size_t close = length - 1;
    int depth = 0;
    size_t open = close;
    for (size_t at = close + 1; at > 0; at--) { // back to the `(` of the parameters
        char c = type[at - 1];
        depth += (c == ')') - (c == '(');
        if (depth == 0) {
            open = at - 1;
            break;
        }
    }
    if (open == close) return false;
    size_t start = open + 1;
    size_t params = close > start ? 1 : 0;
    depth = 0;
    for (size_t at = start; at < close; at++) { // the last top-level comma
        char c = type[at];
        depth += (c == '(' || c == '[') - (c == ')' || c == ']');
        if (c == ',' && depth == 0) {
            start = at + 1;
            params += 1;
        }
    }
    while (start < close && type[start] == ' ')
        start += 1;
    if (params == 0 || close - start >= capacity) return false;
    snprintf(last, capacity, "%.*s", (int)(close - start), type + start);
    *count = params;
    return true;
}

// The parameter's spelling as the tokens of a type: one name (`DivOptions`) or `struct Tag`. Anything else (a pointer,
// a qualifier, an array, `...`) is not a struct passed by value.
static bool smith_lang_named_words(const char *last, char words[2][128], size_t *count) {
    *count = 0;
    const char *at = last;
    while (*at != 0) {
        size_t length = strspn(at, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
        if (length == 0 || length >= 128 || *count == 2) return false;
        snprintf(words[*count], 128, "%.*s", (int)length, at);
        *count += 1;
        at += length;
        if (*at == ' ' && at[1] != 0) at += 1;
    }
    bool tagged = *count == 2 && strcmp(words[0], "struct") == 0;
    bool union_tagged = *count == 2 && strcmp(words[0], "union") == 0;
    return *count == 1 || tagged || union_tagged;
}

static void smith_lang_named_put(SmithToken *made, size_t *used, SmithTokenKind kind, const char *text,
                                 const SmithToken *like, bool space) {
    made[(*used)++] = smith_lang_tuple_made(kind, text, strlen(text), like, space);
}

// Rewrites the call: span[1..close] go back to the queue with `(T){` before the first named argument and `}` before
// the `)`.
static bool smith_lang_named_write(SmithLang *lang, const SmithToken *span, const SmithLangNamed *call,
                                   char words[2][128], size_t word_count) {
    static SmithToken made[smith_lang_queue_max];
    size_t used = 0;
    for (size_t at = 1; at <= call->close; at++) {
        if (at == call->first) {
            bool first_arg = call->first == call->open + 1;
            SmithToken open = smith_lang_tuple_made(SmithTokenKind_Punctuator, "(", 1, &span[at], !first_arg);
            made[used++] = open;
            for (size_t word = 0; word < word_count; word++) {
                smith_lang_named_put(made, &used, SmithTokenKind_Identifier, words[word], &span[at], word > 0);
            }
            smith_lang_named_put(made, &used, SmithTokenKind_Punctuator, ")", &span[at], false);
            smith_lang_named_put(made, &used, SmithTokenKind_Punctuator, "{", &span[at], false);
        }
        if (at == call->close) smith_lang_named_put(made, &used, SmithTokenKind_Punctuator, "}", &span[at], false);
        made[used++] = span[at];
    }
    return smith_lang_tuple_replace(lang, call->close, made, used);
}

typedef enum { SmithLangNamedParam_Struct, SmithLangNamedParam_Count, SmithLangNamedParam_None } SmithLangNamedParam;

// Whether the callee is a generic of this module (its parameters are not C's).
static bool smith_lang_named_generic(SmithLang *lang, const SmithToken *token) {
    const SmithLangName *known = lang->is_module ? smith_lang_name_find(lang, token->text, token->length) : nullptr;
    return known != nullptr && known->generic != 0;
}

// The last parameter of the callee, as the compiler reads it; says to the user what is wrong with it. `Count`: it is a
// struct, and the call does not give the arguments before it.
static SmithLangNamedParam smith_lang_named_param(SmithLang *lang, const SmithHost *host, const SmithToken *span,
                                                  const SmithLangNamed *call, char words[2][128], size_t *word_count) {
    char text[smith_lang_name_max];
    smith_lang_tuple_resolve(lang, span, 0, call->callee_end + 1, text, sizeof text);
    SmithExprType found = {};
    char last[256];
    size_t params = 0;
    const char *callee = span[call->callee_end].text;
    int callee_length = (int)span[call->callee_end].length;
    if (smith_lang_named_generic(lang, &span[0])) {
        smith_lang_error(lang, host, span[call->first].location,
                         "`%.*s` is a generic function: it does not take named arguments yet", callee_length, callee);
        return SmithLangNamedParam_None;
    }
    if (host->expr_type == nullptr || !host->expr_type(host->context, text, (uint32_t)strlen(text), &found) ||
        !smith_lang_named_last(found.written, last, sizeof last, &params)) {
        smith_lang_error(lang, host, span[call->first].location,
                         "named arguments (`.name = value`) need a function with a prototype to give them to: "
                         "`%.*s` is not one",
                         callee_length, callee);
        return SmithLangNamedParam_None;
    }
    SmithTypeInfo info = {};
    bool plain = smith_lang_named_words(last, words, word_count);
    SmithLookupKind space = plain && *word_count == 2 ? SmithLookup_Tag : SmithLookup_Type;
    const char *name = words[*word_count > 0 ? *word_count - 1 : 0];
    if (!plain || host->type_info == nullptr ||
        !host->type_info(host->context, name, (uint32_t)strlen(name), space, &info) || info.fields == 0) {
        smith_lang_error(lang, host, span[call->first].location,
                         "named arguments fill the last parameter of `%.*s`, which must be a struct: it is `%s`",
                         callee_length, callee, last);
        return SmithLangNamedParam_None;
    }
    if (call->positional + 1 == params) return SmithLangNamedParam_Struct;
    smith_lang_error(lang, host, span[call->first].location,
                     "named arguments fill the last parameter, which is parameter %zu of `%.*s`; this call gives %zu "
                     "argument%s before them",
                     params, callee_length, callee, call->positional, call->positional == 1 ? "" : "s");
    return SmithLangNamedParam_Count;
}

// After an error that leaves no struct to build: the named arguments go, so that the compiler has nothing to add.
static void smith_lang_named_drop(SmithLang *lang, const SmithToken *span, const SmithLangNamed *call) {
    static SmithToken kept[smith_lang_queue_max];
    size_t used = 0;
    size_t stop = call->first > call->open + 1 ? call->first - 1 : call->first; // the comma before it goes too
    for (size_t at = 1; at < stop; at++)
        kept[used++] = span[at];
    kept[used++] = span[call->close];
    smith_lang_tuple_replace(lang, call->close, kept, used);
}

// A token that may start a call with named arguments. Everything is decided here; the token itself goes on.
static void smith_lang_named_call(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (token->synthetic || smith_lang_word(token->text, token->length) != SmithLangWord_Other) return;
    SmithToken before = {};
    if (smith_lang_tuple_previous(lang, host, 1, &before) &&
        (smith_lang_punct(&before, ".") || smith_lang_punct(&before, "->"))) {
        return; // a member: its type is not asked of the compiler by name
    }
    static SmithToken span[smith_lang_queue_max];
    SmithLangNamed call = {};
    size_t filled = 0;
    span[0] = *token;
    if (!smith_lang_named_callee(lang, host, span, &filled, &call)) return;
    if (!smith_lang_named_scan(lang, host, span, filled, &call) || !call.found_named) return;
    if (call.stray != 0) {
        smith_lang_error(lang, host, span[call.stray].location,
                         "a positional argument after a named one: the named arguments (`.name = value`) come last");
        smith_lang_named_drop(lang, span, &call);
        return;
    }
    char words[2][128];
    size_t word_count = 0;
    SmithLangNamedParam param = smith_lang_named_param(lang, host, span, &call, words, &word_count);
    if (param == SmithLangNamedParam_None) {
        smith_lang_named_drop(lang, span, &call);
    } else if (!smith_lang_named_write(lang, span, &call, words, word_count)) {
        smith_lang_error(lang, host, span[call.first].location, "this call is too long to lower");
    }
}

// Every token of the file passes here: only a name in a file that has a named argument, outside a generic's recording
// and its captured arguments, is read.
static void smith_lang_named_token(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!host->maybe_named || token->kind != SmithTokenKind_Identifier || token->foreign || token->replay ||
        lang->recording || smith_lang_capturing(lang)) {
        return;
    }
    smith_lang_named_call(lang, host, token);
}
