// The effects of a function type (issue #396). Included by runtime/lang.c, after runtime/lang_unused.c.
//
//   void (*log)(const char *) [[smith::effects(console)]];   =>   ... [[clang::annotate_type("smith::effects:console")]];
//
// C23 puts an attribute after a function declarator's parameter list on the function type, but a plugin's attribute
// cannot sit on a type (clang refuses it), while its own `annotate_type` can and is kept through a typedef, a field, a
// parameter and a call. `[[smith::pure]]` there is `annotate_type("smith::effects:")`: no effects. `std::` is read as
// `smith::`. Only the position after `)` is a type's; a function's own attribute goes before it, as before.

static bool smith_lang_effects_paren; // the token before was `)`

static bool smith_lang_effects_scope(const SmithToken *token) {
    return smith_lang_ident(token, "smith") || smith_lang_ident(token, "std");
}

// The names between the parentheses of `effects(a, b)` into `names` (joined by commas) and the index of the token
// after the `)`; 0 when the tokens are some other shape.
static size_t smith_lang_effects_names(SmithLang *lang, const SmithHost *host, size_t open, char *names,
                                       size_t capacity) {
    size_t used = 0;
    size_t at = open + 1;
    names[0] = 0;
    for (size_t guard = 0; guard < 16; guard++) { // an effect name, then `,` or `)`
        const SmithToken *name = smith_lang_peek(lang, host, at, false);
        if (name->kind != SmithTokenKind_Identifier || used + name->length + 2 > capacity) return 0;
        used += (size_t)snprintf(names + used, capacity - used, "%s%.*s", used > 0 ? "," : "", (int)name->length,
                                 name->text);
        const SmithToken *after = smith_lang_peek(lang, host, at + 1, false);
        if (smith_lang_punct(after, ")")) return at + 2;
        if (!smith_lang_punct(after, ",")) return 0;
        at += 2;
    }
    return 0;
}

// At the first `[` of `[[smith::effects(...)]]` or `[[smith::pure]]` right after `)`: the tokens after it become
// `[clang::annotate_type("smith::effects:...")]]`.
static void smith_lang_effects_type(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    bool after_paren = smith_lang_effects_paren;
    smith_lang_effects_paren = smith_lang_punct(token, ")");
    if (!host->maybe_effects_type || !after_paren || !smith_lang_punct(token, "[") || token->synthetic ||
        token->foreign) {
        return;
    }
    if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), "[") ||
        !smith_lang_effects_scope(smith_lang_peek(lang, host, 1, false)) ||
        !smith_lang_punct(smith_lang_peek(lang, host, 2, false), "::")) {
        return;
    }
    const SmithToken *word = smith_lang_peek(lang, host, 3, false);
    char names[smith_lang_name_max] = "";
    size_t close = 0; // the index of the first `]`
    if (smith_lang_ident(word, "pure")) {
        close = 4;
    } else if (smith_lang_ident(word, "effects") && smith_lang_punct(smith_lang_peek(lang, host, 4, false), "(")) {
        close = smith_lang_effects_names(lang, host, 4, names, sizeof names);
    }
    if (close == 0 || !smith_lang_punct(smith_lang_peek(lang, host, close, false), "]") ||
        !smith_lang_punct(smith_lang_peek(lang, host, close + 1, false), "]")) {
        return;
    }
    char quoted[smith_lang_name_max + 24];
    int length = snprintf(quoted, sizeof quoted, "\"smith::effects:%s\"", names);
    SmithToken made[9];
    made[0] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "[", 1, token, false);
    made[1] = smith_lang_tuple_made(SmithTokenKind_Identifier, "clang", 5, token, false);
    made[2] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "::", 2, token, false);
    made[3] = smith_lang_tuple_made(SmithTokenKind_Identifier, "annotate_type", 13, token, false);
    made[4] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "(", 1, token, false);
    made[5] = smith_lang_tuple_made(SmithTokenKind_String, quoted, (size_t)length, token, false);
    made[6] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ")", 1, token, false);
    made[7] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "]", 1, token, false);
    made[8] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "]", 1, token, false);
    smith_lang_tuple_replace(lang, close + 2, made, 9);
}
