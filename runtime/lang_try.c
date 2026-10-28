// `try` and `errdefer` for `result` and `option` (issue #211). Included by runtime/lang.c, after runtime/lang_tuple.c.
//
// Both are keywords only in a module, as `defer` is. The lowering writes plain C with the dot rule of payload enums
// (the compiler reads `.error` and `.none` in the type the context gives), so it needs no type of the call:
//
//   T x = try f();      =>  T x = __extension__({ __auto_type smith_try_1 = f();
//                               if (smith_try_1 == .error) return {.error = smith_try_1.error};
//                               smith_try_1.ok; });
//   errdefer cleanup;   =>  bool smith_failing_2 = false; _Defer if (smith_failing_2) cleanup;
//   return e;           =>  { RT smith_ret_3 = e; smith_failing_2 = smith_ret_3 == .error; return smith_ret_3; }
//
// The kind (result or option) is the function's: the compiler's error for a call of the other kind carries a hint.
// A statement expression keeps the `try` where it is written, so nothing before it has to be rewritten.
// A generic's body is recorded as written; smith_lang_try_replay (runtime/lang_enum.c) writes the same for an instance.

static bool smith_lang_enum_return(SmithLang *lang, const SmithHost *host);

static constexpr char smith_lang_try_hint[] =
    "`try` returns the failure to the caller as it is: the function must return the same kind as the call (a `Result` "
    "with the same error type, or an `Option`); to change the error, write it out: `if (r == .error) return {.error = "
    "...};`";

typedef enum { SmithLangTryKind_None, SmithLangTryKind_Result, SmithLangTryKind_Option } SmithLangTryKind;

typedef enum { SmithLangTryStep_Inside, SmithLangTryStep_Ends, SmithLangTryStep_Escapes } SmithLangTryStep;

// The kind of the function being parsed, from its return type as the compiler prints it.
static SmithLangTryKind smith_lang_try_kind(const char *type) {
    if (strncmp(type, "struct smith_enum_result_", 25) == 0) return SmithLangTryKind_Result;
    if (strncmp(type, "struct smith_enum_option_", 25) == 0) return SmithLangTryKind_Option;
    return SmithLangTryKind_None;
}

// Writes space-separated words as tokens, in place of `like`.
static void smith_lang_try_say(SmithLang *lang, const SmithHost *host, const SmithToken *like, const char *words) {
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
        SmithToken last = smith_lang_tuple_made(kind, words + start, at - start, like, true);
        last.line_start = start == 0 && like->line_start; // the first stands where the source's token did
        smith_lang_emit_code(lang, host, last);
    }
}

[[gnu::format(printf, 4, 5)]] static void smith_lang_try_sayf(SmithLang *lang, const SmithHost *host,
                                                              const SmithToken *like, const char *format, ...) {
    char words[1200];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(words, sizeof words, format, arguments);
    va_end(arguments);
    smith_lang_try_say(lang, host, like, words);
}

// The flags of the `errdefer`s whose blocks are still open.
static void smith_lang_try_prune(SmithLang *lang) {
    SmithLangTry *tr = &lang->tr;
    while (tr->flag_count > 0 && tr->flags[tr->flag_count - 1].depth > lang->brace_depth)
        tr->flag_count -= 1;
}

// `smith_failing_1 = true ; smith_failing_2 = true ;`: the live flags, set before a return that fails.
static size_t smith_lang_try_set_flags(const SmithLangTry *tr, char *out, size_t capacity) {
    size_t used = 0;
    out[0] = 0;
    for (uint32_t i = 0; i < tr->flag_count && used < capacity; i++)
        used += (size_t)snprintf(out + used, capacity - used, "smith_failing_%u = true ; ", tr->flags[i].id);
    return used;
}

static SmithLangTryStep smith_lang_try_step(const SmithLang *lang, int32_t *parens, uint32_t start,
                                            const SmithToken *token, bool strict, bool payload) {
    if (lang->brace_depth < start) return SmithLangTryStep_Escapes;
    if (lang->brace_depth > start) return SmithLangTryStep_Inside;
    if (payload && *parens == 0 && (smith_lang_punct(token, "}") || smith_lang_punct(token, ","))) {
        return SmithLangTryStep_Ends; // the payload's own end: the token goes on after the statement expression
    }
    if (smith_lang_punct(token, "(") || smith_lang_punct(token, "[")) {
        *parens += 1;
    } else if (smith_lang_punct(token, ")") || smith_lang_punct(token, "]")) {
        if (*parens == 0) return SmithLangTryStep_Escapes;
        *parens -= 1;
    } else if (*parens == 0 && smith_lang_punct(token, ";")) {
        return SmithLangTryStep_Ends;
    } else if (*parens == 0 && strict && smith_lang_punct(token, ",")) {
        return SmithLangTryStep_Escapes;
    }
    return SmithLangTryStep_Inside;
}

// The end of a `try`: the early return and the value, written as the lowering of the dot rule writes them.
static void smith_lang_try_close(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTry *tr = &lang->tr;
    SmithToken at = *token;
    at.location = tr->try_location; // an error in what is written here is the `try`'s
    char flags[256];
    smith_lang_try_set_flags(tr, flags, sizeof flags);
    bool result = tr->try_kind == SmithLangTryKind_Result;
    const char *name = tr->type + 7; // after `struct `
    const char *failure = result ? "error" : "none";
    const char *good = result ? "ok" : "some";
    uint32_t id = tr->try_id;
    char same[400] = "";
    if (result) { // the error is returned as it is: its type must be the function's
        snprintf(same, sizeof same,
                 "_Static_assert ( __builtin_types_compatible_p ( __typeof__ ( smith_try_%u . error ) , __typeof__ ( "
                 "( ( %s * ) 0 ) -> error ) ) , \"`try` returns the error as it is: the function's error type must "
                 "be the call's; to change it, write it out\" ) ;",
                 id, tr->type);
    }
    char early[300];
    if (result) {
        snprintf(early, sizeof early, "return ( %s ) { . tag = %s_tag_%s , . error = smith_try_%u . error } ;",
                 tr->type, name, failure, id);
    } else {
        snprintf(early, sizeof early, "return ( %s ) { . tag = %s_tag_%s } ;", tr->type, name, failure);
    }
    char value[400];
    if (tr->try_wrap) { // `return try f();` returns the success value as the variant that holds it
        snprintf(value, sizeof value, "( %s ) { . tag = %s_tag_%s , . %s = smith_try_%u . %s }", tr->type, name, good,
                 good, id, good);
    } else {
        snprintf(value, sizeof value, "smith_try_%u . %s", id, good);
    }
    smith_lang_try_sayf(lang, host, &at,
                        "; %s if ( smith_try_%u . tag == %s_tag_%s ) { %s%s } if ( smith_try_%u . tag != %s_tag_%s ) "
                        "__builtin_unreachable ( ) ; %s ; } )",
                        same, id, name, failure, flags, early, id, name, good, value);
    tr->try_open = false;
}

// The end of a `return` rewritten for the `errdefer`s: the flags take the value's variant, then it returns.
static void smith_lang_try_close_return(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTry *tr = &lang->tr;
    smith_lang_try_prune(lang);
    char sets[400];
    size_t used = 0;
    sets[0] = 0;
    const char *failure = tr->ret_kind == SmithLangTryKind_Result ? "error" : "none";
    for (uint32_t i = 0; i < tr->flag_count; i++) {
        used +=
            (size_t)snprintf(sets + used, sizeof sets - used, "smith_failing_%u = smith_ret_%u . tag == %s_tag_%s ; ",
                             tr->flags[i].id, tr->ret_id, tr->ret_type + 7, failure);
    }
    smith_lang_try_sayf(lang, host, token, "; %s return smith_ret_%u ; }", sets, tr->ret_id);
    tr->ret_open = false;
}

static void smith_lang_try_larger(SmithLang *lang, const SmithHost *host, uint32_t location) {
    smith_lang_error(lang, host, location,
                     "`try` is a statement of its own, the whole value of a declaration, an assignment or a `return`: "
                     "inside a larger expression the order of evaluation would be unclear when it fails; give it "
                     "its own declaration (`T x = try f();`) and use `x`");
}

// The tokens that continue an open `try` or `return`: true when the token ends the `return` and is dropped.
static bool smith_lang_try_frames(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTry *tr = &lang->tr;
    smith_lang_try_prune(lang);
    if (tr->try_open) {
        SmithLangTryStep step = smith_lang_try_step(lang, &tr->try_parens, tr->try_start, token, true, tr->try_payload);
        if (smith_lang_ident(token, "try") && lang->brace_depth == tr->try_start) step = SmithLangTryStep_Escapes;
        if (step == SmithLangTryStep_Ends) smith_lang_try_close(lang, host, token);
        if (step == SmithLangTryStep_Escapes) {
            smith_lang_try_larger(lang, host, tr->try_location);
            tr->try_open = false;
        }
    }
    if (!tr->ret_open) return false;
    SmithLangTryStep step = smith_lang_try_step(lang, &tr->ret_parens, tr->ret_start, token, false, false);
    if (step == SmithLangTryStep_Escapes) {
        tr->ret_open = false;
        return false;
    }
    if (step != SmithLangTryStep_Ends) return false;
    smith_lang_try_close_return(lang, host, token);
    return true;
}

// Where a `try` is: the value of a declaration or an assignment, a statement of its own or after `return`.
typedef enum {
    SmithLangTryMode_Bad,
    SmithLangTryMode_Value,
    SmithLangTryMode_Statement,
    SmithLangTryMode_Payload
} SmithLangTryMode;

// `{.ok = ` or `, .ok = `: the `=` before the `try` gives a payload its value (a member's `p.ok = try f();` has a name
// before its `.`).
static bool smith_lang_try_designated(SmithLang *lang, const SmithHost *host) {
    SmithToken name;
    SmithToken dot;
    SmithToken open;
    return smith_lang_previous(lang, host, 2, &name) && name.kind == SmithTokenKind_Identifier &&
           smith_lang_previous(lang, host, 3, &dot) && smith_lang_punct(&dot, ".") &&
           smith_lang_previous(lang, host, 4, &open) && (smith_lang_punct(&open, "{") || smith_lang_punct(&open, ","));
}

static SmithLangTryMode smith_lang_try_mode(SmithLang *lang, const SmithHost *host) {
    if (lang->tr.just_returned) return SmithLangTryMode_Value;
    SmithToken before;
    if (!smith_lang_previous(lang, host, 1, &before)) return SmithLangTryMode_Statement;
    if (smith_lang_ident(&before, "return")) return SmithLangTryMode_Value;
    if (smith_lang_punct(&before, "="))
        return smith_lang_try_designated(lang, host) ? SmithLangTryMode_Payload : SmithLangTryMode_Value;
    if (smith_lang_ident(&before, "else") || smith_lang_punct(&before, ";") || smith_lang_punct(&before, "{") ||
        smith_lang_punct(&before, "}") || smith_lang_punct(&before, ")") || smith_lang_punct(&before, ":")) {
        return SmithLangTryMode_Statement;
    }
    return SmithLangTryMode_Bad;
}

// `try`: the start of the statement expression; the end is written at the `;`.
static void smith_lang_try_open(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTry *tr = &lang->tr;
    char type[256];
    if (!host->return_type(host->context, type, sizeof type)) {
        smith_lang_error(lang, host, token->location, "`try` is a statement of a function: it returns from it");
        return;
    }
    SmithLangTryKind kind = smith_lang_try_kind(type);
    if (kind == SmithLangTryKind_None) {
        smith_lang_error(lang, host, token->location,
                         "`try` returns the failure from the function, so the function must return a `Result(T, E)` "
                         "or an `Option(T)`; this one returns something else. Test the value by hand: `if (r == "
                         ".error) return ...;`");
        return;
    }
    SmithLangTryMode mode = smith_lang_try_mode(lang, host);
    if (tr->try_open || mode == SmithLangTryMode_Bad) {
        smith_lang_try_larger(lang, host, token->location);
        return;
    }
    if (host->hint != nullptr) host->hint(host->context, token->location, smith_lang_try_hint);
    tr->try_open = true;
    tr->try_parens = 0;
    tr->try_start = lang->brace_depth;
    tr->try_id = ++tr->next_id;
    tr->try_kind = kind;
    tr->try_payload = mode == SmithLangTryMode_Payload;
    SmithToken before;
    tr->try_wrap =
        mode == SmithLangTryMode_Value &&
        (tr->just_returned || (smith_lang_previous(lang, host, 1, &before) && smith_lang_ident(&before, "return")));
    tr->try_location = token->location;
    snprintf(tr->type, sizeof tr->type, "%s", type);
    smith_lang_try_sayf(lang, host, token, "%s __extension__ ( { __auto_type smith_try_%u =",
                        mode == SmithLangTryMode_Statement ? "( void )" : "", tr->try_id);
}

// `errdefer stmt;`: a flag of the block, and a `defer` that tests it.
static void smith_lang_errdefer(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTry *tr = &lang->tr;
    char type[256];
    if (!host->return_type(host->context, type, sizeof type) || smith_lang_try_kind(type) == SmithLangTryKind_None) {
        smith_lang_error(lang, host, token->location,
                         "`errdefer` runs when the function fails, and only a function that returns a `Result` or "
                         "an `Option` has a failure to mark; use `defer` here");
        return;
    }
    if (tr->flag_count == sizeof tr->flags / sizeof tr->flags[0]) {
        smith_lang_error(lang, host, token->location, "a function has at most %zu `errdefer`s open at once",
                         sizeof tr->flags / sizeof tr->flags[0]);
        return;
    }
    uint32_t id = ++tr->next_id;
    tr->flags[tr->flag_count].id = id;
    tr->flags[tr->flag_count].depth = lang->brace_depth;
    tr->flag_count += 1;
    smith_lang_try_sayf(lang, host, token, "bool smith_failing_%u = false ;", id);
    smith_lang_try_sayf(lang, host, token, "_Defer if ( smith_failing_%u )", id);
}

// `return v;` after an `errdefer`: the value is computed first, the flags read its variant, then it returns.
static void smith_lang_try_return(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTry *tr = &lang->tr;
    char type[256];
    if (!host->return_type(host->context, type, sizeof type)) return;
    SmithLangTryKind kind = smith_lang_try_kind(type);
    if (kind == SmithLangTryKind_None) return;
    tr->ret_open = true;
    tr->ret_parens = 0;
    tr->ret_start = lang->brace_depth;
    tr->ret_id = ++tr->next_id;
    tr->ret_kind = kind;
    snprintf(tr->ret_type, sizeof tr->ret_type, "%s", type);
    tr->after_return = true;
    smith_lang_try_sayf(lang, host, token, "{ %s smith_ret_%u =", type, tr->ret_id);
    smith_lang_enum_return(lang, host); // `return .none;` and `return {.error = e};` name the function's type
}

static bool smith_lang_try_member(SmithLang *lang, const SmithHost *host) {
    return smith_lang_previous_is(lang, host, 1, ".") || smith_lang_previous_is(lang, host, 1, "->");
}

// What `try`, `errdefer` and a `return` after an `errdefer` do with a token of a module. True when the token is
// consumed (its replacement is written).
static bool smith_lang_try_token(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTry *tr = &lang->tr;
    if (token->foreign || !lang->is_module || host->previous == nullptr || host->return_type == nullptr ||
        smith_lang_capturing(lang)) {
        return false;
    }
    bool try_word = smith_lang_ident(token, "try");
    bool errdefer_word = smith_lang_ident(token, "errdefer");
    if (!tr->on && !try_word && !errdefer_word) return false;
    if ((try_word || errdefer_word) && smith_lang_try_member(lang, host)) return false;
    if (lang->recording) return false; // a generic's body is kept as written; an instance is lowered when it is read
    tr->on = true;
    tr->just_returned = tr->after_return;
    tr->after_return = false;
    if (smith_lang_try_frames(lang, host, token)) return true;
    if (try_word) {
        smith_lang_try_open(lang, host, token);
        return true;
    }
    if (errdefer_word) {
        smith_lang_errdefer(lang, host, token);
        return true;
    }
    if (tr->flag_count > 0 && !tr->ret_open && smith_lang_ident(token, "return") &&
        !smith_lang_try_member(lang, host)) {
        smith_lang_try_return(lang, host, token);
        return tr->ret_open;
    }
    return false;
}
