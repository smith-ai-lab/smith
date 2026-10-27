// `_` and `_name` mean "unused" in a module (issue #413). Included by runtime/lang.c, after runtime/lang_try.c.
//
//   void f(i32 _, i32 _, Window *_window) { _ = g(); }   =>   void f(i32 smith_unused_1, i32 smith_unused_2, Window
//       *_window) { [[maybe_unused]] auto smith_discard_3 = g(); }
//
// A `_` that declares something is renamed, so that several may share a scope; `_ = expr;` at the start of a statement
// declares a variable that takes the value (a `[[nodiscard]]` result is used, no warning is left). A `_name` stays
// as written, and reading a name that starts with `_` (one already declared when the token comes) is the error. The
// names C reserves (`__x`, `_X`), a member (`p._x`, `p->_x`) and a name after `::` (`c::_exit`) are no module's.

static unsigned smith_lang_unused_serial;
static SmithToken smith_lang_unused_last; // the token before, for a lowering with no host record (the raw one)
static bool smith_lang_unused_has_last;
static SmithToken smith_lang_unused_last2; // and the one before that, for `[3][_]`

static bool smith_lang_unused_reserved(const SmithToken *token) {
    return token->length >= 2 && (token->text[1] == '_' || (token->text[1] >= 'A' && token->text[1] <= 'Z'));
}

// Words after which an expression, not a declarator, follows.
static bool smith_lang_unused_expression_word(const SmithToken *before) {
    static const char *const words[] = {"return", "case",  "sizeof",  "typeof", "countof", "defer",
                                        "try",    "throw", "alignof", "else",   "do"};
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) { // 11 words
        if (smith_lang_ident(before, words[i])) return true;
    }
    return false;
}

// The attribute `[[maybe_unused]]`, as the tokens clang reads (`[` `[` ... `]` `]`).
static size_t smith_lang_unused_attribute(SmithToken *made, const SmithToken *token, bool space) {
    made[0] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "[", 1, token, space);
    made[1] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "[", 1, token, false);
    made[2] = smith_lang_tuple_made(SmithTokenKind_Identifier, "maybe_unused", 12, token, false);
    made[3] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "]", 1, token, false);
    made[4] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "]", 1, token, false);
    return 5;
}

// `discard`: `_ = e;` is a variable that takes the value; else the `_` that declares is a name of its own.
static bool smith_lang_unused_write(SmithLang *lang, const SmithToken *token, bool discard, bool keep) {
    char name[48];
    if (keep) { // a `_name` that declares a variable: the name stays, and is not reported unused
        snprintf(name, sizeof name, "%.*s", (int)token->length, token->text);
    } else {
        snprintf(name, sizeof name, "smith_%s_%u", discard ? "discard" : "unused", ++smith_lang_unused_serial);
    }
    SmithToken made[12];
    size_t count = 0;
    if (discard) {
        count += smith_lang_unused_attribute(made, token, token->space_before);
        made[count++] = smith_lang_tuple_made(SmithTokenKind_Identifier, "auto", 4, token, true);
    }
    made[count++] =
        smith_lang_tuple_made(SmithTokenKind_Identifier, name, strlen(name), token, discard || token->space_before);
    if (!discard) { // after the name the attribute is GNU's: clang reads `[[` there as an array
        static const char *const words[] = {"__attribute__", "(", "(", "unused", ")", ")"};
        for (size_t i = 0; i < 6; i++) // 6 tokens
            made[count++] =
                smith_lang_tuple_made(i == 0 || i == 3 ? SmithTokenKind_Identifier : SmithTokenKind_Punctuator,
                                      words[i], strlen(words[i]), token, i == 0);
    }
    return smith_lang_tuple_replace(lang, 0, made, count);
}

// ---- Brackets with no size (issue #423)
//
// In a module `[_]` is a fixed array whose size the initializer gives (C's `[]`, written without the `_`), and `[]`
// is an error in every position: C's three meanings of it each have a spelling of their own.

// Whether the declaration the `]` closes began with `extern`: seen by every token, since the host's record is short.
static bool smith_lang_brackets_extern_seen;

static void smith_lang_brackets_track(const SmithToken *token) {
    if (token->synthetic) return;
    if (smith_lang_ident(token, "extern")) smith_lang_brackets_extern_seen = true;
    if (smith_lang_punct(token, ";") || smith_lang_punct(token, "{") || smith_lang_punct(token, "}")) {
        smith_lang_brackets_extern_seen = false;
    }
}

// Whether the tokens after the `]` at `index` (counted from the next token as 0) are more dimensions and then `=`, or
// the `)` and `{` of a compound literal. `literal` says which.
static bool smith_lang_brackets_initialized(SmithLang *lang, const SmithHost *host, size_t index, bool *literal) {
    *literal = false;
    for (size_t at = index; at < smith_lang_queue_max - 2; at++) { // a few dimensions
        const SmithToken *next = smith_lang_peek(lang, host, at, false);
        if (smith_lang_punct(next, "=")) return true;
        if (smith_lang_punct(next, ")")) {
            *literal = smith_lang_punct(smith_lang_peek(lang, host, at + 1, false), "{");
            return *literal;
        }
        if (!smith_lang_punct(next, "[")) return false;
        while (!smith_lang_punct(smith_lang_peek(lang, host, at, false), "]")) { // to the end of the dimension
            if (smith_lang_peek(lang, host, at, false)->kind == SmithTokenKind_Eof || at + 3 >= smith_lang_queue_max) {
                return false;
            }
            at += 1;
        }
    }
    return false;
}

// The `_` of `[_]`: the size comes from the initializer, so the `_` is not written to C.
static bool smith_lang_brackets_inferred(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                         const SmithToken *outer) {
    if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), "]")) return false; // `[_ + 1]` reads `_`: an error
    bool literal = false;
    const char *message = nullptr;
    if (lang->decl.in_params) {
        message =
            "`[_]` takes its size from an initializer, and a parameter has none: write `T xs[N]`, `T xs[:]` for a "
            "length known at run time, or `T *xs` for a pointer";
    } else if (smith_lang_punct(outer, "]")) {
        message = "only the outermost dimension may be `[_]`: write `T m[_][3] = {...}`";
    } else if (host->previous != nullptr && !smith_lang_brackets_initialized(lang, host, 1, &literal)) {
        message = "`[_]` takes its size from an initializer: write `T xs[_] = {...}`, or the size, `T xs[N]`";
    }
    if (message != nullptr) smith_lang_error(lang, host, token->location, "%s", message);
    return true; // the `_` is not written: C's `[]` has the size of the initializer
}

// Whether the token before is a `[`: the lowering's own record of the tokens offered to it, else the host's (a fast
// path takes some tokens without offering them).
static bool smith_lang_bracket_before(SmithLang *lang, const SmithHost *host, bool has_own, const SmithToken *own) {
    if (has_own && smith_lang_punct(own, "[")) return true;
    SmithToken back = {};
    return host->previous != nullptr && smith_lang_previous(lang, host, 1, &back) && smith_lang_punct(&back, "[");
}

// The `]` after a `[`: an error in a module.
static void smith_lang_brackets_empty(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                      bool after_open) {
    if (!lang->is_module || token->foreign || token->synthetic || token->replay || lang->recording || !after_open ||
        lang->decl.in_params) {
        return;
    }
    bool literal = false;
    bool declared_extern = smith_lang_brackets_extern_seen;
    const char *message =
        "`[]` is no array in a module: write `T xs[_] = {...}` for a fixed array of inferred size, or `T xs[:]` for a "
        "slice";
    if (smith_lang_brackets_initialized(lang, host, 0, &literal)) {
        if (literal) message = "`(T[])` is no array in a module: write `(T[_]){...}`";
    } else if (declared_extern) {
        message = "an `extern` array needs its size: write `extern T xs[N]`";
    } else if (smith_lang_punct(smith_lang_peek(lang, host, 0, false), ";")) {
        message = "a flexible array member has its length apart from its data: use a slice field, `T xs[:]`";
    }
    smith_lang_error(lang, host, token->location, "%s", message);
}

// A `]`: after a `_` the host's record shows the `[` (the `_` was dropped), and the lowering's own shows the `_`.
static void smith_lang_brackets_close(SmithLang *lang, const SmithHost *host, const SmithToken *token, bool has_own,
                                      const SmithToken *own) {
    bool dropped = has_own && smith_lang_ident(own, "_");
    smith_lang_brackets_empty(lang, host, token, !dropped && smith_lang_bracket_before(lang, host, has_own, own));
}

// A `_` or a name starting with `_` in a module: true when the token was replaced.
static bool smith_lang_unused_token(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    smith_lang_brackets_track(token);
    SmithToken own = smith_lang_unused_last;
    SmithToken own2 = smith_lang_unused_last2;
    bool has_own = smith_lang_unused_has_last;
    if (!token->synthetic) {
        smith_lang_unused_last2 = smith_lang_unused_last;
        smith_lang_unused_last = *token;
        smith_lang_unused_has_last = true;
    }
    if (smith_lang_punct(token, "]")) smith_lang_brackets_close(lang, host, token, has_own, &own);
    if (!lang->is_module || token->foreign || token->synthetic || token->replay || lang->recording ||
        token->kind != SmithTokenKind_Identifier || token->length == 0 || token->text[0] != '_' ||
        smith_lang_unused_reserved(token)) {
        return false;
    }
    SmithToken before = {};
    bool known = host->previous != nullptr && smith_lang_previous(lang, host, 1, &before);
    if (!known && host->previous == nullptr && has_own) {
        before = own;
        known = true;
    }
    if (known &&
        (smith_lang_punct(&before, ".") || smith_lang_punct(&before, "->") || smith_lang_punct(&before, "::"))) {
        return false;
    }
    if (token->length == 1 && smith_lang_bracket_before(lang, host, has_own, &own)) {
        return smith_lang_brackets_inferred(lang, host, token, &own2);
    }
    if (token->length > 1) {
        if (host->lookup == nullptr ||
            host->lookup(host->context, token->text, token->length, SmithLookup_Value).kind == SmithLookup_None) {
            const SmithToken *after = smith_lang_peek(lang, host, 0, false);
            bool variable =
                known && ((before.kind == SmithTokenKind_Identifier && !smith_lang_unused_expression_word(&before)) ||
                          smith_lang_punct(&before, "*"));
            if (variable &&
                (smith_lang_punct(after, ",") || smith_lang_punct(after, ")") || smith_lang_punct(after, "=") ||
                 smith_lang_punct(after, ";") || smith_lang_punct(after, "["))) {
                return smith_lang_unused_write(lang, token, false, true);
            }
            return false; // it declares the name
        }
        smith_lang_error(lang, host, token->location,
                         "`%.*s` is marked unused (it starts with `_`); rename it to use it", (int)token->length,
                         token->text);
        return false;
    }
    const SmithToken *next = smith_lang_peek(lang, host, 0, false);
    bool statement =
        !known || smith_lang_punct(&before, ";") || smith_lang_punct(&before, "{") || smith_lang_punct(&before, "}");
    if (smith_lang_punct(next, "=")) {
        if (statement) return smith_lang_unused_write(lang, token, true, false);
        if (smith_lang_punct(&before, ")") || smith_lang_ident(&before, "else") || smith_lang_ident(&before, "do")) {
            smith_lang_error(lang, host, token->location,
                             "`_ = expr;` is a statement of its own; put it in braces after the condition");
            return false;
        }
    }
    bool declares = !known || lang->decl.in_params ||
                    (before.kind == SmithTokenKind_Identifier && !smith_lang_unused_expression_word(&before)) ||
                    smith_lang_punct(&before, "*") || smith_lang_punct(&before, "]") || smith_lang_punct(&before, ")");
    if (declares) return smith_lang_unused_write(lang, token, false, false);
    smith_lang_error(lang, host, token->location,
                     "`_` is marked unused: declare it (a parameter or a local) or discard a value with `_ = expr;`, "
                     "but it is never read");
    return false;
}
