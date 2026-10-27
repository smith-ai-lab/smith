// The bodies of a module's functions, for an importer that runs them at compile time (issue #107).
//
// Exported functions are declared in the module's header, so a call in an importing file is to a declaration, and
// clang's evaluator, which needs a definition, cannot run it in a `static_assert` or a `constexpr` initializer. The
// module's own compile has the whole lowered text (slices, tuples and the rest already plain C), so it writes it once,
// beside the header, as `<module>.body` (`SMITH_LANG_BODY=path`): the text of each declaration, with the names it
// declares and what it is. An importer that mentions an exported function in a compile-time context reads the file
// (runtime/lang_bodyuse.c), takes that function and what its text needs of the module, and defines them in front of
// its first declaration, as copies that emit no code of their own.
//
// The file is C that can be included whole, and text a reader can split: `// smith-body: <module>`, then for each
// declaration `//@ K names...` and, but for V, `#if defined(SMITH_BODY_<name>)`, its text and `#endif`. K is F (an
// exported function's definition, written `[[gnu::gnu_inline]] extern inline`: it is evaluated and inlined, and the
// module's one symbol stays the function), P (a private function's), T (a type or a constant: a copy is the same
// thing) or V (state, a variable that is not constant: a copy would be a second variable, so what needs it is not
// carried). What the header holds is left out. An importer that needs `fib` defines SMITH_BODY_calc_fib, with those
// of what it needs in turn, and includes the file (the depfile then names it).

constexpr size_t smith_lang_body_sections_max = 8192;
constexpr size_t smith_lang_body_names_text = 512;

typedef struct {
    uint32_t from; // [from, to) of smith_lang_dump
    uint32_t to;
} SmithLangBodySpan;

static SmithLangBodySpan smith_lang_body_spans[smith_lang_body_sections_max];

// A declaration is complete: the text written since the one before it is its section.
static void smith_lang_body_note(SmithLang *lang) {
    if (!lang->body_enabled || lang->body_closed) return;
    if (lang->dump_used > lang->body_mark && lang->body_count < smith_lang_body_sections_max) {
        smith_lang_body_spans[lang->body_count++] =
            (SmithLangBodySpan){(uint32_t)lang->body_mark, (uint32_t)lang->dump_used};
    }
    lang->body_mark = lang->dump_used;
}

// ---- Reading lowered text
// The text is C as the lowering writes it: names, numbers, strings, one-character punctuation.

static bool smith_body_word_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool smith_body_word_char(char c) {
    return smith_body_word_start(c) || (c >= '0' && c <= '9');
}

// The next token of text[*at ..): its [start, end) and kind (`i` name, `n` number, `s` string or character, `p`
// punctuation); 0 at the end.
static char smith_body_next(const char *text, size_t length, size_t *at, size_t *start, size_t *end) {
    while (*at < length && (text[*at] == ' ' || text[*at] == '\n' || text[*at] == '\t' || text[*at] == '\r'))
        *at += 1;
    if (*at >= length) return 0;
    *start = *at;
    char c = text[*at];
    char kind = 'p';
    if (smith_body_word_start(c)) {
        kind = 'i';
        while (*at < length && smith_body_word_char(text[*at]))
            *at += 1;
    } else if (c >= '0' && c <= '9') {
        kind = 'n';
        while (*at < length && (smith_body_word_char(text[*at]) || text[*at] == '.'))
            *at += 1;
    } else if (c == '"' || c == '\'') {
        kind = 's';
        *at += 1;
        while (*at < length && text[*at] != c) {
            *at += text[*at] == '\\' && *at + 1 < length ? 2 : 1;
        }
        *at += *at < length ? 1 : 0;
    } else {
        *at += 1;
    }
    *end = *at;
    return kind;
}

static bool smith_body_is(const char *text, size_t start, size_t end, const char *word) {
    return end - start == strlen(word) && memcmp(text + start, word, end - start) == 0;
}

// One declaration of a section: what it is and the names it declares.
typedef struct {
    char kind;
    size_t from;
    size_t to;
    char names[smith_lang_body_names_text];
    size_t names_used;
} SmithBodyDecl;

static void smith_body_add_name(SmithBodyDecl *decl, const char *name, size_t length) {
    if (decl->names_used + length + 2 >= sizeof decl->names) return;
    memcpy(decl->names + decl->names_used, name, length);
    decl->names_used += length;
    decl->names[decl->names_used++] = ' ';
    decl->names[decl->names_used] = 0;
}

// Whether `name` is an exported name of the module, as its header declares it (prefixed, or the C name chosen).
static bool smith_body_exported(const SmithLang *lang, const char *name, size_t length) {
    for (size_t i = 0; i < lang->names_count; i++) {
        const SmithLangName *known = &lang->names[i];
        if (!known->exported) continue;
        if (known->c_name.length > 0) {
            if (known->c_name.length == length && memcmp(smith_lang_str(known->c_name), name, length) == 0) return true;
            continue;
        }
        size_t prefix = strlen(lang->prefix);
        if (length == prefix + known->name.length && memcmp(name, lang->prefix, prefix) == 0 &&
            memcmp(name + prefix, smith_lang_str(known->name), known->name.length) == 0) {
            return true;
        }
    }
    return false;
}

static bool smith_body_listed(const SmithLang *lang, const char *name, size_t length) {
    size_t prefix = strlen(lang->prefix);
    for (size_t i = 0; i < lang->body_names_count; i++) {
        const SmithLangText listed = lang->body_names[i];
        const SmithLangName *known = smith_lang_name_find((SmithLang *)lang, smith_lang_str(listed), listed.length);
        if (known != nullptr && known->c_name.length > 0) {
            if (known->c_name.length == length && memcmp(smith_lang_str(known->c_name), name, length) == 0) return true;
        } else if (length == prefix + listed.length && memcmp(name, lang->prefix, prefix) == 0 &&
                   memcmp(name + prefix, smith_lang_str(listed), listed.length) == 0) {
            return true;
        }
    }
    return false;
}

// The next declaration of text[*at, end): [*first, *last) of it, the end after its `;` or after the `}` of a function's
// body (a `{` that follows a `)`). False at the end of the text.
static bool smith_body_split(const char *text, size_t length, size_t *at, size_t *first, size_t *last) {
    size_t start = 0;
    size_t end = 0;
    char previous = 0;
    int depth = 0;
    *first = *at;
    bool any = false;
    for (size_t guard = 0; guard < length + 1; guard++) { // one token per pass
        char kind = smith_body_next(text, length, at, &start, &end);
        if (kind == 0) break;
        if (!any) *first = start;
        any = true;
        char c = kind == 'p' ? text[start] : 0;
        if (kind == 'p' && (c == '(' || c == '[')) {
            depth += 1;
        } else if (kind == 'p' && (c == ')' || c == ']')) {
            depth -= depth > 0 ? 1 : 0;
        } else if (kind == 'p' && c == '{' && depth == 0 && previous == ')') {
            int braces = 1; // a function's body: to its matching `}`
            while (braces > 0 && (kind = smith_body_next(text, length, at, &start, &end)) != 0) {
                braces += kind == 'p' && text[start] == '{' ? 1 : kind == 'p' && text[start] == '}' ? -1 : 0;
            }
            *last = *at;
            return true;
        } else if (kind == 'p' && c == '{') {
            depth += 1000; // a struct or an initializer: its `;` ends the declaration
        } else if (kind == 'p' && c == '}') {
            depth -= depth >= 1000 ? 1000 : 0;
        } else if (kind == 'p' && c == ';' && depth == 0) {
            *last = *at;
            return true;
        }
        previous = kind == 'p' ? c : 'a';
    }
    *last = *at;
    return any;
}

// What the tokens of a declaration said.
typedef struct {
    bool has_typedef;
    bool has_extern;
    bool has_const;
    bool paren_seen; // a `(` after a name, before any `=`: a function
    bool equals_seen;
    bool brace_seen;
    bool after_equals; // the token before was the `=` of an initializer
    bool lost;     // an initializer with nothing after its `=`: a macro the lowering did not write (a copy is wrong)
    bool in_tag;   // the token before was struct, union or enum
    bool tag_enum; // that word was enum
    bool enumerator_next;
    int enum_depth;
    int depth;
    char previous_kind;
    size_t last_start; // the last name at depth 0
    size_t last_end;
    size_t paren_name_start; // the name before the first `(`
    size_t paren_name_end;
    size_t name_start; // the name before the first `=`
    size_t name_end;
} SmithBodyFacts;

// A struct, union or enum tag that has a body names a type: its name, and for an enum its constants.
static void smith_body_fact_tag(SmithBodyFacts *facts, SmithBodyDecl *decl, const char *text, size_t to, size_t at,
                                char kind, size_t start, size_t end) {
    bool tag = kind == 'i' && (smith_body_is(text, start, end, "struct") || smith_body_is(text, start, end, "union") ||
                               smith_body_is(text, start, end, "enum"));
    if (tag) {
        facts->in_tag = true;
        facts->tag_enum = smith_body_is(text, start, end, "enum");
    } else if (kind == 'i' && facts->in_tag) {
        facts->in_tag = false;
        size_t probe = at;
        size_t ps = 0;
        size_t pe = 0;
        if (facts->depth == 0 && smith_body_next(text, to, &probe, &ps, &pe) == 'p' && text[ps] == '{')
            smith_body_add_name(decl, text + start, end - start);
    } else if (kind != 'i') {
        facts->in_tag = false;
    }
    char c = kind == 'p' ? text[start] : 0;
    if (c == '{' && facts->tag_enum && facts->enum_depth < 0) {
        facts->enum_depth = facts->depth + 1;
        facts->enumerator_next = true;
    } else if (kind == 'i' && facts->enumerator_next && facts->depth == facts->enum_depth) {
        smith_body_add_name(decl, text + start, end - start);
        facts->enumerator_next = false;
    } else if (c == ',' && facts->depth == facts->enum_depth) {
        facts->enumerator_next = true;
    }
}

// One token of a declaration.
static void smith_body_fact(SmithBodyFacts *facts, SmithBodyDecl *decl, const char *text, size_t to, size_t at,
                            char kind, size_t start, size_t end) {
    char c = kind == 'p' ? text[start] : 0;
    if (kind == 'i' && facts->depth == 0) {
        facts->has_typedef = facts->has_typedef || smith_body_is(text, start, end, "typedef");
        facts->has_extern = facts->has_extern || smith_body_is(text, start, end, "extern");
        facts->has_const = facts->has_const || (!facts->equals_seen && (smith_body_is(text, start, end, "const") ||
                                                                        smith_body_is(text, start, end, "constexpr")));
    }
    facts->lost = facts->lost || (facts->after_equals && facts->depth == 0 && (c == ';' || c == ','));
    facts->after_equals = c == '=' && facts->depth == 0;
    smith_body_fact_tag(facts, decl, text, to, at, kind, start, end);
    if (c == '(' || c == '[' || c == '{') {
        if (c == '(' && facts->depth == 0 && !facts->paren_seen && !facts->equals_seen && facts->previous_kind == 'i') {
            facts->paren_seen = true;
            facts->paren_name_start = facts->last_start;
            facts->paren_name_end = facts->last_end;
        }
        facts->brace_seen = facts->brace_seen || c == '{';
        facts->depth += 1;
    } else if (c == ')' || c == ']' || c == '}') {
        facts->depth -= facts->depth > 0 ? 1 : 0;
        if (c == '}' && facts->depth < facts->enum_depth) facts->tag_enum = false;
    } else if (c == '=' && facts->depth == 0 && !facts->equals_seen) {
        facts->equals_seen = true;
        facts->name_start = facts->last_start;
        facts->name_end = facts->last_end;
    }
    if (kind == 'i' && facts->depth == 0) {
        facts->last_start = start;
        facts->last_end = end;
    }
    facts->previous_kind = kind == 'i' ? 'i' : 'p';
}

// A function's definition: a listed export is F, a private function P, and an exported inline function, which the
// header defines, nothing.
static void smith_body_function(const SmithLang *lang, const char *text, const SmithBodyFacts *facts,
                                SmithBodyDecl *decl) {
    size_t length = facts->paren_name_end - facts->paren_name_start;
    const char *name = text + facts->paren_name_start;
    if (smith_body_listed(lang, name, length)) {
        decl->kind = 'F';
    } else if (!smith_body_exported(lang, name, length)) {
        decl->kind = 'P';
    }
    smith_body_add_name(decl, name, length);
}

// What the declaration text[from, to) is, and the names it declares.
static void smith_body_classify(const SmithLang *lang, const char *text, size_t from, size_t to, SmithBodyDecl *decl) {
    *decl = (SmithBodyDecl){.kind = 'D', .from = from, .to = to};
    SmithBodyFacts facts = {.enum_depth = -1};
    size_t at = from;
    size_t start = 0;
    size_t end = 0;
    for (size_t guard = 0; guard < to - from + 1; guard++) { // one token per pass
        char kind = smith_body_next(text, to, &at, &start, &end);
        if (kind == 0) break;
        smith_body_fact(&facts, decl, text, to, at, kind, start, end);
    }
    if (facts.paren_seen && to > from && text[to - 1] == '}' && !facts.has_typedef) {
        smith_body_function(lang, text, &facts, decl);
        return;
    }
    if (facts.paren_seen && !facts.has_typedef && !facts.equals_seen) return; // a prototype
    if (facts.has_typedef) {
        if (facts.last_end > facts.last_start)
            smith_body_add_name(decl, text + facts.last_start, facts.last_end - facts.last_start);
    } else if (facts.equals_seen && facts.name_end > facts.name_start) {
        smith_body_add_name(decl, text + facts.name_start, facts.name_end - facts.name_start);
    } else if (!facts.brace_seen && facts.last_end > facts.last_start) {
        smith_body_add_name(decl, text + facts.last_start, facts.last_end - facts.last_start);
    }
    bool declared_exported = false;
    for (size_t i = 0; i < decl->names_used;) { // an exported name: the header holds the declaration
        size_t j = i;
        while (j < decl->names_used && decl->names[j] != ' ')
            j += 1;
        declared_exported = declared_exported || smith_body_exported(lang, decl->names + i, j - i);
        i = j + 1;
    }
    // An exported type is in the header, but a tag defined here may be defined there only as an opaque name
    // (`typedef struct SmithLoop SmithLoop;`): its definition is carried, and a second one that is the same is C's.
    bool tag_definition = !facts.has_typedef && facts.brace_seen && !facts.equals_seen && !facts.paren_seen;
    if ((declared_exported && !tag_definition) || facts.has_extern || decl->names_used == 0) return;
    bool state = !facts.has_typedef && !facts.has_const && (facts.equals_seen || !facts.brace_seen);
    decl->kind = state || facts.lost ? 'V' : 'T';
}

// ---- Writing the file

// The guard of a section: it is defined for a name the importer needs, so that the file may be included whole.
static void smith_body_put(FILE *file, const SmithBodyDecl *decl, const char *text) {
    if (decl->kind == 'D' || decl->to <= decl->from) return;
    fprintf(file, "//@ %c %s\n", decl->kind, decl->names);
    if (decl->kind == 'V') return; // state: nothing to define, the line says a copy would be wrong
    fputs("#if", file);
    for (size_t i = 0; i < decl->names_used;) {
        size_t j = i;
        while (j < decl->names_used && decl->names[j] != ' ')
            j += 1;
        fprintf(file, "%s defined(SMITH_BODY_%.*s)", i > 0 ? " ||" : "", (int)(j - i), decl->names + i);
        i = j + 1;
    }
    fputc('\n', file);
    size_t from = decl->from;
    if (decl->kind ==
        'F') { // no code of its own: the module has it. The attributes the function began with stay first.
        size_t at = from;
        size_t start = 0;
        size_t end = 0;
        for (size_t guard = 0; guard < decl->to - decl->from; guard++) { // one `[[...]]` per pass
            size_t probe = at;
            if (smith_body_next(text, decl->to, &probe, &start, &end) != 'p' || text[start] != '[') break;
            if (smith_body_next(text, decl->to, &probe, &start, &end) != 'p' || text[start] != '[') break;
            int depth = 2;
            while (depth > 0 && smith_body_next(text, decl->to, &probe, &start, &end) != 0) {
                depth += text[start] == '[' && end - start == 1 ? 1 : text[start] == ']' && end - start == 1 ? -1 : 0;
            }
            at = probe;
        }
        fwrite(text + from, 1, at - from, file);
        from = at;
        fputs(" [[gnu::gnu_inline]] extern inline ", file);
    }
    fwrite(text + from, 1, decl->to - from, file);
    fputs("\n#endif\n", file);
}

static void smith_lang_body_write(SmithLang *lang) {
    const char *path = getenv("SMITH_LANG_BODY");
    if (!lang->body_enabled || path == nullptr || !lang->is_module || lang->errors > 0) return;
    FILE *file = fopen(path, "wb");
    if (file == nullptr) return;
    fprintf(file, "// smith-body: %s\n", lang->module);
    char source[PATH_MAX];
    if (lang->main_file != nullptr && realpath(lang->main_file, source) != nullptr) {
        // The importer includes this file, and its depfile then names the module's source: an edit to it rebuilds
        // the importer (the preprocessor reads `__has_include`'s file as a dependency).
        fprintf(file, "#if __has_include(\"%s\")\n#endif\n", source);
    }
    if (lang->include_used > 0) { // the C headers the module's code needs: defined with the first thing a copy needs
        char prelude[2048];
        size_t prelude_used = smith_lang_target_include_prelude(lang, prelude, sizeof prelude, 0);
        fprintf(file, "//@ H SMITH_BODY_INCLUDES\n#if defined(SMITH_BODY_SMITH_BODY_INCLUDES)\n%.*s%.*s#endif\n",
                (int)prelude_used, prelude, (int)lang->include_used, lang->include_body);
    }
    for (size_t i = 0; i < lang->body_count; i++) { // the declarations, in the order of the file
        const SmithLangBodySpan *span = &smith_lang_body_spans[i];
        size_t at = span->from;
        for (size_t guard = 0; guard < (size_t)(span->to - span->from) + 1; guard++) { // one declaration per pass
            size_t first = 0;
            size_t last = 0;
            if (!smith_body_split(smith_lang_dump, span->to, &at, &first, &last)) break;
            SmithBodyDecl decl;
            smith_body_classify(lang, smith_lang_dump, first, last, &decl);
            smith_body_put(file, &decl, smith_lang_dump);
        }
    }
    fclose(file);
}

// ---- Using the bodies of the imports
//
// After the last `import` line, the file asks which exported functions of its imports it runs at compile time (the host
// answers from the file's text: a call in a `static_assert`, a `constexpr` initializer, an `if constexpr`, a file-scope
// initializer or head), takes them from the `.body` of their modules with what their text needs of the module and of
// the modules those import, and defines them before its first declaration.

constexpr size_t smith_body_modules_max = 24;
constexpr size_t smith_body_entries_max = 4096;
constexpr size_t smith_body_pending_max = 4096;
constexpr size_t smith_body_text_max = (size_t)1 << 21;

typedef struct {
    char kind;
    uint8_t module;
    uint32_t names; // [names, names + names_length) of the file's text
    uint32_t names_length;
    uint32_t text; // the lines of the guarded section, without the guard and its #endif
    uint32_t text_length;
    bool selected;
    bool added; // selected for the root being closed: undone if it fails
} SmithBodyEntry;

typedef struct {
    char name[smith_lang_name_max];   // a::b
    char prefix[smith_lang_name_max]; // a_b_
    const char *file;                 // the body file's text, from the host
    uint32_t length;
    char path[PATH_MAX]; // the file to include for the depfile (empty: made in memory)
    uint32_t first;      // its entries
    uint32_t count;
    bool loaded;
    bool include; // its header is included for the names a copy uses
    bool bound;   // the importing file imports it: its header is already included
    bool any;     // an entry of it is selected
} SmithBodyModule;

static SmithBodyModule smith_body_modules[smith_body_modules_max];
static size_t smith_body_module_count;
static SmithBodyEntry smith_body_entries[smith_body_entries_max];
static size_t smith_body_entry_count;
static char smith_body_out[smith_body_text_max];

static bool smith_body_words_have(const char *text, size_t length, const char *marker, const char *word,
                                  size_t word_length, bool prefixed, const char *prefix) {
    size_t cursor = 0;
    size_t start = 0;
    size_t end = 0;
    size_t prefix_length = prefixed ? strlen(prefix) : 0;
    while (smith_lang_header_word(text, length, marker, &cursor, &start, &end)) { // each word
        if (prefixed ? (end - start + prefix_length == word_length && memcmp(prefix, word, prefix_length) == 0 &&
                        memcmp(text + start, word + prefix_length, end - start) == 0)
                     : (end - start == word_length && memcmp(text + start, word, word_length) == 0)) {
            return true;
        }
    }
    return false;
}

static void smith_body_prefix(const char *module, char *prefix, size_t capacity) {
    size_t used = 0;
    for (size_t i = 0; module[i] != 0 && used + 2 < capacity; i++) {
        if (module[i] == ':' && module[i + 1] == ':') {
            prefix[used++] = '_';
            i += 1;
        } else {
            prefix[used++] = module[i];
        }
    }
    prefix[used++] = '_';
    prefix[used] = 0;
}

static int smith_body_module_find(const char *name) {
    for (size_t i = 0; i < smith_body_module_count; i++) {
        if (strcmp(smith_body_modules[i].name, name) == 0) return (int)i;
    }
    return -1;
}

// Splits the body file's lines into entries: `//@ K names`, then the guarded section.
static void smith_body_parse(SmithBodyModule *module, uint32_t index) {
    module->first = (uint32_t)smith_body_entry_count;
    const char *text = module->file;
    size_t length = module->length;
    for (size_t at = 0; at < length;) { // each line
        const char *newline = memchr(text + at, '\n', length - at);
        size_t end = newline != nullptr ? (size_t)(newline - text) : length;
        if (end - at > 4 && memcmp(text + at, "//@ ", 4) == 0 && smith_body_entry_count < smith_body_entries_max) {
            SmithBodyEntry *entry = &smith_body_entries[smith_body_entry_count++];
            *entry = (SmithBodyEntry){.kind = text[at + 4], .module = (uint8_t)index};
            entry->names = (uint32_t)(at + 6);
            entry->names_length = end > at + 6 ? (uint32_t)(end - at - 6) : 0;
            size_t body = end + 1; // the `#if` line, then the text, then `#endif`
            if (entry->kind != 'V' && body < length) {
                size_t guard_end = body;
                while (guard_end < length && text[guard_end] != '\n')
                    guard_end += 1;
                entry->text = (uint32_t)(guard_end + 1);
                size_t finish = entry->text;
                while (finish + 6 <= length &&
                       !(memcmp(text + finish, "#endif\n", 6) == 0 && (finish == 0 || text[finish - 1] == '\n'))) {
                    const char *next = memchr(text + finish, '\n', length - finish);
                    if (next == nullptr) break;
                    finish = (size_t)(next - text) + 1;
                }
                entry->text_length = finish > entry->text ? (uint32_t)(finish - entry->text) : 0;
                end = finish;
                const char *after = memchr(text + end, '\n', length - end);
                end = after != nullptr ? (size_t)(after - text) : length;
            }
        }
        at = end + 1;
    }
    module->count = (uint32_t)smith_body_entry_count - module->first;
    module->loaded = true;
}

static bool smith_body_load(const SmithHost *host, uint32_t index) {
    SmithBodyModule *module = &smith_body_modules[index];
    if (module->loaded) return module->file != nullptr;
    module->loaded = true;
    char header[smith_lang_name_max];
    smith_lang_module_to_path(module->name, header, sizeof header);
    const char *path = nullptr;
    if (!host->read_body(host->context, header, &module->file, &module->length, &path) || module->file == nullptr) {
        module->file = nullptr;
        return false;
    }
    snprintf(module->path, sizeof module->path, "%s", path != nullptr ? path : "");
    module->loaded = false;
    smith_body_parse(module, index);
    return true;
}

static SmithBodyEntry *smith_body_entry_named(const SmithBodyModule *module, const char *name, size_t length) {
    for (uint32_t i = module->first; i < module->first + module->count; i++) {
        SmithBodyEntry *entry = &smith_body_entries[i];
        const char *names = module->file + entry->names;
        for (size_t at = 0; at < entry->names_length;) { // the names of the entry, separated by spaces
            size_t end = at;
            while (end < entry->names_length && names[end] != ' ')
                end += 1;
            if (end - at == length && memcmp(names + at, name, length) == 0) return entry;
            at = end + 1;
        }
    }
    return nullptr;
}

typedef struct {
    uint8_t module;
    char name[smith_lang_name_max];
} SmithBodyNeed;

static SmithBodyNeed smith_body_pending[smith_body_pending_max];

// Adds a module to the table (by name), reading what its header says of its bodies.
static int smith_body_module_add(const SmithHost *host, const char *name, bool bound) {
    int found = smith_body_module_find(name);
    if (found >= 0) {
        smith_body_modules[found].bound = smith_body_modules[found].bound || bound;
        return found;
    }
    if (smith_body_module_count >= smith_body_modules_max) return -1;
    (void)host;
    SmithBodyModule *module = &smith_body_modules[smith_body_module_count];
    *module = (SmithBodyModule){.bound = bound};
    snprintf(module->name, sizeof module->name, "%s", name);
    smith_body_prefix(name, module->prefix, sizeof module->prefix);
    return (int)smith_body_module_count++;
}

// The header text of a module, copied: the host's buffer is reused by the next read.
constexpr size_t smith_body_header_max = (size_t)1 << 18;

static bool smith_body_header(const SmithHost *host, const char *module, char *copy, const char **text,
                              uint32_t *length) {
    char path[smith_lang_name_max];
    smith_lang_module_to_path(module, path, sizeof path);
    const char *read = nullptr;
    uint32_t size = 0;
    if (!host->read_header(host->context, path, &read, &size) || size >= smith_body_header_max) return false;
    memcpy(copy, read, size);
    *text = copy;
    *length = size;
    return true;
}

static char smith_body_header_root[smith_body_header_max];
static char smith_body_header_scan[smith_body_header_max];
static char smith_body_header_other[smith_body_header_max];

// Whether `ident` is the C name of an export of the module whose header is given: its member name in `member`.
static bool smith_body_export_named(const char *header, uint32_t length, const char *prefix, const char *ident,
                                    size_t ident_length, char *member, size_t capacity) {
    size_t cursor = 0;
    size_t start = 0;
    size_t end = 0;
    size_t prefix_length = strlen(prefix);
    while (smith_lang_header_word(header, length, "// smith-names:", &cursor, &start, &end)) { // each export
        const char *equals = memchr(header + start, '=', end - start);
        size_t name = equals != nullptr ? (size_t)(equals - (header + start)) : end - start;
        bool chosen = equals != nullptr;
        const char *lowered = chosen ? equals + 1 : header + start;
        size_t lowered_length = chosen ? end - start - name - 1 : name;
        bool match = chosen ? lowered_length == ident_length && memcmp(lowered, ident, ident_length) == 0
                            : prefix_length + name == ident_length && memcmp(prefix, ident, prefix_length) == 0 &&
                                  memcmp(header + start, ident + prefix_length, name) == 0;
        if (match && name < capacity) {
            snprintf(member, capacity, "%.*s", (int)name, header + start);
            return true;
        }
    }
    return false;
}

// An identifier of an entry's text that is an export of a module its module imports: that module's header is included,
// and if the export has a body it is needed in turn. False when the table is full.
static bool smith_body_foreign(const SmithHost *host, const SmithBodyModule *module, const char *header,
                               uint32_t header_length, const char *ident, size_t ident_length, size_t *pending_count) {
    size_t cursor = 0;
    size_t start = 0;
    size_t end = 0;
    while (smith_lang_header_word(header, header_length, "// smith-imports:", &cursor, &start, &end)) { // each import
        char imported[smith_lang_name_max];
        char prefix[smith_lang_name_max];
        char member[smith_lang_name_max];
        snprintf(imported, sizeof imported, "%.*s", (int)(end - start), header + start);
        smith_body_prefix(imported, prefix, sizeof prefix);
        const char *other_header = nullptr;
        uint32_t other_length = 0;
        if (!smith_body_header(host, imported, smith_body_header_other, &other_header, &other_length) ||
            !smith_body_export_named(other_header, other_length, prefix, ident, ident_length, member, sizeof member)) {
            continue;
        }
        int other = smith_body_module_add(host, imported, false);
        if (other < 0) continue;
        smith_body_modules[other].include = true;
        if (!smith_body_words_have(other_header, other_length, "// smith-bodies:", member, strlen(member), false,
                                   prefix)) {
            continue;
        }
        if (*pending_count >= smith_body_pending_max) return false;
        smith_body_pending[*pending_count].module = (uint8_t)other;
        snprintf(smith_body_pending[*pending_count].name, sizeof smith_body_pending[0].name, "%.*s", (int)ident_length,
                 ident);
        *pending_count += 1;
    }
    (void)module;
    return true;
}

// What the text of an entry names of other things: the entries of its module it uses, and the exports of the modules
// its module imports (their headers are included; the ones that have a body are needed in turn).
static bool smith_body_scan(const SmithHost *host, SmithBodyEntry *entry, size_t *pending_count) {
    SmithBodyModule *module = &smith_body_modules[entry->module];
    const char *text = module->file + entry->text;
    size_t length = entry->text_length;
    const char *header = nullptr;
    uint32_t header_length = 0;
    bool has_header = smith_body_header(host, module->name, smith_body_header_scan, &header, &header_length);
    size_t at = 0;
    size_t start = 0;
    size_t end = 0;
    for (size_t guard = 0; guard < length + 1; guard++) { // one token per pass
        char kind = smith_body_next(text, length, &at, &start, &end);
        if (kind == 0) break;
        if (kind != 'i' || end - start >= smith_lang_name_max) continue;
        SmithBodyEntry *used = smith_body_entry_named(module, text + start, end - start);
        if (used != nullptr && used != entry && !used->selected) {
            if (*pending_count >= smith_body_pending_max) return false;
            smith_body_pending[*pending_count].module = entry->module;
            snprintf(smith_body_pending[*pending_count].name, sizeof smith_body_pending[0].name, "%.*s",
                     (int)(end - start), text + start);
            *pending_count += 1;
        } else if (used == nullptr && has_header &&
                   !smith_body_foreign(host, module, header, header_length, text + start, end - start, pending_count)) {
            return false;
        }
    }
    return true;
}

// Selects `name` of the module and everything its text needs. False when one of them is state or the table is full:
// what was selected for it is undone.
static bool smith_body_close(const SmithHost *host, uint32_t module, const char *name) {
    size_t count = 0;
    smith_body_pending[count].module = (uint8_t)module;
    snprintf(smith_body_pending[count++].name, sizeof smith_body_pending[0].name, "%s", name);
    bool good = true;
    for (size_t guard = 0; count > 0 && good && guard < smith_body_pending_max * 16; guard++) { // one name per pass
        SmithBodyNeed need = smith_body_pending[--count];
        if (!smith_body_load(host, need.module)) {
            good = guard != 0; // the root has no body at all: nothing to carry; a callee that has none is declared
            continue;
        }
        SmithBodyEntry *entry = smith_body_entry_named(&smith_body_modules[need.module], need.name, strlen(need.name));
        if (entry == nullptr || entry->selected) continue;
        if (entry->kind == 'V') {
            good = false;
            break;
        }
        entry->selected = entry->added = true;
        SmithBodyModule *owner = &smith_body_modules[need.module];
        SmithBodyEntry *includes = smith_body_entry_named(owner, "SMITH_BODY_INCLUDES", 19);
        if (includes != nullptr && !includes->selected) includes->selected = includes->added = true;
        owner->any = true;
        good = smith_body_scan(host, entry, &count);
    }
    for (size_t i = 0; i < smith_body_entry_count; i++) {
        if (!good && smith_body_entries[i].added) smith_body_entries[i].selected = false;
        smith_body_entries[i].added = false;
    }
    return good;
}

// Writes the selected entries of a module, in the order of its file.
static size_t smith_body_write_module(size_t used, const SmithBodyModule *module) {
    for (uint32_t i = module->first; i < module->first + module->count; i++) {
        const SmithBodyEntry *entry = &smith_body_entries[i];
        if (!entry->selected || used + entry->text_length + 2 >= sizeof smith_body_out) continue;
        memcpy(smith_body_out + used, module->file + entry->text, entry->text_length);
        used += entry->text_length;
    }
    return used;
}

// The C name of the member of a module: the name `// smith-names:` gives (a chosen C name), else the prefixed one.
static void smith_body_lowered(const char *header, uint32_t length, const char *prefix, const char *member,
                               size_t member_length, char *out, size_t capacity) {
    snprintf(out, capacity, "%s%.*s", prefix, (int)member_length, member);
    size_t cursor = 0;
    size_t start = 0;
    size_t end = 0;
    while (smith_lang_header_word(header, length, "// smith-names:", &cursor, &start, &end)) { // each export
        const char *equals = memchr(header + start, '=', end - start);
        if (equals != nullptr && (size_t)(equals - (header + start)) == member_length &&
            memcmp(header + start, member, member_length) == 0) {
            snprintf(out, capacity, "%.*s", (int)(end - start - member_length - 1), equals + 1);
        }
    }
}

// The functions of one import that the file mentions, each closed over what it needs. True if any was taken.
static bool smith_body_import(const SmithHost *host, const SmithLangBinding *binding) {
    const char *header = nullptr;
    uint32_t length = 0;
    if (!smith_body_header(host, smith_lang_str(binding->module), smith_body_header_root, &header, &length) ||
        memmem(header, length, "// smith-bodies:", 16) == nullptr) {
        return false;
    }
    int index = smith_body_module_add(host, smith_lang_str(binding->module), true);
    if (index < 0) return false;
    bool any = false;
    size_t cursor = 0;
    size_t start = 0;
    size_t end = 0;
    while (smith_lang_header_word(header, length, "// smith-bodies:", &cursor, &start, &end)) { // each listed function
        if (end - start >= smith_lang_name_max ||
            !host->mentions(host->context, header + start, (uint32_t)(end - start)))
            continue;
        char lowered[smith_lang_name_max];
        smith_body_lowered(header, length, smith_body_modules[index].prefix, header + start, end - start, lowered,
                           sizeof lowered);
        any = smith_body_close(host, (uint32_t)index, lowered) || any;
    }
    return any;
}

// The text that defines what was selected: the headers its names need first, the modules the last reached first (what a
// module uses is defined before it), and the `#include` of each body file, for the depfile (every section of it is
// closed: no name is defined for it).
static size_t smith_body_assemble(const SmithHost *host) {
    // The copies raise no warning of the importer's flags (they passed the module's own, and the importer's names may
    // shadow their locals): the compiler says nothing of text it was given.
    static const char head[] = "#pragma clang diagnostic push\n#pragma clang diagnostic ignored \"-Weverything\"\n";
    // A C header of the module's may define `static_assert` as a macro, which a smith file does not have (the lowering
    // does not see a macro as a token).
    static const char tail[] = "#pragma clang diagnostic pop\n#undef static_assert\n";
    size_t used = sizeof head - 1;
    memcpy(smith_body_out, head, used);
    for (size_t m = smith_body_module_count; m > 0; m--) {
        const SmithBodyModule *module = &smith_body_modules[m - 1];
        if (module->include && !module->bound) {
            char header[smith_lang_name_max];
            smith_lang_module_to_path(module->name, header, sizeof header);
            host->include(host->context, header, false);
        }
        if (!module->any || module->path[0] == 0) continue;
        int written = snprintf(smith_body_out + used, sizeof smith_body_out - used, "#include \"%s\"\n", module->path);
        used += written > 0 ? (size_t)written : 0;
    }
    for (size_t m = smith_body_module_count; m > 0; m--)
        used = smith_body_write_module(used, &smith_body_modules[m - 1]);
    if (used + sizeof tail < sizeof smith_body_out) {
        memcpy(smith_body_out + used, tail, sizeof tail - 1);
        used += sizeof tail - 1;
    }
    return used;
}

static void smith_lang_body_use(SmithLang *lang, const SmithHost *host, uint32_t location) {
    if (host->read_body == nullptr || host->mentions == nullptr || host->include_text == nullptr || lang->recording) {
        return;
    }
    smith_body_module_count = 0;
    smith_body_entry_count = 0;
    bool any = false;
    for (size_t b = 0; b < lang->bindings_count; b++) { // the imports whose functions the file runs at compile time
        const SmithLangBinding *binding = &lang->bindings[b];
        if (binding->is_c || binding->is_objc || binding->missing || binding->is_target || binding->is_self) continue;
        any = smith_body_import(host, binding) || any;
    }
    if (!any) return;
    size_t used = smith_body_assemble(host);
    if (used >= sizeof smith_body_out - 2) {
        smith_lang_error(lang, host, location,
                         "the functions of the imports run at compile time are too long to define");
        return;
    }
    host->include_text(host->context, smith_body_out, (uint32_t)used);
}
