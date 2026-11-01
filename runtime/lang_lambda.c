// Lambdas that capture nothing (issue #955, the first slice of #316). Included by runtime/lang.c, after
// runtime/lang_try.c.
//
// `[](i32 a) -> i32 { return a * 2; }`, `[](i32 a) { return a * 2; }` and `[] { ... }` are an expression: a plain
// function with a name made from the file and the place of the `[`, and the address of it. C has no function inside
// a function, so the lowering lifts the body out:
//
//   in a function   the tokens of the lambda are kept, the expression becomes
//                   `__extension__({ extern R f(P); f; })` and the definition `extern R f(P) { ... }` is written
//                   when the function ends (the next token at brace depth 0), where the compiler reads it as any
//                   function. A name the body uses from the enclosing function is then undeclared: the compiler's
//                   error gets a note that says a lambda that captures converts to no function pointer.
//   at file scope   (a `static` initializer) the definition is written before the declaration that holds the lambda,
//                   found by reading ahead from the declaration's first token, and the expression is the name.
//
// The return type is `-> T`, else the type of the first `return` (read from the compiler: the parameters and the
// statements before it, as a statement expression), else `void`. Every other `return` of the body is checked against
// it with a `_Static_assert`; `-> T` says which. A lambda has no name, so it cannot call itself. The tokens of a
// definition are lowered like any others when they are read again: a lambda inside a lambda is lifted from the
// definition of the first.

constexpr size_t smith_lang_lambda_max = 900;
constexpr size_t smith_lang_lambda_text_max = (size_t)1 << 17;
constexpr size_t smith_lang_lambda_hints_max = 64;
constexpr size_t smith_lang_lambda_per_list = 32; // tokens of a capture list read ahead

static SmithToken smith_lang_lambda_work[smith_lang_lambda_max];      // the tokens of the lambda being read
static SmithToken smith_lang_lambda_defs[smith_lang_lambda_max + 64]; // definitions waiting for a function's end
static SmithToken smith_lang_lambda_out[smith_lang_queue_max];        // what is written in place of the lambda
static char smith_lang_lambda_text[smith_lang_lambda_text_max];       // the text of every token above

typedef struct {
    SmithToken *tokens;
    size_t count;
    size_t capacity;
    bool failed;
} SmithLangLambdaOut;

typedef struct {
    size_t captures_from, captures_to; // the tokens between `[` and `]`
    size_t params_from, params_to;     // the tokens between the parentheses
    bool has_type;
    size_t type_from, type_to; // `-> T`: the tokens of T
    size_t body_from, body_to; // the `{` and the `}`
} SmithLangLambdaParts;

constexpr char smith_lang_lambda_capture_hint[] =
    "this name belongs to the enclosing function: a lambda that uses it would capture it, and a lambda converts to a "
    "function pointer only when it captures nothing (a function pointer can't carry captures)";

// ---- Text and tokens

static const char *smith_lang_lambda_store(SmithLang *lang, const char *text, size_t length) {
    if (lang->lambda.text_used + length + 1 > smith_lang_lambda_text_max) return nullptr;
    char *at = smith_lang_lambda_text + lang->lambda.text_used;
    memcpy(at, text, length);
    at[length] = 0;
    lang->lambda.text_used += length + 1;
    return at;
}

// A token of the source, kept: its text is copied (the host's is valid only until the next token), the host's own
// token stays the one re-emitted.
static bool smith_lang_lambda_keep(SmithLang *lang, SmithToken *token) {
    if (token->length == 0) return true;
    const char *text = smith_lang_lambda_store(lang, token->text, token->length);
    if (text == nullptr) return false;
    token->text = text;
    return true;
}

static void smith_lang_lambda_put(SmithLangLambdaOut *out, SmithToken token) {
    if (out->count == out->capacity) {
        out->failed = true;
        return;
    }
    out->tokens[out->count++] = token;
}

// Space-separated words as tokens made here, standing where `like` did; a word in quotes is one string.
static void smith_lang_lambda_say(SmithLang *lang, SmithLangLambdaOut *out, const SmithToken *like, const char *words) {
    size_t at = 0;
    while (words[at] != 0) {
        while (words[at] == ' ')
            at += 1;
        size_t start = at;
        bool quoted = words[at] == '"';
        at += quoted ? 1 : 0;
        while (words[at] != 0 && (quoted ? words[at] != '"' : words[at] != ' '))
            at += 1;
        at += quoted && words[at] == '"' ? 1 : 0;
        if (at == start) break;
        char first = words[start];
        SmithTokenKind kind = quoted ? SmithTokenKind_String : SmithTokenKind_Punctuator;
        if ((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')
            kind = SmithTokenKind_Identifier;
        const char *text = smith_lang_lambda_store(lang, words + start, at - start);
        if (text == nullptr) {
            out->failed = true;
            return;
        }
        SmithToken made = smith_lang_synth(kind, text, at - start, like);
        made.line_start = false;
        made.space_before = true;
        made.synthetic = true;
        made.copy = false;
        made.replay = false;
        smith_lang_lambda_put(out, made);
    }
}

[[gnu::format(printf, 4, 5)]] static void smith_lang_lambda_sayf(SmithLang *lang, SmithLangLambdaOut *out,
                                                                 const SmithToken *like, const char *format, ...) {
    char words[2048];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(words, sizeof words, format, arguments);
    va_end(arguments);
    smith_lang_lambda_say(lang, out, like, words);
}

static void smith_lang_lambda_copy(SmithLangLambdaOut *out, const SmithToken *from, size_t first, size_t last) {
    for (size_t i = first; i < last; i++)
        smith_lang_lambda_put(out, from[i]);
}

// ---- Reading a lambda's parts

// In kept tokens: the `]` of the capture list that opens at `at` (a `[`), or 0 when the brackets hold anything but
// names separated by commas. `[]`, `[x]` and `[x, y]` are capture lists; `[x + 1]` and `[obj message]` are not.
static size_t smith_lang_lambda_list_end(const SmithToken *t, size_t at, size_t n) {
    bool name = true; // a name, or the `]` of an empty list, comes next
    for (size_t i = at + 1; i < n; i++) {
        if (name && smith_lang_punct(&t[i], "]")) return i == at + 1 ? i : 0;
        if (name) {
            if (t[i].kind != SmithTokenKind_Identifier) return 0;
            name = false;
        } else if (smith_lang_punct(&t[i], "]")) {
            return i;
        } else if (smith_lang_punct(&t[i], ",")) {
            name = true;
        } else {
            return 0;
        }
    }
    return 0;
}

static bool smith_lang_lambda_parts(const SmithToken *t, size_t n, SmithLangLambdaParts *p) {
    size_t close = n > 0 && smith_lang_punct(&t[0], "[") ? smith_lang_lambda_list_end(t, 0, n) : 0;
    if (close == 0) return false;
    p->captures_from = 1;
    p->captures_to = close;
    size_t at = close + 1; // after `[` ... `]`
    p->params_from = at;
    p->params_to = at;
    p->has_type = false;
    if (at < n && smith_lang_punct(&t[at], "(")) {
        int depth = 0;
        size_t open = at;
        for (; at < n; at++) {
            if (smith_lang_punct(&t[at], "(")) depth += 1;
            if (smith_lang_punct(&t[at], ")")) {
                depth -= 1;
                if (depth == 0) break;
            }
        }
        if (at >= n) return false;
        p->params_from = open + 1;
        p->params_to = at;
        at += 1;
    }
    if (at < n && smith_lang_punct(&t[at], "->")) {
        at += 1;
        p->type_from = at;
        while (at < n && !smith_lang_punct(&t[at], "{"))
            at += 1;
        p->type_to = at;
        p->has_type = true;
    }
    if (at + 1 >= n || !smith_lang_punct(&t[at], "{") || !smith_lang_punct(&t[n - 1], "}")) return false;
    p->body_from = at;
    p->body_to = n - 1;
    return true;
}

// Through the host's lookahead: the index of the `]` of the capture list whose first token after the `[` is at `first`,
// or SIZE_MAX when the brackets hold anything but names separated by commas.
static size_t smith_lang_lambda_peek_list_end(SmithLang *lang, const SmithHost *host, size_t first) {
    bool name = true;
    for (size_t i = first; i < first + smith_lang_lambda_per_list; i++) {
        const SmithToken *at = smith_lang_peek(lang, host, i, false);
        if (name && smith_lang_punct(at, "]")) return i == first ? i : SIZE_MAX;
        if (name) {
            if (at->kind != SmithTokenKind_Identifier) return SIZE_MAX;
            name = false;
        } else if (smith_lang_punct(at, "]")) {
            return i;
        } else if (smith_lang_punct(at, ",")) {
            name = true;
        } else {
            return SIZE_MAX;
        }
    }
    return SIZE_MAX;
}

// Whether a token before a `[` leaves an expression to start: not a subscript, not a declarator's brackets.
static bool smith_lang_lambda_expression_before(const SmithToken *before) {
    if (before->kind == SmithTokenKind_Identifier) return smith_lang_ident(before, "return");
    if (before->kind != SmithTokenKind_Punctuator) return false;
    return smith_lang_punct(before, "=") || smith_lang_punct(before, "(") || smith_lang_punct(before, ",") ||
           smith_lang_punct(before, "?") || smith_lang_punct(before, ":") || smith_lang_punct(before, "{") ||
           smith_lang_punct(before, "&&") || smith_lang_punct(before, "||");
}

// In a kept body: the `}` of a lambda that starts at `at` (a `[`), or 0.
static size_t smith_lang_lambda_nested_end(const SmithToken *t, size_t at, size_t n) {
    if (at == 0 || at + 2 >= n || !smith_lang_punct(&t[at], "[")) return 0;
    if (!smith_lang_lambda_expression_before(&t[at - 1])) return 0;
    size_t close = smith_lang_lambda_list_end(t, at, n);
    if (close == 0 || close + 1 >= n) return 0;
    if (!smith_lang_punct(&t[close + 1], "(") && !smith_lang_punct(&t[close + 1], "{") &&
        !smith_lang_punct(&t[close + 1], "->")) {
        return 0;
    }
    int depth = 0;
    bool body = false;
    for (size_t i = close + 1; i < n; i++) {
        if (smith_lang_punct(&t[i], "{")) {
            depth += 1;
            body = true;
        }
        if (smith_lang_punct(&t[i], "}")) {
            depth -= 1;
            if (body && depth == 0) return i;
        }
    }
    return 0;
}

// The `;` that ends the `return` at `at` of a kept body (parentheses and braces balanced), or 0.
static size_t smith_lang_lambda_return_end(const SmithToken *t, size_t at, size_t n) {
    int depth = 0;
    for (size_t i = at + 1; i < n; i++) {
        if (smith_lang_punct(&t[i], "(") || smith_lang_punct(&t[i], "{") || smith_lang_punct(&t[i], "[")) depth += 1;
        if (smith_lang_punct(&t[i], ")") || smith_lang_punct(&t[i], "}") || smith_lang_punct(&t[i], "]")) depth -= 1;
        if (depth == 0 && smith_lang_punct(&t[i], ";")) return i;
        if (depth < 0) return 0;
    }
    return 0;
}

static bool smith_lang_lambda_member(const SmithToken *t, size_t at) {
    return at > 0 && (smith_lang_punct(&t[at - 1], ".") || smith_lang_punct(&t[at - 1], "->"));
}

// The `return`s of the body that belong to the lambda itself (not to a lambda inside it), in order: the index of the
// token after `return`'s statement end is `ends`. Returns how many.
constexpr size_t smith_lang_lambda_returns_max = 64;

static size_t smith_lang_lambda_returns(const SmithToken *t, const SmithLangLambdaParts *p, size_t *at, size_t *depth) {
    size_t count = 0;
    size_t level = 0;
    for (size_t i = p->body_from; i <= p->body_to; i++) {
        size_t nested = smith_lang_lambda_nested_end(t, i, p->body_to);
        if (nested != 0) {
            i = nested;
            continue;
        }
        if (smith_lang_punct(&t[i], "{")) level += 1;
        if (smith_lang_punct(&t[i], "}")) level -= 1;
        if (smith_lang_ident(&t[i], "return") && !smith_lang_lambda_member(t, i) &&
            count < smith_lang_lambda_returns_max) {
            at[count] = i;
            depth[count] = level;
            count += 1;
        }
    }
    return count;
}

// ---- The type of the first return

static bool smith_lang_lambda_append(char *text, size_t capacity, size_t *used, const SmithToken *token) {
    const char *spelling = token->text;
    size_t length = token->length;
    if (token->kind == SmithTokenKind_Identifier) {
        const char *lower = smith_lang_capital_name(spelling, length);
        if (lower != nullptr) {
            spelling = lower;
            length = strlen(lower);
        }
        const SmithLangSized *sized = smith_lang_find_sized(spelling, length);
        if (sized != nullptr) {
            spelling = sized->standard;
            length = strlen(spelling);
        }
    }
    if (*used + length + 2 > capacity) return false;
    memcpy(text + *used, spelling, length);
    *used += length;
    text[(*used)++] = '\x1f';
    return true;
}

// The parameters as the members of a struct (`i32 a ; i32 b ;`), for the probe below.
static void smith_lang_lambda_members(const SmithToken *t, const SmithLangLambdaParts *p, char *members,
                                      size_t capacity) {
    size_t shown = 0;
    bool only_void = p->params_to == p->params_from + 1 && smith_lang_ident(&t[p->params_from], "void");
    int parens = 0;
    for (size_t i = p->params_from; i < p->params_to && !only_void && shown + 2 < capacity; i++) {
        const char *spelling = t[i].text;
        size_t length = t[i].length;
        const char *lower =
            t[i].kind == SmithTokenKind_Identifier ? smith_lang_capital_name(spelling, length) : nullptr;
        if (lower != nullptr) {
            spelling = lower;
            length = strlen(lower);
        }
        parens += smith_lang_punct(&t[i], "(") ? 1 : 0;
        parens -= smith_lang_punct(&t[i], ")") ? 1 : 0;
        bool separator = parens == 0 && smith_lang_punct(&t[i], ",");
        int written = snprintf(members + shown, capacity - shown, "%.*s ", separator ? 1 : (int)length,
                               separator ? ";" : spelling);
        if (written < 0) break;
        shown += (size_t)written;
    }
    if (!only_void && p->params_to > p->params_from && shown + 2 < capacity) members[shown++] = ';';
    members[shown] = 0;
}

// Whether `t[at]` uses a parameter: the name is a declared name of the parameters (not their types').
static bool smith_lang_lambda_uses_parameter(const SmithToken *t, size_t at, const SmithLangLambdaParts *p) {
    if (t[at].kind != SmithTokenKind_Identifier || smith_lang_lambda_member(t, at)) return false;
    for (size_t j = p->params_from + 1; j < p->params_to; j++) {
        if (t[j].kind != SmithTokenKind_Identifier || t[j].length != t[at].length ||
            memcmp(t[j].text, t[at].text, t[at].length) != 0) {
            continue;
        }
        if (j + 1 == p->params_to || smith_lang_punct(&t[j + 1], ")") || smith_lang_punct(&t[j + 1], "[") ||
            smith_lang_punct(&t[j + 1], ",")) {
            return true;
        }
    }
    return false;
}

// The first `return`'s expression as tokens separated by \x1f, each parameter a member of a compound literal; false
// when it does not fit.
static bool smith_lang_lambda_probe(const SmithToken *t, const SmithLangLambdaParts *p, size_t first, size_t end,
                                    char *text, size_t capacity, size_t *used) {
    static char members[2048];
    smith_lang_lambda_members(t, p, members, sizeof members);
    bool only_void = p->params_to == p->params_from + 1 && smith_lang_ident(&t[p->params_from], "void");
    bool good = true;
    for (size_t i = first; good && i < end; i++) {
        if (only_void || !smith_lang_lambda_uses_parameter(t, i, p)) {
            good = smith_lang_lambda_append(text, capacity, used, &t[i]);
            continue;
        }
        char piece[2300];
        snprintf(piece, sizeof piece, "( ( struct { %s } ) { } . %.*s )", members, (int)t[i].length, t[i].text);
        SmithToken whole = t[i];
        whole.text = piece;
        whole.length = (uint32_t)strlen(piece);
        whole.kind = SmithTokenKind_Punctuator;
        good = smith_lang_lambda_append(text, capacity, used, &whole);
    }
    return good && *used > 0;
}

// The type of `return e;` as the compiler reads it with the parameters and the statements before it. 1: `type` is it,
// 0: there is no `return`, -1: it cannot be read (`why`).
static int smith_lang_lambda_infer(const SmithHost *host, const SmithToken *t, size_t n, const SmithLangLambdaParts *p,
                                   char *type, size_t capacity, const char **why) {
    (void)n;
    size_t at[smith_lang_lambda_returns_max];
    size_t depth[smith_lang_lambda_returns_max];
    size_t count = smith_lang_lambda_returns(t, p, at, depth);
    if (count == 0) return 0;
    if (depth[0] != 1) {
        *why = "the first `return` of this lambda is inside a nested block, so its type is not read from it: write the "
               "return type, `[](...) -> T { ... }`";
        return -1;
    }
    size_t end = smith_lang_lambda_return_end(t, at[0], p->body_to);
    if (end == 0 || end == at[0] + 1) {
        return 0; // `return;`
    }
    if (at[0] != p->body_from + 1) {
        *why = "this lambda has statements before its first `return`, so its type is not read from it: write the "
               "return type, `[](...) -> T { ... }`";
        return -1;
    }
    // The compiler reads the expression without the lambda's scope, so each parameter is a member of a compound
    // literal: `a` is `((struct { i32 a; i32 b; }){}.a)`.
    static char text[8192];
    size_t used = 0;
    if (!smith_lang_lambda_probe(t, p, at[0] + 1, end, text, sizeof text, &used)) {
        *why = "the first `return` of this lambda is too long to read its type from: write the return type, "
               "`[](...) -> T { ... }`";
        return -1;
    }
    text[used - 1] = 0;
    SmithExprType found = {};
    found.explain = true;
    if (host->expr_type == nullptr || !host->expr_type(host->context, text, (uint32_t)(used - 1), &found)) {
        static char reason[600];
        snprintf(reason, sizeof reason,
                 "the type of this lambda's first `return` cannot be read (%s): write the return type, `[](...) -> T "
                 "{ ... }`",
                 found.why != nullptr ? found.why : "the compiler gave no reason");
        *why = reason;
        return -1;
    }
    const char *spelled = found.written[0] != 0 ? found.written : found.canonical;
    if (strpbrk(spelled, "([{") != nullptr || snprintf(type, capacity, "%s", spelled) >= (int)capacity) {
        *why = "the type of this lambda's first `return` can't be written before the parameters: write the return "
               "type, `[](...) -> T { ... }`";
        return -1;
    }
    return 1;
}

// ---- Writing the function

static uint32_t smith_lang_lambda_file_hash(const SmithHost *host) {
    uint32_t hash = 2166136261u; // FNV-1a of the path, so that two files' lambdas are never one symbol
    for (const char *at = host->main_file != nullptr ? host->main_file : ""; *at != 0; at++)
        hash = (hash ^ (uint8_t)*at) * 16777619u;
    return hash;
}

// `R f ( P )` with R as written (the tokens of `-> T`) or as spelled.
static void smith_lang_lambda_signature(SmithLang *lang, SmithLangLambdaOut *out, const SmithToken *t,
                                        const SmithLangLambdaParts *p, const char *type, const char *name,
                                        const SmithToken *like) {
    if (p->has_type) {
        smith_lang_lambda_copy(out, t, p->type_from, p->type_to);
    } else {
        smith_lang_lambda_say(lang, out, like, type);
    }
    smith_lang_lambda_sayf(lang, out, like, "%s (", name);
    smith_lang_lambda_copy(out, t, p->params_from, p->params_to);
    smith_lang_lambda_say(lang, out, like, ")");
}

// The body with a check on each `return` after the first, when the return type was inferred.
static void smith_lang_lambda_body(SmithLang *lang, SmithLangLambdaOut *out, const SmithToken *t,
                                   const SmithLangLambdaParts *p, const char *type, bool check) {
    size_t at[smith_lang_lambda_returns_max];
    size_t depth[smith_lang_lambda_returns_max];
    size_t count = smith_lang_lambda_returns(t, p, at, depth);
    size_t next = 1; // the returns[0] is the one the type came from
    for (size_t i = p->body_from; i <= p->body_to; i++) {
        size_t nested = smith_lang_lambda_nested_end(t, i, p->body_to);
        if (nested != 0) {
            smith_lang_lambda_copy(out, t, i, nested + 1);
            i = nested;
            continue;
        }
        if (check && next < count && i == at[next]) {
            size_t end = smith_lang_lambda_return_end(t, i, p->body_to);
            next += 1;
            if (end > i + 1) {
                smith_lang_lambda_say(lang, out, &t[i],
                                      "{ _Static_assert ( __builtin_types_compatible_p ( __typeof__ (");
                smith_lang_lambda_copy(out, t, i + 1, end);
                smith_lang_lambda_sayf(lang, out, &t[i],
                                       ") , %s ) , \"this `return` has another type than the lambda's, %s, which "
                                       "is the type of its first `return`; write the return type: `[](...) -> T`\" "
                                       ") ;",
                                       type, type);
                smith_lang_lambda_copy(out, t, i, end + 1);
                smith_lang_lambda_say(lang, out, &t[end], "}");
                i = end;
                continue;
            }
        }
        smith_lang_lambda_put(out, t[i]);
    }
}

// Whether the token is a type name: a C type word, a sized type, a typedef the parser knows, or the tag after `struct`.
static bool smith_lang_lambda_type_name(const SmithHost *host, const SmithToken *t, size_t at) {
    const SmithToken *token = &t[at];
    if (token->kind != SmithTokenKind_Identifier) return false;
    SmithLangWord word = smith_lang_word(token->text, token->length);
    if (word == SmithLangWord_TypeKeyword || word == SmithLangWord_Qualifier) return true;
    if (word != SmithLangWord_Other) return false;
    if (smith_lang_find_sized(token->text, token->length) != nullptr ||
        smith_lang_capital_name(token->text, token->length) != nullptr) {
        return true;
    }
    if (at > 0 && t[at - 1].kind == SmithTokenKind_Identifier &&
        smith_lang_word(t[at - 1].text, t[at - 1].length) == SmithLangWord_Tag) {
        return true;
    }
    return host->lookup != nullptr &&
           host->lookup(host->context, token->text, token->length, SmithLookup_Type).kind == SmithLookup_Type;
}

// Whether the identifier at `at` declares a name of the lambda itself (a parameter or a local of the body): a type
// name, maybe with stars, comes before it and a declarator's end after.
static bool smith_lang_lambda_declares(const SmithHost *host, const SmithToken *t, size_t at, size_t first,
                                       size_t last) {
    if (at <= first || at + 1 > last || smith_lang_lambda_member(t, at)) return false;
    size_t before = at - 1;
    while (before > first && smith_lang_punct(&t[before], "*"))
        before -= 1;
    if (!smith_lang_lambda_type_name(host, t, before)) return false;
    const SmithToken *after = &t[at + 1];
    return smith_lang_punct(after, "=") || smith_lang_punct(after, ";") || smith_lang_punct(after, ",") ||
           smith_lang_punct(after, "[") || smith_lang_punct(after, ")") || smith_lang_punct(after, ":");
}

// Whether `t[at]` names something the lambda declares anywhere (parameters, locals): any use of that name is its own.
static bool smith_lang_lambda_own_name(const SmithHost *host, const SmithToken *t, size_t at,
                                       const SmithLangLambdaParts *p) {
    for (size_t i = p->params_from; i < p->body_to; i++) {
        if (i == p->params_to) i = p->body_from;
        if (t[i].kind != SmithTokenKind_Identifier || t[i].length != t[at].length ||
            memcmp(t[i].text, t[at].text, t[at].length) != 0) {
            continue;
        }
        if (smith_lang_lambda_declares(host, t, i, p->params_from, p->body_to)) return true;
    }
    return false;
}

// Names the lambda's body uses that the enclosing function may own. The compiler's error for each, once the body is
// read at file scope, gets the note; in a module, whose own names the lowering knows, the first is an error here.
static void smith_lang_lambda_captures(SmithLang *lang, const SmithHost *host, const SmithToken *t,
                                       const SmithLangLambdaParts *p) {
    if (host->lookup == nullptr) return;
    size_t hinted = 0;
    bool reported = false;
    for (size_t i = p->body_from + 1; i < p->body_to && hinted < smith_lang_lambda_hints_max; i++) {
        if (t[i].kind != SmithTokenKind_Identifier || smith_lang_lambda_member(t, i)) continue;
        if (i + 1 < p->body_to && smith_lang_punct(&t[i + 1], "::")) continue;
        if (smith_lang_lambda_own_name(host, t, i, p)) continue;
        SmithLookup found = host->lookup(host->context, t[i].text, t[i].length, SmithLookup_Value);
        if (found.kind != SmithLookup_Value || !found.in_main_file) continue;
        if (host->hint != nullptr && !lang->is_module)
            host->hint(host->context, t[i].location, smith_lang_lambda_capture_hint);
        hinted += 1;
        if (lang->is_module && !reported && smith_lang_name_find(lang, t[i].text, t[i].length) == nullptr) {
            reported = true;
            smith_lang_error(lang, host, t[i].location,
                             "`%.*s` belongs to the enclosing function: a lambda that uses it would capture it, and a "
                             "lambda converts to a function pointer only when it captures nothing (a function "
                             "pointer can't carry captures)",
                             (int)t[i].length, t[i].text);
        }
    }
}

// The capture list `[x, y]` of a lambda that becomes a plain function. Captures exist only in modules: in a file with
// no `module` line the list is read and left to the body's hint (a name of the enclosing function). In a module each
// name must be a local of the enclosing function, and a lambda that captures converts to no function pointer.
static bool smith_lang_lambda_check_list(SmithLang *lang, const SmithHost *host, const SmithToken *t,
                                         const SmithLangLambdaParts *p) {
    if (!lang->is_module || p->captures_from == p->captures_to) return true;
    for (size_t i = p->captures_from; i < p->captures_to; i += 2) {
        SmithLookup found = {};
        if (host->lookup != nullptr) found = host->lookup(host->context, t[i].text, t[i].length, SmithLookup_Value);
        if (found.kind == SmithLookup_Value && found.in_main_file &&
            smith_lang_name_find(lang, t[i].text, t[i].length) == nullptr) {
            continue;
        }
        smith_lang_error(lang, host, t[i].location,
                         "`%.*s` is not a local of the enclosing function: a capture list names locals only (to "
                         "capture a computed value, write a local first)",
                         (int)t[i].length, t[i].text);
        return false;
    }
    smith_lang_error(lang, host, t[p->captures_from].location,
                     "this lambda captures `%.*s`, and a lambda that captures converts to no function pointer (a "
                     "function pointer can't carry captures); pass it where a `Lambda(F)` is expected",
                     (int)t[p->captures_from].length, t[p->captures_from].text);
    return false;
}

// The definition and the name of the lambda in `t[0 .. n)`. `file_scope`: the definition is `static`, written before
// its use; else it has the file's own external name (a block can only declare an external function) and is hidden.
static bool smith_lang_lambda_define(SmithLang *lang, const SmithHost *host, const SmithToken *t, size_t n,
                                     bool file_scope, SmithLangLambdaOut *def, char *name, size_t name_capacity,
                                     SmithLangLambdaParts *parts, char *type, size_t type_capacity) {
    if (!smith_lang_lambda_parts(t, n, parts)) {
        smith_lang_error(lang, host, t[0].location,
                         "a lambda is `[](parameters) -> type { body }`; the parameters and `-> type` are optional");
        return false;
    }
    if (!smith_lang_lambda_check_list(lang, host, t, parts)) return false;
    bool inferred = !parts->has_type;
    type[0] = 0;
    if (inferred) {
        const char *why = "";
        int found = smith_lang_lambda_infer(host, t, n, parts, type, type_capacity, &why);
        if (found < 0) {
            smith_lang_error(lang, host, t[0].location, "%s", why);
            return false;
        }
        if (found == 0) snprintf(type, type_capacity, "void");
    }
    snprintf(name, name_capacity, "smith_lambda_%08x_%u", smith_lang_lambda_file_hash(host), t[0].location);
    smith_lang_lambda_captures(lang, host, t, parts);
    const SmithToken *like = &t[0];
    // a module's private function is `static` by the lowering's own rule
    if (!file_scope)
        smith_lang_lambda_say(lang, def, like, "extern __attribute__ ( ( visibility ( \"hidden\" ) ) )");
    else if (!lang->is_module)
        smith_lang_lambda_say(lang, def, like, "static");
    if (!file_scope) { // a prototype of its own, so that -Wmissing-prototypes finds one
        smith_lang_lambda_signature(lang, def, t, parts, type, name, like);
        smith_lang_lambda_say(lang, def, like, "; extern __attribute__ ( ( visibility ( \"hidden\" ) ) )");
    }
    smith_lang_lambda_signature(lang, def, t, parts, type, name, like);
    smith_lang_lambda_body(lang, def, t, parts, type, inferred && strcmp(type, "void") != 0);
    return !def->failed;
}

// ---- A lambda in a function

static bool smith_lang_lambda_in_function(const SmithHost *host) {
    char type[256];
    return host->return_type != nullptr && host->return_type(host->context, type, sizeof type);
}

// Whether the `[` being lowered starts a lambda in an expression of a function.
static bool smith_lang_lambda_starts(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!smith_lang_punct(token, "[") || lang->lambda.depth == 0 || host->previous == nullptr) return false;
    size_t close = smith_lang_lambda_peek_list_end(lang, host, 0);
    if (close == SIZE_MAX) return false;
    const SmithToken *next = smith_lang_peek(lang, host, close + 1, false);
    if (!smith_lang_punct(next, "(") && !smith_lang_punct(next, "{") && !smith_lang_punct(next, "->")) return false;
    if (!lang->lambda.started || !smith_lang_lambda_expression_before(&lang->lambda.last)) return false;
    return smith_lang_lambda_in_function(host);
}

static void smith_lang_lambda_reset_text(SmithLang *lang) {
    if (lang->lambda.phase == 0 && lang->lambda.pending == 0 && lang->queue_count == 0) lang->lambda.text_used = 0;
}

// The lambda is read: the expression is written in its place, the definition waits for the function's end.
static void smith_lang_lambda_finish(SmithLang *lang, const SmithHost *host) {
    SmithLangLambda *state = &lang->lambda;
    SmithToken *t = smith_lang_lambda_work;
    size_t n = state->count;
    state->phase = 0;
    state->count = 0;
    SmithLangLambdaOut def = {smith_lang_lambda_defs + state->pending, 0,
                              sizeof smith_lang_lambda_defs / sizeof smith_lang_lambda_defs[0] - state->pending, false};
    SmithLangLambdaParts parts = {};
    char name[64];
    char type[256];
    if (!smith_lang_lambda_define(lang, host, t, n, false, &def, name, sizeof name, &parts, type, sizeof type)) {
        if (def.failed) {
            smith_lang_error(lang, host, t[0].location,
                             "this lambda is too long for the lowering: split it into functions");
        }
        return;
    }
    state->pending += (uint32_t)def.count;
    SmithLangLambdaOut site = {smith_lang_lambda_out, 0, smith_lang_queue_max, false};
    const SmithToken *like = &t[0];
    smith_lang_lambda_say(lang, &site, like, "__extension__ ( {");
    smith_lang_lambda_say(lang, &site, like, "extern");
    smith_lang_lambda_signature(lang, &site, t, &parts, type, name, like);
    smith_lang_lambda_sayf(lang, &site, like, "; %s ; } )", name);
    site.tokens[0].line_start = t[0].line_start;
    if (site.failed || !smith_lang_tuple_replace(lang, 0, site.tokens, site.count)) {
        smith_lang_error(lang, host, t[0].location,
                         "this lambda is too long for the lowering: split it into functions");
    }
}

// A token while a lambda is being read: kept, until the body's `}`.
static bool smith_lang_lambda_collect(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangLambda *state = &lang->lambda;
    if (token->kind == SmithTokenKind_Eof || state->count == smith_lang_lambda_max) {
        smith_lang_error(lang, host, state->start,
                         token->kind == SmithTokenKind_Eof ? "this lambda is never closed"
                                                           : "this lambda is too long for the lowering: split it into "
                                                             "functions");
        state->phase = 0;
        state->count = 0;
        return token->kind != SmithTokenKind_Eof;
    }
    SmithToken kept = *token;
    kept.handle = token->handle;
    if (!smith_lang_lambda_keep(lang, &kept)) {
        smith_lang_error(lang, host, state->start, "this lambda is too long for the lowering: split it into functions");
        state->phase = 0;
        state->count = 0;
        return true;
    }
    smith_lang_lambda_work[state->count++] = kept;
    if (smith_lang_punct(token, "(")) state->parens += 1;
    if (smith_lang_punct(token, ")")) state->parens -= 1;
    if (state->parens == 0 && smith_lang_punct(token, "{")) {
        state->braces += 1;
        state->body = true;
    }
    if (state->parens == 0 && smith_lang_punct(token, "}")) {
        state->braces -= 1;
        if (state->body && state->braces == 0) smith_lang_lambda_finish(lang, host);
    }
    if (!state->body && smith_lang_punct(token, ";")) {
        smith_lang_error(lang, host, state->start, "a lambda is `[](parameters) -> type { body }`");
        state->phase = 0;
        state->count = 0;
    }
    return true;
}

// The definitions of the lambdas of the function that just ended, in front of the token after it.
static void smith_lang_lambda_flush(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangLambda *state = &lang->lambda;
    size_t pending = state->pending;
    state->pending = 0;
    static SmithToken made[smith_lang_queue_max];
    if (pending + 1 + lang->queue_count > smith_lang_queue_max) {
        smith_lang_error(lang, host, smith_lang_lambda_defs[0].location,
                         "the lambdas of this function are too long for the lowering: split it into functions");
        return;
    }
    memcpy(made, smith_lang_lambda_defs, pending * sizeof made[0]);
    made[pending] = *token;
    smith_lang_tuple_replace(lang, 0, made, pending + 1);
}

// ---- A lambda in a declaration at file scope

constexpr size_t smith_lang_lambda_per_declaration = 8;

// The queue index of the `}` that ends the lambda whose `[` is at `i`, or 0 when it is none (`[` `]` and `(`, `{` or
// `->`, then a body).
static size_t smith_lang_lambda_queue_end(SmithLang *lang, const SmithHost *host, size_t i, size_t limit) {
    size_t close = smith_lang_lambda_peek_list_end(lang, host, i + 1);
    if (close == SIZE_MAX) return 0;
    const SmithToken *next = smith_lang_peek(lang, host, close + 1, false);
    if (!smith_lang_punct(next, "(") && !smith_lang_punct(next, "{") && !smith_lang_punct(next, "->")) return 0;
    int braces = 0;
    bool body = false;
    for (size_t j = close + 1; j < limit; j++) {
        const SmithToken *inner = smith_lang_peek(lang, host, j, false);
        if (inner->kind == SmithTokenKind_Eof) return 0;
        braces += smith_lang_punct(inner, "{") ? 1 : 0;
        body = body || braces > 0;
        braces -= smith_lang_punct(inner, "}") ? 1 : 0;
        if (body && braces == 0) return j;
    }
    return 0;
}

// The lambdas of the declaration that starts at the queue's head, read ahead to its `;`: where each starts and ends.
static size_t smith_lang_lambda_scan(SmithLang *lang, const SmithHost *host, const SmithToken *token, size_t *starts,
                                     size_t *ends) {
    size_t found = 0;
    int depth = 0;
    bool assigned = false;
    const SmithToken *before = token;
    size_t limit = smith_lang_queue_max - 8;
    for (size_t i = 0; i < limit; i++) {
        const SmithToken *at = smith_lang_peek(lang, host, i, false);
        if (at->kind == SmithTokenKind_Eof || at->foreign) break;
        if (depth == 0 && smith_lang_punct(at, ";")) break;
        if (depth == 0 && smith_lang_punct(at, "=")) assigned = true;
        if (depth == 0 && smith_lang_punct(at, "{") && !assigned) break; // a function's body or a type's members
        size_t end = 0;
        if (smith_lang_punct(at, "[") && found < smith_lang_lambda_per_declaration &&
            smith_lang_lambda_expression_before(before)) {
            end = smith_lang_lambda_queue_end(lang, host, i, limit);
        }
        if (end != 0) {
            starts[found] = i;
            ends[found] = end;
            found += 1;
            before = smith_lang_peek(lang, host, end, false);
            i = end;
            continue;
        }
        if (smith_lang_punct(at, "(") || smith_lang_punct(at, "{") || smith_lang_punct(at, "[")) depth += 1;
        if (smith_lang_punct(at, ")") || smith_lang_punct(at, "}") || smith_lang_punct(at, "]")) depth -= 1;
        before = at;
    }
    return found;
}

// The definition of the lambda at queue indexes [first, last] written to `out`, and its name.
static bool smith_lang_lambda_define_queued(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                            size_t first, size_t last, SmithLangLambdaOut *out, char *name,
                                            size_t name_capacity) {
    size_t n = last - first + 1;
    bool fits = n <= smith_lang_lambda_max;
    for (size_t j = 0; fits && j < n; j++) {
        SmithToken copy = *smith_lang_peek(lang, host, first + j, false);
        fits = smith_lang_lambda_keep(lang, &copy);
        smith_lang_lambda_work[j] = copy;
    }
    if (!fits) {
        smith_lang_error(lang, host, token->location, "this lambda is too long for the lowering: split it");
        return false;
    }
    SmithLangLambdaParts parts = {};
    char type[256];
    return smith_lang_lambda_define(lang, host, smith_lang_lambda_work, n, true, out, name, name_capacity, &parts, type,
                                    sizeof type);
}

// The lambdas of the declaration that starts with `token`: each one's definition goes in front of it, and its place
// becomes the name. True when something was written.
static bool smith_lang_lambda_declaration(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                          bool at_start) {
    if (token->foreign || token->kind != SmithTokenKind_Identifier || host->previous == nullptr ||
        smith_lang_ident(token, "module") || smith_lang_ident(token, "import") || smith_lang_ident(token, "export") ||
        !at_start || smith_lang_lambda_in_function(host)) {
        return false;
    }
    size_t starts[smith_lang_lambda_per_declaration];
    size_t ends[smith_lang_lambda_per_declaration];
    size_t found = smith_lang_lambda_scan(lang, host, token, starts, ends);
    if (found == 0) return false;
    SmithLangLambdaOut out = {smith_lang_lambda_out, 0, smith_lang_queue_max, false};
    char names[smith_lang_lambda_per_declaration][64];
    for (size_t k = 0; k < found; k++) { // the definitions first
        if (!smith_lang_lambda_define_queued(lang, host, token, starts[k], ends[k], &out, names[k], sizeof names[k])) {
            return false;
        }
    }
    SmithToken head = *token;
    smith_lang_lambda_keep(lang, &head);
    smith_lang_lambda_say(lang, &out, token,
                          "_Static_assert ( 1 , \"\" ) ;"); // ends the definitions: a declaration of its own
    smith_lang_lambda_put(&out, head);
    size_t done = 0;
    for (size_t k = 0; k < found; k++) { // the declaration, each lambda replaced by its name
        for (size_t j = done; j < starts[k]; j++) {
            SmithToken copy = *smith_lang_peek(lang, host, j, false);
            smith_lang_lambda_keep(lang, &copy);
            smith_lang_lambda_put(&out, copy);
        }
        smith_lang_lambda_say(lang, &out, smith_lang_peek(lang, host, starts[k], false), names[k]);
        done = ends[k] + 1;
    }
    if (out.failed || !smith_lang_tuple_replace(lang, done, out.tokens, out.count)) {
        smith_lang_error(lang, host, token->location, "this declaration with lambdas is too long for the lowering");
        return false;
    }
    return true;
}

// The raw lowering (the header pass, `--smith-lower`) has no parser and writes no function body to the compiler: the
// `[` ... `]` of a lambda in a body are dropped, so that they are not read as an array's empty brackets.
static bool smith_lang_lambda_raw(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangLambda *state = &lang->lambda;
    if (!smith_lang_punct(token, "[") || !state->started || !smith_lang_lambda_expression_before(&state->last)) {
        return false;
    }
    size_t close = smith_lang_lambda_peek_list_end(lang, host, 0);
    if (close == SIZE_MAX) return false;
    const SmithToken *next = smith_lang_peek(lang, host, close + 1, false);
    if (!smith_lang_punct(next, "(") && !smith_lang_punct(next, "{") && !smith_lang_punct(next, "->")) return false;
    // the capture list and its `]` go too, and the `[` with this return
    return smith_lang_tuple_replace(lang, close + 1, nullptr, 0);
}

// What lambdas do with a token. True when the token is consumed.
static bool smith_lang_lambda_step(SmithLang *lang, const SmithHost *host, const SmithToken *token, bool at_start) {
    SmithLangLambda *state = &lang->lambda;
    if (token->foreign || lang->recording || smith_lang_capturing(lang)) return false;
    if (state->phase == 1) return smith_lang_lambda_collect(lang, host, token);
    if (state->pending > 0 && state->depth == 0) {
        smith_lang_lambda_flush(lang, host, token);
        return true;
    }
    if (host->previous == nullptr) return smith_lang_lambda_raw(lang, host, token);
    if (!host->maybe_brackets) return false;
    smith_lang_lambda_reset_text(lang);
    if (smith_lang_lambda_starts(lang, host, token)) {
        state->phase = 1;
        state->start = token->location;
        state->count = 0;
        state->parens = 0;
        state->braces = 0;
        state->body = false;
        return smith_lang_lambda_collect(lang, host, token);
    }
    return state->depth == 0 && smith_lang_lambda_declaration(lang, host, token, at_start);
}

// The token is the first of a declaration at file scope when the one before ended a declaration or a function (the
// host's record of the tokens before lags behind the lowering's, so the lowering keeps its own).
static bool smith_lang_lambda_token(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangLambda *state = &lang->lambda;
    if (token->foreign) return false;
    bool at_start = !state->started || (state->after_close && state->depth == 0);
    bool consumed = smith_lang_lambda_step(lang, host, token, at_start);
    if (!consumed && !lang->recording && !smith_lang_capturing(lang)) {
        state->started = true;
        if (smith_lang_punct(token, "{")) state->depth += 1;
        if (smith_lang_punct(token, "}") && state->depth > 0) state->depth -= 1;
        // a `module` or `import` line ends in a `;` that no token carries
        state->after_close = smith_lang_punct(token, ";") || smith_lang_punct(token, "}") ||
                             smith_lang_ident(token, "module") || smith_lang_ident(token, "import");
        state->last = *token; // the text of a name is the host's, valid for this token only
        if (token->kind == SmithTokenKind_Identifier) {
            bool is_return = smith_lang_ident(token, "return");
            state->last.text = is_return ? "return" : "x";
            state->last.length = is_return ? 6 : 1;
        }
    }
    return consumed;
}
