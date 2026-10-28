// The head of a file-scope declaration in a module: held until it is known what it declares, then written out with the
// module's prefix, `static` if it is private, and for an exported function a prototype carrying the symbol.
// Included by runtime/lang.c, after runtime/lang_import.c.

typedef enum {
    SmithLangForm_Type,     // a typedef, a tag, an assertion: nothing is declared that has a symbol
    SmithLangForm_Function, // a name followed by a parameter list
    SmithLangForm_Variable,
} SmithLangForm;

static SmithToken smith_lang_held_token(const SmithLang *lang, const SmithLangHeld *held) {
    SmithToken token = {};
    token.kind = held->kind;
    token.line_start = held->line_start;
    token.space_before = held->space_before;
    token.expand = held->expand;
    token.synthetic = held->synthetic;
    token.via = held->via;
    token.name = held->role == SmithLangRole_Declared;
    token.location = held->location;
    token.handle = held->handle;
    token.text = smith_lang_scratch_str(held->text);
    token.length = held->text.length;
    if (held->header_text.length > 0) {
        token.header_text = smith_lang_scratch_str(held->header_text);
        token.header_length = held->header_text.length;
    }
    (void)lang;
    return token;
}

static void smith_lang_hold(SmithLang *lang, const SmithToken *token, SmithLangRole role,
                            const SmithLangContext *context) {
    if (lang->head_count >= smith_lang_head_max) {
        lang->broken = true;
        return;
    }
    SmithLangHeld *held = &lang->head[lang->head_count++];
    held->kind = token->kind;
    held->role = role;
    held->line_start = token->line_start || lang->carry_line_start;
    lang->carry_line_start = false;
    held->space_before = token->space_before;
    held->expand = token->expand;
    held->synthetic = token->synthetic;
    held->hoisted = false;
    held->via = token->via;
    held->after_member = context->after_member;
    held->after_tag = context->after_tag;
    held->tag_word = context->tag_word;
    held->typeish = context->typeish;
    held->before = context->before;
    held->location = token->location;
    held->handle = token->handle;
    held->paren_depth = lang->paren_depth;
    held->text = smith_lang_scratch_store(lang, token->text, token->length);
    held->header_text = token->header_text != nullptr && token->header_length > 0
                            ? smith_lang_scratch_store(lang, token->header_text, token->header_length)
                            : (SmithLangText){0, 0};
    held->original = held->text;
}

static void smith_lang_emit_held(SmithLang *lang, const SmithHost *host, size_t index, bool header_too) {
    SmithToken token = smith_lang_held_token(lang, &lang->head[index]);
    if (header_too) {
        smith_lang_emit(lang, host, token);
    } else {
        smith_lang_emit_code(lang, host, token);
    }
}

// ---- Resolving a name once the token after it is known
// -----------------------------------------------------------------

// Whether the held token at `index` is inside the parentheses of `typeof` (or `__typeof__`): what is there is an
// expression, and `typeof(*x)` declares nothing.
static bool smith_lang_in_typeof(const SmithLang *lang, size_t index) {
    int depth = 0;
    for (size_t i = index; i > 0; i--) { // back to the unmatched `(`
        const SmithLangHeld *before = &lang->head[i - 1];
        const char *text = smith_lang_scratch_str(before->text);
        if (before->kind == SmithTokenKind_Punctuator && before->text.length == 1) {
            depth += text[0] == ')' ? 1 : 0;
            if (text[0] == '(' && depth-- == 0) {
                if (i < 2) return false;
                const SmithLangHeld *keyword = &lang->head[i - 2];
                const char *word = smith_lang_scratch_str(keyword->text);
                return smith_lang_equals(word, keyword->text.length, "typeof") ||
                       smith_lang_equals(word, keyword->text.length, "__typeof__") ||
                       smith_lang_equals(word, keyword->text.length, "typeof_unqual");
            }
        }
    }
    return false;
}

// Whether the identifier just held introduces a name, given what follows it.
static bool smith_lang_declares_here(const SmithLang *lang, const SmithLangHeld *held, const SmithToken *next) {
    if (held->synthetic || held->after_member || held->after_tag || !lang->decl.saw_type || lang->decl.name_found) {
        return false; // a name the lowering made (a slice's `data`) declares nothing
    }
    if (held->paren_depth == 0) {
        return smith_lang_punct(next, "(") || smith_lang_punct(next, "[") || smith_lang_punct(next, "=") ||
               smith_lang_punct(next, ";") || smith_lang_punct(next, ",");
    }
    // void (*callback)(int): the name inside the first parentheses, after a star (`typeof(*x)` is no declarator).
    return held->paren_depth == 1 && held->before == '*' && smith_lang_punct(next, ")") && !lang->decl.in_params &&
           !smith_lang_in_typeof(lang, (size_t)(held - lang->head));
}

static bool smith_lang_type_use_follows(const SmithToken *next) {
    if (next->kind == SmithTokenKind_Identifier) return true;
    return smith_lang_punct(next, "*");
}

static void smith_lang_replace_held(SmithLang *lang, SmithLangHeld *held, const SmithLangLowered *lowered) {
    if (!lowered->replaced) return;
    held->handle = smith_token_no_handle;
    held->text = smith_lang_scratch_store(lang, lowered->text, lowered->length);
    held->expand = lowered->expand;
    if (lowered->header != nullptr)
        held->header_text = smith_lang_scratch_store(lang, lowered->header, lowered->header_length);
}

// Whether the name held at `index` is the `Y` of `constexpr type Y = X;`, a type alias: a module's type name, as a
// typedef's is.
static bool smith_lang_alias_name(const SmithLang *lang, size_t index, const SmithToken *next) {
    if (!lang->is_module || !lang->decl.is_constexpr || lang->decl.in_params || index == 0 ||
        !smith_lang_punct(next, "=")) {
        return false;
    }
    const SmithLangHeld *before = &lang->head[index - 1];
    return before->kind == SmithTokenKind_Identifier &&
           smith_lang_equals(smith_lang_scratch_str(before->original), before->original.length, "type");
}

static void smith_lang_resolve(SmithLang *lang, const SmithHost *host, const SmithToken *next) {
    if (lang->pending == SIZE_MAX) return;
    SmithLangHeld *held = &lang->head[lang->pending];
    size_t index = lang->pending;
    lang->pending = SIZE_MAX;
    const char *name = smith_lang_scratch_str(held->text);
    size_t length = held->text.length;
    bool is_type = false;
    bool declared = false;
    if (held->role == SmithLangRole_Plain && smith_lang_word(name, length) == SmithLangWord_Other &&
        smith_lang_declares_here(lang, held, next)) {
        bool alias = smith_lang_alias_name(lang, index, next);
        lang->decl.is_alias = alias;
        SmithLangNameKind kind = lang->decl.is_typedef || alias ? SmithLangName_Type
                                 : smith_lang_punct(next, "(")  ? SmithLangName_Function
                                                                : SmithLangName_Value;
        const char *prefixed = smith_lang_declare(lang, host, name, length, held->location, kind, lang->decl.exported);
        if (prefixed != nullptr) {
            SmithLangLowered lowered = {.replaced = true, .text = prefixed, .length = lang->tmp_length};
            smith_lang_replace_held(lang, held, &lowered);
        }
        held->role = SmithLangRole_Declared;
        declared = true;
        lang->decl.name_found = true;
        lang->decl.declared += 1;
        lang->decl.last_name = (uint32_t)index;
        if (kind == SmithLangName_Function) {
            lang->decl.is_function = true;
            lang->decl.params_next = true;
        }
    } else if (held->role == SmithLangRole_Plain) {
        SmithLangContext context = {held->after_member, held->after_tag, held->tag_word,
                                    held->typeish,      false,           held->before};
        lang->generated_name = lang->generated_name || held->synthetic;
        SmithLangLowered lowered = smith_lang_lower(lang, host, name, length, held->location, &context, next);
        lang->generated_name = false;
        smith_lang_replace_held(lang, held, &lowered);
        is_type = lowered.is_type;
    }
    if (!declared) {
        // `T[n] f(...)`: the type before the bracket of a generic's array result
        bool array_result =
            lang->array_result_open != 0 && smith_lang_punct(next, "[") && next->location == lang->array_result_open;
        is_type = is_type || smith_lang_type_use_follows(next) || array_result;
    }
    if (!declared && !is_type && lang->paren_depth == 0 && lang->bracket_depth == 0 && smith_lang_punct(next, "(")) {
        // `uint64_t (*hook)(int)`: a name before the `(*` of a function pointer's declarator is the result type
        is_type = smith_lang_punct(smith_lang_peek(lang, host, 0, false), "*");
    }
    if (is_type) lang->decl.saw_type = true;
    SmithToken as_token = smith_lang_held_token(lang, held);
    smith_lang_track(lang, &as_token, is_type);
    lang->typeish = is_type;
    lang->prev_name_length = 0;
}

// ---- Taking a token into the head
// ----------------------------------------------------------------------------------------

static void smith_lang_note_keyword(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangWord word = smith_lang_word(token->text, token->length);
    if (word == SmithLangWord_Typedef) lang->decl.is_typedef = true;
    if (word == SmithLangWord_TypeKeyword) lang->decl.saw_type = true;
    if (word == SmithLangWord_Tag) {
        lang->decl.aggregate = true;
        lang->decl.last_tag_enum = smith_lang_ident(token, "enum");
        lang->decl.last_tag_word = token->text[0];
    }
    if (word == SmithLangWord_Storage && lang->bracket_depth == 0) { // `[static N]` in a parameter is no storage
        if (smith_lang_ident(token, "static") || smith_lang_ident(token, "extern")) lang->decl.storage = true;
        if (smith_lang_ident(token, "extern")) lang->decl.is_extern = true;
        if (smith_lang_ident(token, "static") && lang->decl.exported) {
            smith_lang_error(lang, host, token->location,
                             "an exported declaration cannot be `static`: export is what makes it public");
        }
        if (smith_lang_ident(token, "constexpr")) lang->decl.is_constexpr = true;
    }
    if (smith_lang_ident(token, "inline")) lang->decl.is_inline = true;
}

static bool smith_lang_head_punct(SmithLang *lang, const SmithToken *token) {
    if (token->tuple_brace) { // a tuple's definition is part of the head, as a parenthesis is
        if (smith_lang_punct(token, "{")) {
            lang->paren_depth += 1;
        } else if (lang->paren_depth > 0) {
            lang->paren_depth -= 1;
        }
        return false;
    }
    if (smith_lang_punct(token, "(")) {
        if (lang->decl.params_next && lang->paren_depth == 0) {
            lang->decl.in_params = true;
            lang->params_count = 0;
        }
        lang->decl.params_next = false;
        lang->paren_depth += 1;
    } else if (smith_lang_punct(token, ")")) {
        if (lang->paren_depth > 0) lang->paren_depth -= 1;
        if (lang->paren_depth == 0) lang->decl.in_params = false;
        if (lang->paren_depth == 0 && lang->decl.result_call && !lang->decl.name_found) {
            lang->decl.result_call = false; // `slice_of(T, k)` is a type: the name is next
            lang->decl.saw_type = true;
        }
    } else if (smith_lang_punct(token, "[")) {
        lang->bracket_depth += 1;
    } else if (smith_lang_punct(token, "]")) {
        if (lang->bracket_depth > 0) lang->bracket_depth -= 1;
    } else if (smith_lang_punct(token, ",") && lang->paren_depth == 0 && lang->bracket_depth == 0) {
        lang->decl.name_found = false;
    }
    if (lang->paren_depth != 0 || lang->bracket_depth != 0) return false;
    return smith_lang_punct(token, ";") || smith_lang_punct(token, "=") || smith_lang_punct(token, "{");
}

// ---- Writing the head out
// ------------------------------------------------------------------------------------------------

// A module's symbol is `_` and its path (`_smith::test::order`) on every platform: Mach-O's own spelling of a C name,
// and on Linux the same, so that a header written on one platform (include/smith/*.h, generated on macOS) names the
// symbols a compile on the other defines (issue #305).
constexpr char smith_lang_symbol_underscore[] = "_";

static SmithToken smith_lang_punct_after(const char *text, const SmithToken *like) {
    SmithToken made = smith_lang_synth(SmithTokenKind_Punctuator, text, strlen(text), like);
    made.line_start = false;
    made.space_before = false;
    made.synthetic = true;
    return made;
}

// __asm__("_game::replay::open") after a declarator, so the symbol is the module's.
static void smith_lang_emit_label(SmithLang *lang, const SmithHost *host, const SmithToken *like, bool header_too) {
    const SmithLangHeld *name_held = &lang->head[lang->decl.last_name];
    char label[smith_lang_name_max * 2];
    int length = snprintf(label, sizeof label, "\"%s%s%s\"", smith_lang_symbol_underscore, lang->symbol,
                          smith_lang_scratch_str(name_held->original));
    SmithToken asm_token = smith_lang_synth(SmithTokenKind_Identifier, "__asm__", 7, like);
    asm_token.line_start = false;
    asm_token.space_before = true;
    asm_token.synthetic = true;
    SmithToken open = smith_lang_punct_after("(", like);
    SmithToken string = smith_lang_synth(SmithTokenKind_String, label, (size_t)length, like);
    string.line_start = false;
    string.space_before = false;
    string.synthetic = true;
    SmithToken close = smith_lang_punct_after(")", like);
    SmithToken pieces[4] = {asm_token, open, string, close};
    for (size_t i = 0; i < 4; i++) { // the four pieces
        if (header_too) {
            smith_lang_emit(lang, host, pieces[i]);
        } else {
            smith_lang_emit_code(lang, host, pieces[i]);
        }
    }
}

static SmithToken smith_lang_last_of_head(const SmithLang *lang) {
    return smith_lang_held_token(lang, &lang->head[lang->head_count - 1]);
}

// Where `static` goes in the held head: after the attributes the declaration begins with ([[nodiscard]] static bool
// f()), since an attribute that follows a storage class belongs to the type and not to the function.
static size_t smith_lang_head_after_attributes(const SmithLang *lang) {
    size_t at = 0;
    while (at + 1 < lang->head_count) { // each leading [[ ]]
        const SmithLangHeld *open = &lang->head[at];
        const SmithLangHeld *second = &lang->head[at + 1];
        if (!smith_lang_equals(smith_lang_scratch_str(open->text), open->text.length, "[") ||
            !smith_lang_equals(smith_lang_scratch_str(second->text), second->text.length, "["))
            break;
        int depth = 0;
        size_t end = at;
        for (; end < lang->head_count; end++) { // to the matching ]
            const SmithLangHeld *held = &lang->head[end];
            const char *text = smith_lang_scratch_str(held->text);
            depth += smith_lang_equals(text, held->text.length, "[") ? 1 : 0;
            depth -= smith_lang_equals(text, held->text.length, "]") ? 1 : 0;
            if (depth == 0) break;
        }
        if (end >= lang->head_count) break;
        at = end + 1;
    }
    return at < lang->head_count ? at : 0;
}

// Whether a token of the held head is dropped: a private function or variable that has a symbol is neither `static`
// nor `inline` (a C `inline` definition has no symbol of its own).
static bool smith_lang_head_dropped(const SmithLang *lang, size_t index) {
    if (!lang->decl.linked) return false;
    const SmithLangHeld *held = &lang->head[index];
    if (held->kind != SmithTokenKind_Identifier || held->paren_depth != 0) return false;
    const char *text = smith_lang_scratch_str(held->text);
    return smith_lang_equals(text, held->text.length, "static") ||
           smith_lang_equals(text, held->text.length, "inline") ||
           smith_lang_equals(text, held->text.length, "__inline") ||
           smith_lang_equals(text, held->text.length, "__inline__");
}

// ---- An array parameter keeps its size
//
// In a module a parameter `T xs[N]` is N objects: the code is written `T xs[static N]`, which clang knows as a pointer
// that is not null and has N objects (`dereferenceable`), and which `smith prove` reads. The generated header keeps
// the C declaration. The first dimension only; a parameter with no size is an error.

typedef enum { SmithLangArray_None, SmithLangArray_Sized, SmithLangArray_Unsized } SmithLangArray;

// What the token after the `[` of an array parameter says of its size.
static SmithLangArray smith_lang_array_size(const SmithToken *token) {
    if (smith_lang_punct(token, "]")) return SmithLangArray_Unsized;
    if (smith_lang_punct(token, "*") || smith_lang_punct(token, ".")) return SmithLangArray_None;
    if (token->kind == SmithTokenKind_Identifier) {
        SmithLangWord word = smith_lang_word(token->text, token->length);
        if (word == SmithLangWord_Storage || word == SmithLangWord_Qualifier) return SmithLangArray_None;
    }
    return SmithLangArray_Sized;
}

static void smith_lang_unsized_error(SmithLang *lang, const SmithHost *host, uint32_t location) {
    smith_lang_error(lang, host, location,
                     "an array parameter needs its size: write `T xs[N]`, `T xs[:]` for a length known at run time, "
                     "or `T *xs` for a pointer");
}

// ---- The size of an array parameter, kept for `countof(xs)`
//
// Both ways a head is read (a private one as it streams by, an exported one held) tell this each token of a parameter
// list, with how deep in `[ ]` it is before and after. The tokens of each dimension are kept as the code wrote them.

static void smith_lang_array_token(SmithLang *lang, const SmithHost *host, const char *text, size_t length,
                                   SmithTokenKind kind, uint32_t before, uint32_t after) {
    bool open = kind == SmithTokenKind_Punctuator && length == 1 && text[0] == '[';
    bool close = kind == SmithTokenKind_Punctuator && length == 1 && text[0] == ']';
    SmithLangArrayParam *param =
        lang->array_param_count > 0 ? &lang->array_params[lang->array_param_count - 1] : nullptr;
    if (open && before == 0) {
        lang->array_capture = -1;
        if (lang->array_after_close && param != nullptr && param->dims < smith_lang_array_dims_max) {
            lang->array_capture = (int32_t)param->dims++;
            param->sizes[lang->array_capture].whole = true;
        } else if (lang->decl.prev_ident && lang->array_param_count < smith_lang_array_params_max &&
                   lang->array_pending[0] != 0) {
            param = &lang->array_params[lang->array_param_count++];
            *param = (SmithLangArrayParam){.dims = 1};
            memcpy(param->name, lang->array_pending, sizeof param->name);
            param->sizes[0].whole = true;
            lang->array_capture = 0;
            if (!lang->countof_watched && host->watch != nullptr) {
                host->watch(host->context, "countof", 7);
                lang->countof_watched = true;
            }
        }
        lang->array_after_close = false;
        return;
    }
    if (close && after == 0) {
        lang->array_capture = -1;
        lang->array_after_close = param != nullptr;
        return;
    }
    lang->array_after_close = false;
    if (lang->array_capture >= 0 && param != nullptr) {
        __typeof__(param->sizes[0]) *size = &param->sizes[lang->array_capture];
        if (size->count < smith_lang_array_tokens_max && length < smith_lang_array_text_max) {
            size->kinds[size->count] = kind;
            memcpy(size->texts[size->count], text, length);
            size->texts[size->count][length] = 0;
            size->count += 1;
        } else {
            size->whole = false;
        }
    } else if (before == 0 && kind == SmithTokenKind_Identifier && length < sizeof lang->array_pending) {
        memcpy(lang->array_pending, text, length);
        lang->array_pending[length] = 0;
    }
}

static bool smith_lang_slice_defined(const SmithLang *lang, const char *tag);

// The end of the braces that open at the held token `open`, or `head_count` when they do not close.
static size_t smith_lang_head_close_brace(const SmithLang *lang, size_t open) {
    size_t depth = 0;
    for (size_t at = open; at < lang->head_count; at++) { // to the matching }
        const SmithLangHeld *held = &lang->head[at];
        const char *text = smith_lang_scratch_str(held->text);
        if (held->kind != SmithTokenKind_Punctuator || held->text.length != 1) continue;
        depth += text[0] == '{' ? 1 : 0;
        if (text[0] == '}' && --depth == 0) return at;
    }
    return lang->head_count;
}

// Whether the held tokens at `at` begin the definition of a slice's struct in a parameter list: `struct smith_array_...
// {`, all made by the lowering.
static bool smith_lang_head_slice_struct(const SmithLang *lang, size_t at) {
    if (at + 2 >= lang->head_count || lang->head[at].paren_depth == 0) return false;
    const SmithLangHeld *keyword = &lang->head[at];
    const SmithLangHeld *tag = &lang->head[at + 1];
    const SmithLangHeld *brace = &lang->head[at + 2];
    const char *name = smith_lang_scratch_str(tag->text);
    return keyword->synthetic && tag->synthetic && brace->synthetic &&
           smith_lang_equals(smith_lang_scratch_str(keyword->text), keyword->text.length, "struct") &&
           tag->text.length > 12 && memcmp(name, "smith_array_", 12) == 0 &&
           smith_lang_equals(smith_lang_scratch_str(brace->text), brace->text.length, "{");
}

// An exported function's prototype and its definition both define a slice's struct in their parameter lists: two
// types that clang's `-fsanitize=function` then mangles through each other until the stack is gone (issue #152; the
// same text in C crashes clang 23). The code defines each struct once at file scope, before the head, and the
// parameter names the tag; the header keeps the definition in the prototype.
static void smith_lang_hoist_slice_structs(SmithLang *lang, const SmithHost *host) {
    for (size_t at = 0; at < lang->head_count; at++) { // bounded by the head
        if (!smith_lang_head_slice_struct(lang, at)) continue;
        size_t close = smith_lang_head_close_brace(lang, at + 2);
        if (close >= lang->head_count) return;
        char tag[smith_lang_name_max];
        snprintf(tag, sizeof tag, "%.*s", (int)lang->head[at + 1].text.length,
                 smith_lang_scratch_str(lang->head[at + 1].text));
        bool first = !smith_lang_slice_defined(lang, tag);
        if (first && lang->slice.tag_count < 32) {
            snprintf(lang->slice.tags[lang->slice.tag_count++], sizeof lang->slice.tags[0], "%s", tag);
        }
        for (size_t i = at; i <= close; i++) { // the definition goes first, and is not written in the head again
            if (first) {
                SmithToken token = smith_lang_held_token(lang, &lang->head[i]);
                token.line_start = i == at;
                smith_lang_emit_code(lang, host, token);
            }
            if (i >= at + 2) lang->head[i].hoisted = true;
        }
        if (first) {
            SmithToken last = smith_lang_held_token(lang, &lang->head[close]);
            smith_lang_emit_code(lang, host, smith_lang_punct_after(";", &last));
        }
        at = close;
    }
}

static void smith_lang_emit_head(SmithLang *lang, const SmithHost *host, bool header_too, bool with_static) {
    size_t static_at = with_static ? smith_lang_head_after_attributes(lang) : SIZE_MAX;
    bool function = lang->decl.is_function && !lang->decl.is_typedef;
    lang->array_param_count = 0;
    lang->array_capture = -1;
    lang->array_after_close = false;
    size_t brackets = 0;
    bool param_bracket = false;
    for (size_t i = 0; i < lang->head_count; i++) { // bounded by the head
        if (smith_lang_head_dropped(lang, i)) continue;
        SmithLangHeld *held = &lang->head[i];
        SmithToken now = smith_lang_held_token(lang, held);
        if (held->hoisted) { // the code names the tag; the header keeps the definition
            if (header_too) smith_lang_emit_header(lang, now);
            continue;
        }
        if (param_bracket) {
            param_bracket = false;
            SmithLangArray size = smith_lang_array_size(&now);
            if (size == SmithLangArray_Unsized && !header_too) smith_lang_unsized_error(lang, host, now.location);
            if (size == SmithLangArray_Sized) {
                smith_lang_emit_code(lang, host, smith_lang_static_keyword(&now));
            }
        }
        size_t brackets_before = brackets;
        if (function && smith_lang_punct(&now, "[")) {
            bool named = false;
            if (i > 0) {
                const SmithLangHeld *before = &lang->head[i - 1];
                named =
                    before->kind == SmithTokenKind_Identifier && !before->synthetic && // not a slice's `count[1]`
                    smith_lang_word(smith_lang_scratch_str(before->text), before->text.length) == SmithLangWord_Other;
            }
            param_bracket = brackets == 0 && held->paren_depth == 1 && named;
            brackets += 1;
        } else if (function && smith_lang_punct(&now, "]") && brackets > 0) {
            brackets -= 1;
        }
        if (function && held->paren_depth == 1) {
            lang->decl.prev_ident = i > 0 && lang->head[i - 1].kind == SmithTokenKind_Identifier &&
                                    smith_lang_word(smith_lang_scratch_str(lang->head[i - 1].text),
                                                    lang->head[i - 1].text.length) == SmithLangWord_Other;
            smith_lang_array_token(lang, host, now.text, now.length, now.kind, (uint32_t)brackets_before,
                                   (uint32_t)brackets);
        }
        if (i == static_at) {
            SmithToken first = smith_lang_held_token(lang, &lang->head[i]);
            smith_lang_emit_code(lang, host, smith_lang_static_keyword(&first));
            lang->head[i].line_start = false;
            lang->head[i].space_before = true;
        }
        smith_lang_emit_held(lang, host, i, header_too);
    }
}

static SmithLangForm smith_lang_form(const SmithLang *lang) {
    if (lang->decl.is_typedef || lang->decl.declared == 0) return SmithLangForm_Type;
    return lang->decl.is_function ? SmithLangForm_Function : SmithLangForm_Variable;
}

// A declaration token of an exported variable: the header's, and, when the module is to see the declaration before the
// definition (as a C file that includes its own header does, so -Wmissing-variable-declarations is quiet), its own.
static void smith_lang_variable_token(SmithLang *lang, const SmithHost *host, SmithToken token, bool in_code) {
    if (in_code) {
        smith_lang_emit(lang, host, token);
    } else {
        smith_lang_emit_header(lang, token);
    }
}

// extern T name __asm__("sym"); in the header for an exported variable, and before its definition in the module (a
// variable declared `export extern` has no definition here: its one declaration is the module's too, and is not
// written twice).
static void smith_lang_declare_variable(SmithLang *lang, const SmithHost *host, bool labeled) {
    bool in_code = !lang->decl.is_extern;
    lang->emit_synthetic = in_code;
    SmithToken first = smith_lang_held_token(lang, &lang->head[0]);
    SmithToken keyword = smith_lang_synth(SmithTokenKind_Identifier, "extern", 6, &first);
    if (!lang->decl.is_extern) smith_lang_variable_token(lang, host, keyword, in_code);
    for (size_t i = 0; i < lang->head_count; i++) { // bounded by the head
        if (smith_lang_head_dropped(lang, i)) continue;
        SmithToken token = smith_lang_held_token(lang, &lang->head[i]);
        if (i == 0 && !lang->decl.is_extern) {
            token.line_start = false;
            token.space_before = true;
        }
        smith_lang_variable_token(lang, host, token, in_code);
    }
    SmithToken last = smith_lang_last_of_head(lang);
    if (!labeled) { // a chosen C name is the symbol: extern T name;
        smith_lang_variable_token(lang, host, smith_lang_punct_after(";", &last), in_code);
        lang->emit_synthetic = false;
        return;
    }
    char label[smith_lang_name_max * 2];
    int length = snprintf(label, sizeof label, "\"%s%s%s\"", smith_lang_symbol_underscore, lang->symbol,
                          smith_lang_scratch_str(lang->head[lang->decl.last_name].original));
    SmithToken pieces[5] = {smith_lang_synth(SmithTokenKind_Identifier, "__asm__", 7, &last),
                            smith_lang_punct_after("(", &last),
                            smith_lang_synth(SmithTokenKind_String, label, (size_t)length, &last),
                            smith_lang_punct_after(")", &last), smith_lang_punct_after(";", &last)};
    pieces[0].line_start = false;
    pieces[0].space_before = true;
    pieces[2].line_start = false;
    pieces[2].space_before = false;
    for (size_t i = 0; i < 5; i++)
        smith_lang_variable_token(lang, host, pieces[i], in_code); // the five pieces
    lang->emit_synthetic = false;
}

static void smith_lang_body_note(SmithLang *lang); // runtime/lang_body.c

// An exported `constexpr type Y = X;` is `typedef X Y;` in the header, so C callers see the name. What was written to
// the header after the `typedef` keyword is the initializer, as the lowering spelled it (`double;`, `uint8_t[16];`,
// `void (*)(int32_t);`): the name goes where C's declarator has it, inside the `(*` of a function pointer or a pointer
// to an array, before the `[` of an array, else after the type. An initializer that is no type (a call of a function
// over types) has no C spelling: nothing is written for it.
static bool smith_lang_alias_declarator(const char *init, size_t length, const char *name, char *out, size_t capacity) {
    size_t at = 0;
    size_t insert = length;
    bool after_star = false;
    int depth = 0;
    for (size_t i = 0; i < length; i++) { // an initializer is a type: its `(` is a declarator's or a parameter list's
        if (init[i] == '(') {
            size_t k = i + 1;
            while (k < length && init[k] == ' ')
                k++;
            size_t before = i;
            while (before > 0 && init[before - 1] == ' ')
                before--;
            bool declarator = k < length && init[k] == '*';
            bool params = before > 0 && init[before - 1] == ')';
            if (!declarator && !params && depth == 0) return false;
            if (declarator && !after_star && insert == length) {
                while (k < length && (init[k] == '*' || init[k] == ' '))
                    k++;
                if (k < length && init[k] == ')') {
                    insert = k;
                    after_star = true;
                }
            }
            depth += 1;
        } else if (init[i] == ')') {
            depth -= 1;
        } else if (init[i] == '[' && insert == length && depth == 0) {
            insert = i;
        }
    }
    at = insert;
    while (at > 0 && init[at - 1] == ' ')
        at--;
    bool glue = after_star || (at > 0 && init[at - 1] == '*');
    int written = snprintf(out, capacity, "%.*s%s%s%.*s", (int)at, init, glue ? "" : " ", name, (int)(length - insert),
                           init + insert);
    return written > 0 && (size_t)written < capacity;
}

static void smith_lang_alias_finish(SmithLang *lang) {
    char *body = smith_lang_header_body;
    size_t start = lang->decl.alias_mark;
    size_t end = lang->header_used;
    lang->decl.alias_header = false;
    if (start == 0 || end < start) return;
    while (start < end && body[start] == ' ')
        start++;
    while (end > start && (body[end - 1] == ';' || body[end - 1] == ' ' || body[end - 1] == '\n'))
        end--;
    char declarator[smith_lang_name_max * 2];
    size_t keyword = lang->decl.alias_mark - (sizeof "typedef" - 1);
    if (end > start && memchr(body + start, '/', end - start) == nullptr &&
        smith_lang_alias_declarator(body + start, end - start, lang->decl.alias_name, declarator, sizeof declarator)) {
        lang->header_used = lang->decl.alias_mark;
        lang->header_used +=
            (size_t)snprintf(body + lang->header_used, smith_lang_header_max - lang->header_used, " %s;", declarator);
        return;
    }
    lang->header_used = keyword; // no C spelling: the typedef is dropped
    while (lang->header_used > 0 && (body[lang->header_used - 1] == '\n' || body[lang->header_used - 1] == ' '))
        lang->header_used -= 1;
}

static void smith_lang_end_declaration(SmithLang *lang) {
    if (lang->decl.alias_header) smith_lang_alias_finish(lang);
    if (lang->due_late != 0 && lang->due_count < smith_lang_due_max) lang->due[lang->due_count++] = lang->due_late - 1;
    lang->due_late = 0;
    lang->local_count = 0;
    smith_lang_body_note(lang);
    if (lang->decl.c_name.length > 0 && !lang->decl.c_name_used) { // reported by the next token fed (it has the host)
        lang->unnamed_c_name = true;
        lang->unnamed_c_name_location = lang->decl.c_name_location;
    }
    // A comment after the export's last token follows it in the header.
    lang->tail_open = lang->export_active || lang->export_tail;
    lang->export_tail = false;
    lang->export_active = false;
    smith_lang_priv_end(lang);
    lang->state = SmithLangState_Start;
    lang->decl = (SmithLangDecl){};
    lang->pending_export = false;
    lang->init_top = false;
    lang->tail_declarator = false;
    lang->statement_typedef = false;
    lang->agg_pending = false;
    lang->head_count = 0;
    lang->scratch_used = 0;
    lang->pending = SIZE_MAX;
    lang->paren_depth = 0;
    lang->bracket_depth = 0;
}

static void smith_lang_push_brace(SmithLang *lang, const SmithHost *host, SmithLangBrace kind, uint32_t location) {
    if (lang->brace_depth >= smith_lang_braces_max) {
        smith_lang_error(lang, host, location, "braces nest more deeply than smith reads (%zu)", smith_lang_braces_max);
        lang->broken = true;
        return;
    }
    lang->braces[lang->brace_depth++] = kind;
}

// A function's `{`: its body needs the braces, the module's names and the imported ones.
static void smith_lang_enter_body(SmithLang *lang, const SmithHost *host, uint32_t location) {
    if (host->previous != nullptr) {
        if (lang->has_c && !lang->imports_watched) host->watch_imports(host->context);
        lang->imports_watched = true;
        lang->body_sparse = !lang->body_enabled && !host->maybe_local_tag; // the body file needs every token written
        host->watch_mode(host->context, host->maybe_if_constexpr || lang->body_enabled || host->maybe_local_tag ||
                                                smith_lang_type_on(lang, host) || smith_lang_tuples_on(lang, host)
                                            ? SmithWatch_All
                                            : SmithWatch_Braces);
    }
    smith_lang_push_brace(lang, host, SmithLangBrace_Function, location);
    lang->in_function = true;
    lang->export_active = lang->decl.inline_header;          // an exported inline function's body is in the header too
    if (lang->private_active) smith_lang_priv_abandon(lang); // a private function's body is kept by no one
    lang->decl.checking = false;
}

// After the head is written: the terminator that ended it, and where the declaration goes from there.
static void smith_lang_after_head(SmithLang *lang, const SmithHost *host, const SmithToken *terminator,
                                  SmithLangForm form, bool proto_written) {
    bool semicolon = smith_lang_punct(terminator, ";");
    if (proto_written && semicolon) {
        smith_lang_end_declaration(lang);
        return;
    }
    if (lang->decl.alias_header) {
        smith_lang_emit_code(lang, host, *terminator); // the header has the initializer only
    } else {
        smith_lang_emit(lang, host, *terminator);
    }
    lang->head_count = 0;
    lang->scratch_used = 0;
    lang->pending = SIZE_MAX;
    lang->paren_depth = 0;
    lang->bracket_depth = 0;
    if (semicolon) {
        smith_lang_end_declaration(lang);
        return;
    }
    lang->state = SmithLangState_Stream;
    lang->prev_char = terminator->text[0];
    lang->prev_member = false;
    lang->prev_tag = false;
    lang->typeish = false;
    lang->prev_name_length = 0;
    if (smith_lang_punct(terminator, "=")) {
        lang->init_top = true;
        return;
    }
    if (form == SmithLangForm_Function) {
        smith_lang_enter_body(lang, host, terminator->location);
        return;
    }
    smith_lang_push_brace(lang, host, lang->decl.last_tag_enum ? SmithLangBrace_Enum : SmithLangBrace_Aggregate,
                          terminator->location);
}

// An exported inline function is defined in the header, as `static inline`, and so in the module: it has no symbol
// (the includer's compiler inlines it, or makes its own copy), and its body goes to the header with the head.
static void smith_lang_emit_inline_head(SmithLang *lang, const SmithHost *host) {
    lang->decl.inline_header = true;
    // After the attributes the head begins with ([[maybe_unused]] static inline ...): one that follows a storage class
    // belongs to the type, and the compiler refuses it before `inline`.
    size_t at = smith_lang_head_after_attributes(lang);
    SmithToken first = smith_lang_held_token(lang, &lang->head[at]);
    for (size_t i = 0; i < at; i++) // the attributes
        smith_lang_emit_held(lang, host, i, true);
    smith_lang_emit(lang, host, smith_lang_static_keyword(&first));
    lang->head[at].line_start = false;
    lang->head[at].space_before = true;
    for (size_t i = at; i < lang->head_count; i++) // bounded by the head
        smith_lang_emit_held(lang, host, i, true);
}

// runtime/lang_generic.c
static void smith_lang_generic_flush_head(SmithLang *lang, const SmithHost *host, const SmithToken *terminator);

// ---- main with the io
// ----------------------------------------------------------------------------------------------------
//
// `i32 main(io::Io *io) { ... }` is written as `static int32_t smith_user_main(SmithIo *io) { ... }` and, at the end of
// the file, `int main(int argc, char **argv)` makes the io, calls it, closes the io and returns its status. The host
// of a file with no `module` line sees the head as it is read (runtime/lang_sparse.c); here the head is held whole.

// `i32` that begins `i32 main(io::Io *name)`: the declaration is the program's body, a static function.
static bool smith_lang_io_main_ahead(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!lang->io_import || !smith_lang_equals(token->text, token->length, "i32")) return false;
    if (!smith_lang_ident(smith_lang_peek(lang, host, 0, false), "main") ||
        !smith_lang_punct(smith_lang_peek(lang, host, 1, false), "(")) {
        return false;
    }
    const SmithToken *alias = smith_lang_peek(lang, host, 2, false);
    SmithLangBinding *binding =
        alias->kind == SmithTokenKind_Identifier ? smith_lang_binding_find(lang, alias->text, alias->length) : nullptr;
    return binding != nullptr && smith_lang_is_io_binding(binding) &&
           smith_lang_punct(smith_lang_peek(lang, host, 3, false), "::") &&
           smith_lang_ident(smith_lang_peek(lang, host, 4, false), "Io") &&
           smith_lang_punct(smith_lang_peek(lang, host, 5, false), "*") &&
           smith_lang_peek(lang, host, 6, false)->kind == SmithTokenKind_Identifier &&
           smith_lang_punct(smith_lang_peek(lang, host, 7, false), ")");
}

// The first token of a declaration: when it is `i32 main(io::Io *name)`, the `main` still queued is renamed (it then
// goes through whatever path the file takes as the name of an ordinary function, which is `static` where the file's
// private functions are) and the file's end writes the C `main`. Returns whether it was one.
static bool smith_lang_io_main_begin(SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    if (!smith_lang_io_main_ahead(lang, host, first)) return false;
    SmithToken *name = &lang->queue[lang->queue_head];
    name->text = smith_lang_io_main_name;
    name->length = sizeof smith_lang_io_main_name - 1;
    name->handle = smith_token_no_handle;
    lang->io_main = true;
    lang->io_main_location = name->location;
    // The debugger and the editor show it as the program wrote it.
    smith_lang_written_add(lang, smith_lang_io_main_name, sizeof smith_lang_io_main_name - 1, "main", 4, 0,
                           lang->module_text, true, "main", 4);
    return true;
}

// The C `main` that makes the io, runs the function the program wrote with it, closes it and returns its status.
static void smith_lang_emit_io_main(SmithLang *lang, const SmithHost *host) {
    static const char *const pieces[] = {"int",    "main",
                                         "(",      "int",
                                         "argc",   ",",
                                         "char",   "*",
                                         "*",      "argv",
                                         ")",      "{",
                                         "",       "io",
                                         "=",      "smith_io_make",
                                         "(",      "argc",
                                         ",",      "argv",
                                         ")",      ";",
                                         "int",    "status",
                                         "=",      smith_lang_io_main_name,
                                         "(",      "&",
                                         "io",     ")",
                                         ";",      "smith_io_close",
                                         "(",      "&",
                                         "io",     ")",
                                         ";",      "return",
                                         "status", ";",
                                         "}"};
    SmithToken like = {};
    like.location = lang->io_main_location;
    bool tight = true;                                              // after `(` and `&` the next token is not spaced
    for (size_t i = 0; i < sizeof pieces / sizeof pieces[0]; i++) { // 45 pieces
        const char *text = pieces[i];
        if (i == 12) text = lang->io_type.length > 0 ? smith_lang_str(lang->io_type) : "SmithIo";
        bool word = (text[0] >= 'a' && text[0] <= 'z') || (text[0] >= 'A' && text[0] <= 'Z') || text[0] == '_';
        SmithToken made =
            smith_lang_synth(word ? SmithTokenKind_Identifier : SmithTokenKind_Punctuator, text, strlen(text), &like);
        made.synthetic = true;
        made.line_start = i == 0;
        made.space_before = i != 0 && !tight && strchr("),;", text[0]) == nullptr;
        smith_lang_emit(lang, host, made);
        tight = text[0] == '(' || text[0] == '&';
    }
}

// The end of the file: the `main` of an io-form one, unless the file also made an io of its own.
static void smith_lang_finish_io_main(SmithLang *lang, const SmithHost *host) {
    if (!lang->io_main) return;
    if (lang->io_make_seen) {
        smith_lang_error(lang, host, lang->io_make_location,
                         "main already has the io: use `io`, the parameter of `main`, instead of `io::make`");
    }
    smith_lang_emit_io_main(lang, host);
}

// An exported function with a body: an importer may run it (runtime/lang_body.c).
static void smith_lang_body_listed_add(SmithLang *lang, bool is_main) {
    if (!lang->emit_synthetic || !lang->decl.exported || is_main ||
        lang->body_names_count >= smith_lang_body_names_max) {
        return;
    }
    const SmithLangHeld *held = &lang->head[lang->decl.last_name];
    lang->body_names[lang->body_names_count++] =
        smith_lang_store(lang, smith_lang_scratch_str(held->original), held->original.length);
}

// The head of an exported alias: the module's code has it as written; the header begins `typedef`, and its end
// (smith_lang_alias_finish) puts the name where the declarator has it.
static void smith_lang_alias_begin(SmithLang *lang, const SmithHost *host, const SmithToken *like) {
    (void)host;
    const SmithLangHeld *name = &lang->head[lang->decl.last_name];
    snprintf(lang->decl.alias_name, sizeof lang->decl.alias_name, "%.*s", (int)name->text.length,
             smith_lang_scratch_str(name->text));
    SmithToken keyword = smith_lang_synth(SmithTokenKind_Identifier, "typedef", 7, like);
    keyword.line_start = true;
    keyword.space_before = false;
    smith_lang_emit_header(lang, keyword);
    lang->decl.alias_mark = lang->header_used;
    lang->decl.alias_header = true;
}

// The head of a declaration that has no symbol of its own to declare: written to the module and its header, but an
// exported alias, which has a `typedef` in the header instead.
static void smith_lang_emit_plain_head(SmithLang *lang, const SmithHost *host, const SmithToken *terminator,
                                       bool with_static) {
    if (lang->decl.is_alias && lang->decl.exported && smith_lang_punct(terminator, "=")) {
        SmithToken like = smith_lang_last_of_head(lang);
        smith_lang_alias_begin(lang, host, &like);
        smith_lang_emit_head(lang, host, false, false);
        return;
    }
    smith_lang_emit_head(lang, host, true, with_static);
}

// The head is complete: the terminator says what follows. Writes the head, then the terminator, and picks the state.
static void smith_lang_flush_head(SmithLang *lang, const SmithHost *host, const SmithToken *terminator) {
    if (lang->decl.generic) { // recorded, not written
        smith_lang_generic_flush_head(lang, host, terminator);
        return;
    }
    SmithLangForm form = smith_lang_form(lang);
    bool is_main = false;
    if (form != SmithLangForm_Type) {
        const SmithLangHeld *name_held = &lang->head[lang->decl.last_name];
        is_main = smith_lang_equals(smith_lang_scratch_str(name_held->original), name_held->original.length, "main");
    }
    if (lang->decl.exported && lang->decl.declared > 1) {
        smith_lang_error(lang, host, lang->head[0].location, "an exported declaration declares one name; split it");
    }
    // A chosen C name is the symbol too, as C writes it: no label, so C and C++ define and call it by that name.
    bool has_c_name = lang->decl.c_name_used && lang->decl.c_name.length > 0;
    // A private function or variable that an exported generic's instance calls has a symbol too (lang_private.c): the
    // header pass keeps its prototype for the header, and the module's own compile reads its name from there.
    bool linked = false;
    if (lang->is_module && !lang->decl.exported && form != SmithLangForm_Type && !lang->decl.is_constexpr &&
        !lang->decl.is_extern && !is_main) {
        const SmithLangHeld *name_held = &lang->head[lang->decl.last_name];
        linked = lang->private_active ||
                 smith_lang_is_linked(lang, smith_lang_scratch_str(name_held->original), name_held->original.length);
    }
    lang->decl.linked = linked;
    smith_lang_priv_form(lang, (uint8_t)form, linked);
    bool symbol = (lang->decl.exported || linked) && form != SmithLangForm_Type && !lang->decl.is_constexpr && !is_main;
    bool private_static = lang->is_module && !lang->decl.exported && form != SmithLangForm_Type &&
                          !lang->decl.storage && !is_main && !linked;
    bool proto_written = false;
    SmithToken like = smith_lang_last_of_head(lang);
    if (symbol && lang->decl.exported && form == SmithLangForm_Function && lang->decl.is_inline &&
        smith_lang_punct(terminator, "{")) {
        smith_lang_emit_inline_head(lang, host);
    } else if (symbol && form == SmithLangForm_Function) {
        // With a body the head is written twice: the prototype that carries the symbol is a copy, and the definition
        // after it has the tokens of the source.
        lang->emit_synthetic = smith_lang_punct(terminator, "{");
        smith_lang_body_listed_add(lang, is_main);
        smith_lang_hoist_slice_structs(lang, host);
        smith_lang_emit_head(lang, host, true, false);
        if (!has_c_name) smith_lang_emit_label(lang, host, &like, true);
        smith_lang_emit(lang, host, smith_lang_punct_after(";", &like));
        lang->emit_synthetic = false;
        lang->export_active = false;
        smith_lang_priv_end(lang);
        proto_written = true;
        if (smith_lang_punct(terminator, "{")) {
            smith_lang_emit_head(lang, host, false, false);
            proto_written = false;
        } else {
            lang->export_tail = true;
            smith_lang_end_declaration(lang);
            return;
        }
    } else if (symbol && form == SmithLangForm_Variable) {
        smith_lang_declare_variable(lang, host, !has_c_name);
        lang->export_active = false;
        smith_lang_priv_end(lang);
        lang->export_tail = true;
        smith_lang_emit_head(lang, host, false, false);
        if (!has_c_name) smith_lang_emit_label(lang, host, &like, false);
    } else {
        smith_lang_emit_plain_head(lang, host, terminator, private_static);
    }
    smith_lang_after_head(lang, host, terminator, form, proto_written);
}

// ---- A token in the head
// ----------------------------------------------------------------------------------------------------

static bool smith_lang_enter_namespace(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                       SmithToken *result) {
    if (lang->bindings_count == 0 || lang->prev_member) return false;
    SmithLangBinding *binding = smith_lang_binding_find(lang, token->text, token->length);
    if (binding == nullptr) return false;
    return smith_lang_namespace(lang, host, token, binding, result);
}

static void smith_lang_head_identifier(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangContext context = smith_lang_context(lang, host, false);
    if (smith_lang_word(token->text, token->length) != SmithLangWord_Other) {
        smith_lang_note_keyword(lang, host, token);
        smith_lang_hold(lang, token, SmithLangRole_Plain, &context);
        smith_lang_track(lang, token, false);
        return;
    }
    SmithToken resolved;
    if (smith_lang_enter_namespace(lang, host, token, &resolved)) {
        smith_lang_hold(lang, &resolved, SmithLangRole_Done, &context);
    } else {
        smith_lang_hold(lang, token, SmithLangRole_Plain, &context);
    }
    lang->pending = lang->head_count - 1;
}

static void smith_lang_head(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    smith_lang_resolve(lang, host, token);
    if (token->kind == SmithTokenKind_Identifier) {
        smith_lang_head_identifier(lang, host, token);
        return;
    }
    if (token->kind == SmithTokenKind_Punctuator && smith_lang_head_punct(lang, token)) {
        smith_lang_flush_head(lang, host, token);
        return;
    }
    SmithLangContext context = smith_lang_context(lang, host, false);
    smith_lang_hold(lang, token, SmithLangRole_Plain, &context);
    smith_lang_track(lang, token, false);
    if (smith_lang_punct(token, "*") || smith_lang_punct(token, "(") || smith_lang_punct(token, ")")) return;
}
