// What names mean: the module's table, the import bindings, and the lowering of one identifier. Included by
// runtime/lang.c, after runtime/lang_out.c.

// What came before a token, as far as lowering it is concerned.
typedef struct {
    bool after_member; // after . or ->: a field, never a name in scope
    bool after_tag;    // after struct, union or enum
    bool typeish;      // the tokens before end a type, so a name now is a declarator
    bool lazy;         // typeish is not known yet: ask when it matters (it can take a lookup)
    char before;       // the first character of the token before, if it was a punctuator
} SmithLangContext;

typedef struct {
    bool replaced;
    bool is_type;
    bool expand; // the text is a macro of the compiler's (__UINT32_TYPE__): let it expand
    const char *text;
    size_t length;
    const char *header;
    size_t header_length;
} SmithLangLowered;

static uint32_t smith_lang_hash(const char *text, size_t length) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < length; i++) { // bounded by the name
        hash ^= (uint8_t)text[i];
        hash *= 16777619U;
    }
    return hash;
}

static SmithLangName *smith_lang_name_find(SmithLang *lang, const char *text, size_t length) {
    uint32_t slot = smith_lang_hash(text, length) & (smith_lang_slots - 1);
    for (size_t probe = 0; probe < smith_lang_slots; probe++) { // the table is half empty at most
        uint32_t entry = lang->slots[slot];
        if (entry == 0) return nullptr;
        SmithLangName *name = &lang->names[entry - 1];
        if (name->name.length == length && memcmp(smith_lang_str(name->name), text, length) == 0) return name;
        slot = (slot + 1) & (smith_lang_slots - 1);
    }
    return nullptr;
}

static SmithLangName *smith_lang_name_add(SmithLang *lang, const char *text, size_t length, SmithLangNameKind kind,
                                          bool exported) {
    if (lang->names_count >= smith_lang_names_max) {
        lang->broken = true;
        return nullptr;
    }
    SmithLangName *name = &lang->names[lang->names_count++];
    name->name = smith_lang_store(lang, text, length);
    name->kind = kind;
    name->exported = exported;
    uint32_t slot = smith_lang_hash(text, length) & (smith_lang_slots - 1);
    for (size_t probe = 0; probe < smith_lang_slots; probe++) { // the table is half empty at most
        if (lang->slots[slot] == 0) break;
        slot = (slot + 1) & (smith_lang_slots - 1);
    }
    lang->slots[slot] = (uint32_t)lang->names_count;
    return name;
}

static SmithLangBinding *smith_lang_binding_find(SmithLang *lang, const char *text, size_t length) {
    for (size_t i = 0; i < lang->bindings_count; i++) { // bounded by the table
        SmithLangBinding *binding = &lang->bindings[i];
        if (binding->name.length == length && memcmp(smith_lang_str(binding->name), text, length) == 0) return binding;
    }
    return nullptr;
}

static bool smith_lang_follows_declarator(const SmithToken *next) {
    if (next->kind == SmithTokenKind_Eof) return true;
    if (next->kind != SmithTokenKind_Punctuator) return false;
    static const char *const punctuators[] = {"=", ";", ",", "[", ")", ":"};
    for (size_t i = 0; i < sizeof punctuators / sizeof punctuators[0]; i++) { // 6 entries
        if (smith_lang_equals(next->text, next->length, punctuators[i])) return true;
    }
    return false;
}

// Whether the tokens before end a type. A name just before may be a type only the parser knows.
static bool smith_lang_typeish(SmithLang *lang, const SmithHost *host) {
    if (lang->typeish) return true;
    if (lang->prev_name_length == 0) return false;
    if (lang->is_module) {
        SmithLangName *name = smith_lang_name_find(lang, lang->prev_name, lang->prev_name_length);
        if (name != nullptr) return name->kind == SmithLangName_Type || name->kind == SmithLangName_Tag;
    }
    return host->lookup(host->context, lang->prev_name, (uint32_t)lang->prev_name_length, SmithLookup_Type).kind ==
           SmithLookup_Type;
}

static SmithLangContext smith_lang_context(SmithLang *lang, const SmithHost *host, bool lazy) {
    SmithLangContext context = {
        .after_member = lang->prev_member,
        .after_tag = lang->prev_tag,
        .typeish = !lazy && smith_lang_typeish(lang, host),
        .lazy = lazy,
        .before = lang->prev_char,
    };
    return context;
}

static bool smith_lang_context_typeish(SmithLang *lang, const SmithHost *host, const SmithLangContext *context) {
    return context->lazy ? smith_lang_typeish(lang, host) : context->typeish;
}

// Remembers what a token was, for the one after it.
static void smith_lang_track(SmithLang *lang, const SmithToken *token, bool is_type) {
    lang->prev_member = false;
    lang->prev_tag = false;
    lang->prev_char = token->kind == SmithTokenKind_Punctuator && token->length > 0 ? token->text[0] : 0;
    if (token->kind == SmithTokenKind_Identifier) {
        SmithLangWord word = smith_lang_word(token->text, token->length);
        if (word == SmithLangWord_Qualifier || word == SmithLangWord_Storage) return;
        lang->prev_name_length = 0;
        lang->typeish = is_type || word == SmithLangWord_TypeKeyword;
        lang->prev_tag = word == SmithLangWord_Tag;
        if (word == SmithLangWord_Other && !is_type && token->length < sizeof lang->prev_name) {
            memcpy(lang->prev_name, token->text, token->length);
            lang->prev_name_length = token->length;
        }
        return;
    }
    if (smith_lang_punct(token, "*")) return;
    if (smith_lang_punct(token, "[") && lang->prev_char == '[') lang->in_attribute = true;
    if (smith_lang_punct(token, "]") && lang->prev_char == ']') lang->in_attribute = false;
    lang->prev_member = smith_lang_punct(token, ".") || smith_lang_punct(token, "->");
    lang->typeish = false;
    lang->prev_name_length = 0;
}

// ---- Declared names -------------------------------------------------------------------------------------------------

// A name the module introduces at file scope: the table learns it, and the declaration gets the prefixed one.
// Returns null when the name is kept as written (main).
static const char *smith_lang_declare(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                      uint32_t location, SmithLangNameKind kind, bool exported) {
    (void)location;
    if (kind == SmithLangName_Function && smith_lang_equals(name, length, "main")) return nullptr;
    SmithLangName *known = smith_lang_name_find(lang, name, length);
    if (known == nullptr) {
        smith_lang_name_add(lang, name, length, kind, exported);
        if (host->watch != nullptr) host->watch(host->context, name, (uint32_t)length);
        if (host->rename != nullptr) {
            const char *prefixed = smith_lang_prefixed(lang, name, length);
            host->rename(host->context, name, (uint32_t)length, prefixed, (uint32_t)lang->tmp_length);
        }
    }
    if (known != nullptr && exported && !known->exported) known->exported = true;
    return smith_lang_prefixed(lang, name, length);
}

// Whether `module`, `import` or `export` is the file's own name for something, which the parser or a typedef says.
static bool smith_lang_is_own_word(SmithLang *lang, const SmithHost *host, const SmithToken *word) {
    uint8_t bit = smith_lang_ident(word, "module") ? 1 : smith_lang_ident(word, "import") ? 2 : 4;
    return (lang->own_words & bit) != 0 ||
           host->lookup(host->context, word->text, word->length, SmithLookup_Type).kind != SmithLookup_None;
}

// A typedef of one of those words (`typedef int import;`) makes it the file's own for good.
static void smith_lang_note_own_word(SmithLang *lang, const SmithToken *word) {
    if (smith_lang_ident(word, "module")) lang->own_words |= 1;
    if (smith_lang_ident(word, "import")) lang->own_words |= 2;
    if (smith_lang_ident(word, "export")) lang->own_words |= 4;
}

// ---- Namespace accesses: rp::open and c::sinf
// -------------------------------------------------------------------------

static SmithToken smith_lang_made(const SmithToken *like, const char *text, size_t length, bool expand) {
    SmithToken made = smith_lang_synth(SmithTokenKind_Identifier, text, length, like);
    made.expand = expand;
    return made;
}

static void smith_lang_allow(SmithLang *lang, uint32_t location) {
    lang->allowed[lang->allowed_next++ % smith_lang_allowed_max] = location; // a ring: a macro expands at once
}

// `name::member` where name is an import binding. The member is read unexpanded: for `c::X` it is a macro to expand
// then, and for a module it is part of a name, so it must not be expanded at all. Anything else with that name (a
// variable called io, say) is just a name, and so is `gnu` in [[gnu::unused]].
static bool smith_lang_namespace(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                 const SmithLangBinding *binding, SmithToken *result) {
    if (lang->in_attribute) return false;
    const SmithToken *colons = smith_lang_peek(lang, host, 0, false);
    if (!smith_lang_punct(colons, "::")) return false;
    const SmithToken *member = smith_lang_peek(lang, host, 1, true);
    if (member->kind != SmithTokenKind_Identifier || member->foreign) {
        smith_lang_error(lang, host, name->location, "`%s::` must be followed by a name",
                         smith_lang_str(binding->name));
        return false;
    }
    smith_lang_take(lang, host);
    SmithToken taken = smith_lang_take(lang, host);
    if (binding->is_c) {
        smith_lang_allow(lang, taken.location);
        *result = smith_lang_made(name, taken.text, taken.length, true);
        result->location = taken.location;
    } else {
        const char *prefix = smith_lang_str(binding->prefix);
        size_t prefix_length = binding->prefix.length;
        size_t total = prefix_length + taken.length;
        if (total + 1 > sizeof lang->tmp) {
            lang->broken = true;
            return false;
        }
        memcpy(lang->tmp, prefix, prefix_length);
        memcpy(lang->tmp + prefix_length, taken.text, taken.length);
        lang->tmp[total] = 0;
        *result = smith_lang_made(name, lang->tmp, total, false);
        result->location = taken.location;
    }
    return true;
}

// ---- One identifier
// --------------------------------------------------------------------------------------------------

static SmithLangLowered smith_lang_keep(void) {
    SmithLangLowered kept = {0};
    return kept;
}

static SmithLangLowered smith_lang_prefix_name(SmithLang *lang, const char *name, size_t length, bool is_type) {
    SmithLangLowered lowered = {0};
    lowered.replaced = true;
    lowered.is_type = is_type;
    lowered.text = smith_lang_prefixed(lang, name, length);
    lowered.length = lang->tmp_length;
    return lowered;
}

// The token after the one being lowered: given by the caller when it has it, else read (and the lowering holds it).
static const SmithToken *smith_lang_after(SmithLang *lang, const SmithHost *host, const SmithToken **next) {
    if (*next == nullptr) *next = smith_lang_peek(lang, host, 0, false);
    return *next;
}

static SmithLangLowered smith_lang_lower_sized(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                               const SmithLangContext *context, const SmithToken **next) {
    const SmithLangSized *sized = smith_lang_find_sized(name, length);
    if (sized == nullptr || (lang->statement_typedef && lang->brace_depth == lang->typedef_depth)) {
        return smith_lang_keep();
    }
    if (smith_lang_follows_declarator(smith_lang_after(lang, host, next)) &&
        smith_lang_context_typeish(lang, host, context)) {
        return smith_lang_keep();
    }
    if (host->lookup(host->context, name, (uint32_t)length, SmithLookup_Type).kind != SmithLookup_None) {
        return smith_lang_keep(); // the file has its own u32
    }
    SmithLangLowered lowered = {0};
    lowered.replaced = true;
    lowered.is_type = true;
    lowered.expand = true;
    lowered.text = sized->standard;
    lowered.length = strlen(sized->standard);
    lowered.header = sized->header;
    lowered.header_length = strlen(sized->header);
    return lowered;
}

// A tag that an imported header declared, used without `c::` (in every file, a module or not).
static void smith_lang_check_c_tag(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                   uint32_t location) {
    if (!lang->has_c) return;
    SmithLookup found = host->lookup(host->context, name, (uint32_t)length, SmithLookup_Tag);
    if (found.kind != SmithLookup_None && found.from_import && !found.in_main_file) {
        smith_lang_error(lang, host, location, "`struct %.*s` names a C tag; write `struct c::%.*s`", (int)length, name,
                         (int)length, name);
    }
}

// The name after struct, union or enum. A tag the module defines or declares (`struct S {` or `struct S;`) is its own;
// one it only uses is the module's if it was defined above, else a C header's (and in a module that header must say so
// with `struct c.S`).
static SmithLangLowered smith_lang_lower_tag(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                             uint32_t location, const SmithToken **next) {
    SmithLangLowered kept = smith_lang_keep();
    kept.is_type = true;
    if (!lang->is_module) { // a file with no `module` line has no tags of its own to lower, only a C tag to refuse
        smith_lang_check_c_tag(lang, host, name, length, location);
        return kept;
    }
    SmithLangName *known = smith_lang_name_find(lang, name, length);
    if (known != nullptr) return smith_lang_prefix_name(lang, name, length, true);
    const SmithToken *after = smith_lang_after(lang, host, next);
    bool defines = smith_lang_punct(after, "{") || smith_lang_punct(after, ";") || smith_lang_punct(after, ":");
    if (!lang->in_function && defines) {
        const char *prefixed =
            smith_lang_declare(lang, host, name, length, location, SmithLangName_Tag, lang->decl.exported);
        return prefixed != nullptr ? smith_lang_prefix_name(lang, name, length, true) : kept;
    }
    smith_lang_check_c_tag(lang, host, name, length, location);
    return kept;
}

static bool smith_lang_is_member_name(SmithLang *lang, const SmithHost *host, const SmithLangContext *context,
                                      const SmithToken **after) {
    if (lang->brace_depth == 0 || lang->braces[lang->brace_depth - 1] != SmithLangBrace_Aggregate) return false;
    const SmithToken *next = smith_lang_after(lang, host, after);
    if (lang->paren_depth == 0) {
        return next->kind == SmithTokenKind_Punctuator && (smith_lang_punct(next, ";") || smith_lang_punct(next, ",") ||
                                                           smith_lang_punct(next, "[") || smith_lang_punct(next, ":"));
    }
    return context->before == '*' && smith_lang_punct(next, ")");
}

// A name used in a module that a C header declared, with no `c.`.
static void smith_lang_check_c_name(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                    uint32_t location, bool *is_type) {
    if (length >= 2 && name[0] == '_' && name[1] == '_') return;
    SmithLookup found = host->lookup(host->context, name, (uint32_t)length, SmithLookup_Value);
    *is_type = found.kind == SmithLookup_Type;
    if (found.kind == SmithLookup_None || !found.from_import || found.in_main_file) return;
    smith_lang_error(lang, host, location, "`%.*s` is a C name; write `c::%.*s`", (int)length, name, (int)length, name);
}

// What a name that nothing special applies to becomes. `next` is the token after it.
static SmithLangLowered smith_lang_lower(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                         uint32_t location, const SmithLangContext *context, const SmithToken *next) {
    if (smith_lang_word(name, length) != SmithLangWord_Other || context->after_member) return smith_lang_keep();
    if (context->after_tag) return smith_lang_lower_tag(lang, host, name, length, location, &next);
    SmithLangLowered sized = smith_lang_lower_sized(lang, host, name, length, context, &next);
    if (sized.replaced) return sized;
    if (smith_lang_is_member_name(lang, host, context, &next)) return smith_lang_keep();
    SmithLangName *known = lang->is_module ? smith_lang_name_find(lang, name, length) : nullptr;
    if (known != nullptr) {
        if (lang->decl.checking && !known->exported &&
            (known->kind == SmithLangName_Type || known->kind == SmithLangName_Tag)) {
            smith_lang_error(lang, host, location, "an exported declaration names `%.*s`, which is private; export it",
                             (int)length, name);
        }
        return smith_lang_prefix_name(lang, name, length,
                                      known->kind == SmithLangName_Type || known->kind == SmithLangName_Tag);
    }
    SmithLangLowered kept = smith_lang_keep();
    if (!lang->has_c) return kept;
    bool declarator = smith_lang_follows_declarator(smith_lang_after(lang, host, &next)) &&
                      smith_lang_context_typeish(lang, host, context);
    if (!declarator) smith_lang_check_c_name(lang, host, name, length, location, &kept.is_type);
    return kept;
}
