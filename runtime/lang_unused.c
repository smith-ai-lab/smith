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

// A `_` or a name starting with `_` in a module: true when the token was replaced.
static bool smith_lang_unused_token(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithToken own = smith_lang_unused_last;
    bool has_own = smith_lang_unused_has_last;
    if (!token->synthetic) {
        smith_lang_unused_last = *token;
        smith_lang_unused_has_last = true;
    }
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
