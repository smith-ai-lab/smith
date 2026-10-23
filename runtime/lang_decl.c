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

// runtime/lang_sparse.c
static bool smith_lang_sparse_in_attribute(SmithLang *lang, const SmithHost *host);

// runtime/lang_names.c
static void smith_lang_written_add(SmithLang *lang, const char *lowered, size_t lowered_length, const char *written,
                                   size_t written_length, size_t prefix_length, SmithLangText module, bool direct,
                                   const char *member, size_t member_length);
static const SmithLangCName *smith_lang_cname_find(const SmithLang *lang, const char *fallback, size_t length);
static void smith_lang_written_access(SmithLang *lang, const SmithLangBinding *binding, size_t lowered_length,
                                      const char *member, size_t member_length);
static void smith_lang_written_read(SmithLang *lang, const SmithHost *host, const char *header, const char *module,
                                    const char *bind, size_t bind_length);
static void smith_lang_note_qualifier(SmithLang *lang, const SmithLangBinding *binding, const SmithToken *cursor);

// runtime/lang_generic.c
static bool smith_lang_is_type_param(const SmithLang *lang, const char *name, size_t length);
static bool smith_lang_generic_call(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                    const SmithLangBinding *binding, const SmithToken *member);
static bool smith_lang_generic_call_here(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                         const SmithLangName *known);
static void smith_lang_generic_private_name(SmithLang *lang, const SmithHost *host, SmithLangName *known,
                                            const char *name, size_t length, uint32_t location);

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

// ---- Parameters
// A parameter is not the export that has its name: `void step(Step *step)` keeps `step` in the header, and the body of
// the function reads it as the parameter (C's own scope), so the parameter is kept as written wherever it is used.

static bool smith_lang_is_param(const SmithLang *lang, const char *name, size_t length) {
    for (uint32_t i = 0; i < lang->params_count; i++) { // at most 16
        if (strlen(lang->params[i]) == length && memcmp(lang->params[i], name, length) == 0) return true;
    }
    return false;
}

// A name of the module in a function's parameter list, outside brackets (`char a[N]` uses N), that is no type: the
// parameter's own name (a function pointer's, `int (*width)(Box *box)`, too). Remembered, so that the body keeps it.
static bool smith_lang_param_here(SmithLang *lang, const SmithLangName *known, const char *name, size_t length) {
    if (!lang->decl.in_params || lang->paren_depth == 0 || lang->bracket_depth != 0) return false;
    if (known->kind == SmithLangName_Type || known->kind == SmithLangName_Tag) return false;
    if (length < smith_lang_param_max && lang->params_count < smith_lang_params_max &&
        !smith_lang_is_param(lang, name, length)) {
        memcpy(lang->params[lang->params_count], name, length);
        lang->params[lang->params_count][length] = 0;
        lang->params_count += 1;
    }
    return true;
}

// Whether the binding is the import of smith::io, under whatever name the file gave it.
static bool smith_lang_is_io_binding(const SmithLangBinding *binding) {
    return !binding->is_c && smith_lang_equals(smith_lang_str(binding->module), binding->module.length, "smith::io");
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

// Whether the tokens being read are an attribute's: `[[gnu::format(printf, 1, 2)]]` or `__attribute__((format(...)))`.
// What stands there is the attribute's own argument (a format kind, a name), never a use of a C name or a module.
static bool smith_lang_in_attribute(const SmithLang *lang) {
    return lang->in_attribute || lang->attribute_parens > 0;
}

// The parentheses of `__attribute__((`, followed through the tokens that are tracked.
static void smith_lang_track_attribute(SmithLang *lang, const SmithToken *token) {
    if (token->kind == SmithTokenKind_Identifier) {
        lang->attribute_next = smith_lang_ident(token, "__attribute__") || smith_lang_ident(token, "__attribute");
        return;
    }
    bool open = smith_lang_punct(token, "(");
    if (open && lang->attribute_parens > 0) lang->attribute_parens += 1;
    if (open && lang->attribute_parens == 0 && lang->attribute_next) lang->attribute_parens = 1;
    if (smith_lang_punct(token, ")") && lang->attribute_parens > 0) lang->attribute_parens -= 1;
    lang->attribute_next = false;
}

// Remembers what a token was, for the one after it.
static void smith_lang_track(SmithLang *lang, const SmithToken *token, bool is_type) {
    smith_lang_track_attribute(lang, token);
    lang->prev_member = false;
    lang->prev_tag = false;
    char before = lang->prev_char; // the token before this one
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
    if (smith_lang_punct(token, "[") && before == '[') lang->in_attribute = true;
    if (smith_lang_punct(token, "]") && before == ']') lang->in_attribute = false;
    lang->prev_member = smith_lang_punct(token, ".") || smith_lang_punct(token, "->");
    lang->typeish = false;
    lang->prev_name_length = 0;
}

// ---- Declared names -------------------------------------------------------------------------------------------------

// The name the prefix and `name` make, as C sees it when no C name was chosen for it: compared with a chosen one.
static bool smith_lang_default_name_is(const SmithLang *lang, const SmithLangName *entry, const char *c_name,
                                       size_t length) {
    size_t prefix = strlen(lang->prefix);
    const char *text = smith_lang_str(entry->name);
    return entry->c_name.length == 0 && length == prefix + entry->name.length &&
           memcmp(c_name, lang->prefix, prefix) == 0 && memcmp(c_name + prefix, text, entry->name.length) == 0;
}

// A C name chosen with [[smith::c_name]] must be the C name of nothing else the module declares, private names
// included (they are prefixed and `static`, but the compiler sees one set of names). Returns false, with the error
// reported, when it is.
static bool smith_lang_c_name_free(SmithLang *lang, const SmithHost *host, const char *c_name, size_t length,
                                   const char *name, size_t name_length, uint32_t location) {
    for (size_t i = 0; i < lang->names_count; i++) { // the names declared so far
        const SmithLangName *other = &lang->names[i];
        bool same_chosen = other->c_name.length == length && memcmp(smith_lang_str(other->c_name), c_name, length) == 0;
        if (same_chosen) {
            smith_lang_error(lang, host, location,
                             "[[smith::c_name]] on `%.*s` chooses the C name of `%s`; give one of them another name",
                             (int)name_length, name, smith_lang_str(other->name));
            return false;
        }
        if (smith_lang_default_name_is(lang, other, c_name, length)) {
            smith_lang_error(lang, host, location,
                             "[[smith::c_name]] on `%.*s` chooses the C name the module's prefix gives `%s`; give one "
                             "of them another name",
                             (int)name_length, name, smith_lang_str(other->name));
            return false;
        }
    }
    return true;
}

// The reverse: a name declared after a chosen C name, which the prefix would make the same.
static void smith_lang_c_name_taken(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                    uint32_t location) {
    const char *prefixed = smith_lang_prefixed(lang, name, length);
    size_t prefixed_length = lang->tmp_length;
    for (size_t i = 0; i < lang->names_count; i++) { // the names declared so far
        const SmithLangName *other = &lang->names[i];
        if (other->c_name.length == prefixed_length &&
            memcmp(smith_lang_str(other->c_name), prefixed, prefixed_length) == 0) {
            smith_lang_error(lang, host, location,
                             "`%.*s` gets the C name that [[smith::c_name]] chose for `%s`; give one of them another "
                             "name",
                             (int)length, name, smith_lang_str(other->name));
            return;
        }
    }
}

// The C name a new export takes from [[smith::c_name]]: the declaration's first name, but no enumeration constant.
static bool smith_lang_takes_c_name(const SmithLang *lang, SmithLangNameKind kind, bool exported) {
    return exported && kind != SmithLangName_Constant && lang->decl.c_name.length > 0 && !lang->decl.c_name_used;
}

// The C name of an exported enum's constant: the enum's C name (the one [[smith::c_name]] chose for it or its tag's),
// an underscore and the constant, `SmithTestKind_Equal`, which is what C's naming rule for enumeration constants asks.
// False when the enum has no C name yet (an anonymous typedef without [[smith::c_name]]): the constant is prefixed.
static bool smith_lang_enum_constant_name(const SmithLang *lang, SmithLangNameKind kind, bool exported,
                                          const char *name, size_t length, char *out, size_t capacity) {
    if (kind != SmithLangName_Constant || !exported) return false;
    SmithLangText prefix = lang->decl.enum_prefix;
    if (prefix.length == 0 && !lang->decl.c_name_used) prefix = lang->decl.c_name;
    if (prefix.length == 0 || prefix.length + length + 2 > capacity) return false;
    snprintf(out, capacity, "%s_%.*s", smith_lang_str(prefix), (int)length, name);
    return true;
}

static void smith_lang_declare_new(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                   uint32_t location, SmithLangNameKind kind, bool exported) {
    bool chosen = smith_lang_takes_c_name(lang, kind, exported);
    char c_name[smith_lang_name_max];
    size_t c_length = chosen ? lang->decl.c_name.length : 0;
    if (chosen) {
        memcpy(c_name, smith_lang_str(lang->decl.c_name), c_length + 1);
        lang->decl.c_name_used = true;
        if (!smith_lang_c_name_free(lang, host, c_name, c_length, name, length, lang->decl.c_name_location))
            chosen = false;
    } else if (smith_lang_enum_constant_name(lang, kind, exported, name, length, c_name, sizeof c_name)) {
        c_length = strlen(c_name);
        chosen = smith_lang_c_name_free(lang, host, c_name, c_length, name, length, location);
    } else if (lang->c_name_count > 0) {
        smith_lang_c_name_taken(lang, host, name, length, location);
    }
    SmithLangName *added = smith_lang_name_add(lang, name, length, kind, exported);
    if (added != nullptr) {
        added->location = location;
        if (chosen) {
            added->c_name = smith_lang_store(lang, c_name, c_length);
            lang->c_name_count += 1;
        }
    }
    const char *lowered = smith_lang_prefixed(lang, name, length);
    if (kind == SmithLangName_Tag && exported && lang->decl.last_tag_enum) { // its constants follow this C name
        lang->decl.enum_prefix = smith_lang_store(lang, lowered, lang->tmp_length);
    }
    smith_lang_written_add(lang, lowered, lang->tmp_length, name, length, chosen ? 0 : strlen(lang->prefix),
                           lang->module_text, true, chosen ? name : nullptr, chosen ? length : 0);
    if (host->watch != nullptr) host->watch(host->context, name, (uint32_t)length);
    if (host->rename != nullptr && !lang->decl.generic) { // a generic is not a name to rewrite where it stands
        const char *prefixed = smith_lang_prefixed(lang, name, length);
        host->rename(host->context, name, (uint32_t)length, prefixed, (uint32_t)lang->tmp_length);
    }
}

// A name the module introduces at file scope: the table learns it, and the declaration gets the prefixed one (or the
// C name chosen for it). Returns null when the name is kept as written (main, and the C symbol an `extern` declares).
static const char *smith_lang_declare(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                      uint32_t location, SmithLangNameKind kind, bool exported) {
    if (kind == SmithLangName_Function &&
        (smith_lang_equals(name, length, "main") || smith_lang_equals(name, length, smith_lang_io_main_name))) {
        return nullptr;
    }
    // `extern T name;` without `export` is a symbol that C defines: it keeps its C name, in the module as in C.
    bool symbol = kind == SmithLangName_Function || kind == SmithLangName_Value;
    if (symbol && !exported && lang->decl.is_extern && !lang->decl.is_typedef) return nullptr;
    // The build file's entry point is the symbol `build`, which smith's build program calls: in `module build;` the
    // exported function `build` keeps that C name with no attribute.
    if (kind == SmithLangName_Function && exported && lang->is_module && lang->decl.c_name.length == 0 &&
        strcmp(lang->module, "build") == 0 && smith_lang_equals(name, length, "build")) {
        lang->decl.c_name = smith_lang_store(lang, "build", 5);
        lang->decl.c_name_location = location;
    }
    SmithLangName *known = smith_lang_name_find(lang, name, length);
    if (known == nullptr) {
        smith_lang_declare_new(lang, host, name, length, location, kind, exported);
    } else if (smith_lang_takes_c_name(lang, kind, exported) && known->c_name.length == 0) {
        lang->decl.c_name_used = true;
        smith_lang_error(lang, host, lang->decl.c_name_location,
                         "`%.*s` was declared before; [[smith::c_name]] goes on its first declaration", (int)length,
                         name);
    }
    if (known != nullptr && exported && !known->exported) known->exported = true;
    const char *prefixed = smith_lang_prefixed(lang, name, length);
    if (lang->private_active) smith_lang_priv_name(lang, name, length, prefixed, lang->tmp_length);
    return prefixed;
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

// runtime/lang_generic.c: the body of an exported generic is being recorded
static bool smith_lang_recording_exported(const SmithLang *lang);

// runtime/lang_target.c
static void smith_lang_target_use(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                  const SmithToken *member, SmithToken *result, char *digits, size_t capacity);

// runtime/lang_names.c: the export of an import (or a name written through one) with this C name, if the table has it
static SmithLangWritten *smith_lang_written_lookup(const SmithLang *lang, const char *text, size_t length);

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

// `x::name` where the module's header lists its exports and `name` is not among them (`lang->tmp` holds the C name the
// access makes): a typo, a private name, or a header from before the module had it. The compiler reports the name as
// undeclared (with its own suggestion, if it has one); this is the note that follows that error, about the header.
static void smith_lang_check_listed(SmithLang *lang, const SmithHost *host, const SmithLangBinding *binding,
                                    const SmithToken *taken, size_t total) {
    if (!binding->listed || host->hint == nullptr || smith_lang_written_lookup(lang, lang->tmp, total) != nullptr) {
        return;
    }
    char message[smith_lang_message_max];
    snprintf(message, sizeof message,
             "the header of module `%s` lists no export `%.*s`; if the module has one, the header is stale: `smith "
             "build` writes it again",
             smith_lang_str(binding->module), (int)taken->length, taken->text);
    host->hint(host->context, taken->location, message);
}

// `io::make` and `io::Io` as the file writes them: the first make is refused in a file whose main has the io, and the
// type is what the `main` written at the end of that file names.
static void smith_lang_note_io_use(SmithLang *lang, const SmithToken *member, size_t total) {
    if (!lang->io_make_seen && smith_lang_equals(member->text, member->length, "make")) {
        lang->io_make_seen = true;
        lang->io_make_location = member->location;
    }
    if (lang->io_type.length == 0 && smith_lang_equals(member->text, member->length, "Io")) {
        lang->io_type = smith_lang_store(lang, lang->tmp, total);
    }
}

// `name::member` where name is an import binding. The member is read unexpanded: for `c::X` it is a macro to expand
// then, and for a module it is part of a name, so it must not be expanded at all. Anything else with that name (a
// variable called io, say) is just a name, and so is `gnu` in [[gnu::unused]].
static bool smith_lang_namespace(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                 const SmithLangBinding *binding, SmithToken *result) {
    if (smith_lang_in_attribute(lang)) return false;
    const SmithToken *colons = smith_lang_peek(lang, host, 0, false);
    if (!smith_lang_punct(colons, "::")) return false;
    const SmithToken *member = smith_lang_peek(lang, host, 1, true);
    if (member->completion) { // `x::` and the cursor: the name to complete is a member of x
        smith_lang_take(lang, host);
        SmithToken cursor = smith_lang_take(lang, host);
        smith_lang_note_qualifier(lang, binding, &cursor);
        *result = cursor; // it goes to the parser as it is: that is where the completion happens
        return true;
    }
    if (binding->is_c && member->foreign && member->kind != SmithTokenKind_Eof) {
        // `c::SIZE_MAX` inside the argument of a macro (`c::assert(n < c::SIZE_MAX)`): the preprocessor expanded
        // SIZE_MAX with the argument, before this name was reached, so what follows `c::` is already its expansion. The
        // `c::` has done its work; the expansion goes on as it is (the hook allowed the macro, see macroUsed).
        smith_lang_take(lang, host);
        *result = smith_lang_take(lang, host);
        return true;
    }
    if (member->kind != SmithTokenKind_Identifier || member->foreign) {
        smith_lang_error(lang, host, name->location, "`%s::` must be followed by a name",
                         smith_lang_str(binding->name));
        return false;
    }
    smith_lang_take(lang, host);
    SmithToken taken = smith_lang_take(lang, host);
    if (binding->is_target) { // a fact of the target (runtime/lang_target.c): one token where the access stood
        static char digits[24];
        smith_lang_target_use(lang, host, name, &taken, result, digits, sizeof digits);
        return true;
    }
    if (binding->missing && host->suppress != nullptr) host->suppress(host->context, taken.location);
    if (!binding->is_c && smith_lang_recording_exported(lang)) { // a call to a generic, say, which has no `via` token
        lang->bindings[binding - lang->bindings].in_header = true;
    }
    if (binding->is_c) {
        smith_lang_allow(lang, taken.location);
        *result = smith_lang_made(name, taken.text, taken.length, true);
        result->location = taken.location;
        result->via = (uint16_t)(binding - lang->bindings + 1);
    } else if (smith_lang_generic_call(lang, host, name, binding, &taken)) {
        lang->namespace_done = true; // the call, with its arguments, is written
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
        const SmithLangCName *chosen = lang->cnames_count > 0 ? smith_lang_cname_find(lang, lang->tmp, total) : nullptr;
        if (chosen == nullptr) smith_lang_check_listed(lang, host, binding, &taken, total);
        if (smith_lang_is_hidden(lang, lang->tmp, total)) {
            smith_lang_error(lang, host, taken.location,
                             "`%s::%.*s` is private to its module (an exported generic's instance uses it); export it "
                             "to use it",
                             smith_lang_str(binding->name), (int)taken.length, taken.text);
        }
        if (chosen != nullptr) { // the module chose this export's C name
            total = chosen->c_name.length;
            memcpy(lang->tmp, smith_lang_str(chosen->c_name), total + 1);
        }
        *result = smith_lang_made(name, lang->tmp, total, false);
        if (smith_lang_is_io_binding(binding)) smith_lang_note_io_use(lang, &taken, total);
        result->location = taken.location;
        result->via = (uint16_t)(binding - lang->bindings + 1);
        smith_lang_written_access(lang, binding, total, taken.text, taken.length);
    }
    return true;
}

// ---- One identifier
// --------------------------------------------------------------------------------------------------

static SmithLangLowered smith_lang_keep(void) {
    SmithLangLowered kept = {};
    return kept;
}

static SmithLangLowered smith_lang_prefix_name(SmithLang *lang, const char *name, size_t length, bool is_type) {
    SmithLangLowered lowered = {};
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
    SmithLangLowered lowered = {};
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
    if (known != nullptr) {
        if (lang->recording) smith_lang_generic_private_name(lang, host, known, name, length, location);
        return smith_lang_prefix_name(lang, name, length, true);
    }
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
    if (smith_lang_in_attribute(lang) || (host->previous != nullptr && smith_lang_sparse_in_attribute(lang, host))) {
        return; // [[gnu::format(printf, 1, 2)]]: printf is the attribute's word for a kind of format
    }
    SmithLookup found = host->lookup(host->context, name, (uint32_t)length, SmithLookup_Value);
    *is_type = found.kind == SmithLookup_Type;
    if (found.kind == SmithLookup_None || !found.from_import || found.in_main_file) return;
    for (size_t i = 0; i < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; i++) { // 13 entries
        if (smith_lang_sized[i].name[0] != 'f' && smith_lang_equals(name, length, smith_lang_sized[i].header)) {
            smith_lang_error(lang, host, location, "`%.*s` is a C type name; write `%s`", (int)length, name,
                             smith_lang_sized[i].name);
            return;
        }
    }
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
    if (lang->recording && smith_lang_is_type_param(lang, name, length)) {
        SmithLangLowered param = smith_lang_keep(); // T: kept as written, and marked when it is kept
        param.is_type = true;
        return param;
    }
    SmithLangName *known = lang->is_module ? smith_lang_name_find(lang, name, length) : nullptr;
    if (known != nullptr && lang->in_function && smith_lang_is_param(lang, name, length)) return smith_lang_keep();
    if (known != nullptr && smith_lang_param_here(lang, known, name, length)) return smith_lang_keep();
    if (known != nullptr) {
        if (lang->recording) smith_lang_generic_private_name(lang, host, known, name, length, location);
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
