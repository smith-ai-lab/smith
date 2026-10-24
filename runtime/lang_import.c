// The module line, imports and exports. Included by runtime/lang.c, after runtime/lang_decl.c.

// runtime/lang_generic.c
static void smith_lang_generics_load(SmithLang *lang, const SmithHost *host, const char *module);

// Reads tokens up to the `;` that ends a line, for a line that is malformed.
static void smith_lang_skip_line(SmithLang *lang, const SmithHost *host) {
    for (size_t i = 0; i < smith_lang_batch_max; i++) { // bounded by the batch
        const SmithToken *next = smith_lang_peek(lang, host, 0, false);
        if (next->kind == SmithTokenKind_Eof) return;
        bool done = smith_lang_punct(next, ";");
        smith_lang_take(lang, host);
        if (done) return;
    }
}

// a::b::c into its forms: colons ("a::b::c"), path ("a/b/c") and underscored ("a_b_c"). Stops at the token that is not
// part of the name and returns the last segment through `last`. Returns false, with the error reported, for a bad name.
typedef struct {
    char colons[smith_lang_name_max];
    char path[smith_lang_name_max];
    char underscored[smith_lang_name_max];
    char last[smith_lang_name_max];
    uint32_t location;
} SmithLangModuleName;

static bool smith_lang_read_module_name(SmithLang *lang, const SmithHost *host, SmithLangModuleName *name) {
    size_t used = 0;       // in colons
    size_t short_used = 0; // in path and underscored
    size_t last = 0;
    for (size_t i = 0; i < smith_lang_name_max / 2; i++) { // bounded by the name's size
        const SmithToken *part = smith_lang_peek(lang, host, 0, false);
        if (part->kind != SmithTokenKind_Identifier || smith_lang_is_word(part) ||
            used + part->length + 3 >= smith_lang_name_max) {
            smith_lang_error(lang, host, part->location,
                             "a module name is names separated by `::`, such as game::replay");
            return false;
        }
        if (i == 0) name->location = part->location;
        SmithToken taken = smith_lang_take(lang, host);
        if (used > 0) {
            memcpy(name->colons + used, "::", 2);
            used += 2;
            name->path[short_used] = '/';
            name->underscored[short_used] = '_';
            short_used += 1;
        }
        last = used;
        memcpy(name->colons + used, taken.text, taken.length);
        memcpy(name->path + short_used, taken.text, taken.length);
        memcpy(name->underscored + short_used, taken.text, taken.length);
        used += taken.length;
        short_used += taken.length;
        if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), "::")) break;
        smith_lang_take(lang, host);
    }
    name->colons[used] = name->path[short_used] = name->underscored[short_used] = 0;
    memcpy(name->last, name->colons + last, used - last + 1);
    return true;
}

// The module's name must be the end of the file's path: game::replay is in a file whose path ends in game/replay.smith.
static bool smith_lang_path_matches(const char *file, const char *path) {
    size_t file_length = strlen(file);
    size_t path_length = strlen(path);
    static const char extension[] = ".smith";
    size_t total = path_length + sizeof extension - 1;
    if (file_length < total || memcmp(file + file_length - total, path, path_length) != 0 ||
        memcmp(file + file_length - sizeof extension + 1, extension, sizeof extension - 1) != 0) {
        return false;
    }
    return file_length == total || file[file_length - total - 1] == '/';
}

static bool smith_lang_add_binding(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                   uint32_t location, const SmithLangModuleName *module, bool is_c);

static void smith_lang_module_line(SmithLang *lang, const SmithHost *host, const SmithToken *keyword) {
    SmithLangModuleName name = {};
    bool good = smith_lang_read_module_name(lang, host, &name);
    if (!good) {
        smith_lang_skip_line(lang, host);
        return;
    }
    if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), ";")) {
        smith_lang_error(lang, host, keyword->location, "`module %s` must end with a semicolon", name.colons);
    } else {
        smith_lang_take(lang, host);
    }
    if (lang->seen_declaration || lang->imports_seen > 0 ||
        (host->declared != nullptr && host->declared(host->context))) {
        smith_lang_error(lang, host, keyword->location, "the `module` line must come first in the file");
    }
    if (lang->seen_module_line) {
        smith_lang_error(lang, host, keyword->location, "a file has one module line");
        return;
    }
    lang->seen_module_line = true;
    if (host->main_file != nullptr && !smith_lang_path_matches(host->main_file, name.path)) {
        smith_lang_error(lang, host, name.location,
                         "module `%s` must be in a file whose path ends in `%s.smith`; this one is `%s`", name.colons,
                         name.path, host->main_file);
    }
    lang->is_module = true;
    snprintf(lang->module, sizeof lang->module, "%s", name.colons);
    snprintf(lang->prefix, sizeof lang->prefix, "%s_", name.underscored);
    snprintf(lang->symbol, sizeof lang->symbol, "%s::", name.colons);
    lang->module_text = smith_lang_store(lang, name.colons, strlen(name.colons));
    // The module's own last name is a binding too, so an export is written `tools::dup` as an importer writes it; an
    // import of that name (`import a::tools;` in `b::tools`) takes the name over.
    if (smith_lang_add_binding(lang, host, name.last, strlen(name.last), name.location, &name, false)) {
        smith_lang_binding_find(lang, name.last, strlen(name.last))->is_self = true;
    }
    smith_lang_linked_read(lang, host); // the private names an exported generic's instance calls have a symbol
}

// ---- Imports
// ----------------------------------------------------------------------------------------------------------

// The modules a generated header says it imports: the words after `// smith-imports:`.
static size_t smith_lang_header_imports(const char *text, size_t length, char names[][128], size_t capacity) {
    static const char marker[] = "// smith-imports:";
    size_t count = 0;
    for (size_t i = 0; i + sizeof marker < length; i++) { // bounded by the header
        if ((i != 0 && text[i - 1] != '\n') || memcmp(text + i, marker, sizeof marker - 1) != 0) continue;
        size_t at = i + sizeof marker - 1;
        for (size_t guard = 0; at < length && text[at] != '\n' && count < capacity && guard < length; guard++) {
            while (at < length && text[at] == ' ')
                at += 1;
            size_t start = at;
            while (at < length && text[at] != ' ' && text[at] != '\n')
                at += 1;
            if (at > start && at - start < 128) {
                memcpy(names[count], text + start, at - start);
                names[count++][at - start] = 0;
            }
        }
        break;
    }
    return count;
}

// a::b::c into a/b/c.h
static void smith_lang_module_to_path(const char *module, char *path, size_t capacity) {
    size_t used = 0;
    for (size_t i = 0; module[i] != 0 && used + 4 < capacity; i++) { // bounded by the name
        if (module[i] == ':' && module[i + 1] == ':') {
            path[used++] = '/';
            i += 1;
        } else {
            path[used++] = module[i];
        }
    }
    memcpy(path + used, ".h", 3);
}

// Whether importing `first` leads back to this module: a breadth-first walk over the imports the generated headers
// declare. `chain` gets the names along the way.
static bool smith_lang_find_cycle(SmithLang *lang, const SmithHost *host, const char *first, char *chain,
                                  size_t capacity) {
    static char found[smith_lang_cycle_max][128];
    int parent[smith_lang_cycle_max];
    size_t count = 1;
    snprintf(found[0], sizeof found[0], "%s", first);
    parent[0] = -1;
    for (size_t at = 0; at < count; at++) { // each name is read once
        if (strcmp(found[at], lang->module) == 0) {
            chain[0] = 0;
            size_t used = (size_t)snprintf(chain, capacity, "%s", lang->module);
            int order[smith_lang_cycle_max];
            size_t length = 0;
            for (int i = (int)at; i >= 0 && length < smith_lang_cycle_max; i = parent[i])
                order[length++] = i;
            for (size_t i = length; i > 0 && used < capacity; i--) {
                used += (size_t)snprintf(chain + used, capacity - used, " -> %s", found[order[i - 1]]);
            }
            return true;
        }
        char path[smith_lang_name_max];
        smith_lang_module_to_path(found[at], path, sizeof path);
        const char *text = nullptr;
        uint32_t length = 0;
        if (!host->read_header(host->context, path, &text, &length)) continue;
        char names[smith_lang_cycle_max][128];
        size_t imported = smith_lang_header_imports(text, length, names, smith_lang_cycle_max);
        for (size_t i = 0; i < imported && count < smith_lang_cycle_max; i++) {
            bool seen = false;
            for (size_t j = 0; j < count; j++)
                seen = seen || strcmp(found[j], names[i]) == 0;
            if (seen) continue;
            snprintf(found[count], sizeof found[count], "%s", names[i]);
            parent[count++] = (int)at;
        }
    }
    return false;
}

static bool smith_lang_add_binding(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                   uint32_t location, const SmithLangModuleName *module, bool is_c) {
    SmithLangBinding *binding = smith_lang_binding_find(lang, name, length);
    if (binding != nullptr && !binding->is_self) {
        smith_lang_error(lang, host, location, "`%.*s` is already the name of an import", (int)length, name);
        return false;
    }
    if (binding == nullptr && lang->bindings_count >= smith_lang_bindings_max) {
        lang->broken = true;
        return false;
    }
    if (host->watch != nullptr) host->watch(host->context, name, (uint32_t)length);
    if (binding == nullptr) binding = &lang->bindings[lang->bindings_count++];
    *binding = (SmithLangBinding){};
    binding->name = smith_lang_store(lang, name, length);
    binding->is_c = is_c;
    if (module != nullptr) {
        char prefix[smith_lang_name_max];
        snprintf(prefix, sizeof prefix, "%s_", module->underscored);
        binding->prefix = smith_lang_store(lang, prefix, strlen(prefix));
        binding->module = smith_lang_store(lang, module->colons, strlen(module->colons));
    }
    return true;
}

// `import target;` is the facts of the target (runtime/lang_target.c), no module of any file: no header is read.
static bool smith_lang_import_is_target(SmithLang *lang, const char *module, const char *bind, size_t bind_length) {
    if (strcmp(module, "target") != 0) return false;
    smith_lang_binding_find(lang, bind, bind_length)->is_target = true;
    return true;
}

// Whether a header has a generic that holds a `Field` of its own: one of its lines names `fields(` and, as well, the
// type `Field` or `constexpr auto` (a field kept in a variable). The lines that only write `fields(T)[i].name` and
// `x.[fields(T)[i]]` need nothing of the importer.
static bool smith_lang_import_reflects(const char *text, uint32_t length) {
    static const char marker[] = "// smith-generic:";
    for (const char *at = memmem(text, length, marker, sizeof marker - 1); at != nullptr;
         at = memmem(at + 1, (size_t)(text + length - at - 1), marker, sizeof marker - 1)) { // one pass per line
        const char *end = memchr(at, '\n', (size_t)(text + length - at));
        size_t line = end != nullptr ? (size_t)(end - at) : (size_t)(text + length - at);
        if (memmem(at, line, "=fields p@", 10) == nullptr) continue;
        if (memmem(at, line, "=Field ", 7) != nullptr) return true;
        if (memmem(at, line, "=constexpr ", 11) != nullptr && memmem(at, line, "=auto ", 6) != nullptr) return true;
    }
    return false;
}

// A module whose exports use tuples says so in its header (`// smith-tuples`): this file indexes the tuples it gets, so
// the host offers it every token.
static void smith_lang_import_tuples(SmithLang *lang, const SmithHost *host, const char *text, uint32_t length) {
    if (text == nullptr) return;
    if (!lang->field_import && smith_lang_import_reflects(text, length)) { // a generic that keeps a field (#100)
        lang->field_import = true;
        if (host->watch_mode != nullptr) host->watch_mode(host->context, SmithWatch_All);
    }
    if (memmem(text, length, "// smith-slices", 15) != nullptr) lang->slices_seen = true;
    if (memmem(text, length, "// smith-enums", 14) != nullptr) lang->enums_seen = true;
    if (!lang->type_import && memmem(text, length, "// smith-types:", 15) != nullptr) { // functions over `type` (#107)
        lang->type_import = true;
        if (host->type_values != nullptr) host->type_values(host->context);
        if (host->watch_mode != nullptr) host->watch_mode(host->context, SmithWatch_All);
    }
    if (memmem(text, length, "// smith-tuples", 15) == nullptr && !lang->slices_seen) return;
    lang->tuples_seen = true;
    if (host->watch_mode != nullptr) host->watch_mode(host->context, SmithWatch_All);
}

// The header an `import a;` found is a C header, not the one smith writes for module `a` (it has no `// smith-module:`
// line): a C header with the module's name is on the include path ahead of the module's. The import is the one error.
static bool smith_lang_refuse_c_header(SmithLang *lang, const SmithHost *host, const SmithLangModuleName *name,
                                       const char *bind, size_t bind_length, const char *header, const char *text,
                                       uint32_t length) {
    if (text == nullptr || length == 0) return false;
    if (memmem(text, length, "// smith-module:", 16) != nullptr) { // a module's header
        smith_lang_import_tuples(lang, host, text, length);
        return false;
    }
    SmithLangBinding *failed = smith_lang_binding_find(lang, bind, bind_length);
    if (failed != nullptr) failed->missing = true;
    smith_lang_error(lang, host, name->location,
                     "`import %s;` found `%s`, which is a C header, not the header smith writes for the module (it "
                     "has no `// smith-module:` line): rename the C header or the module, or import the C header as "
                     "`import \"%s\";`",
                     name->colons, header, header);
    return true;
}

// runtime/lang_type.c, which comes after this file: the import's functions over `type` are defined here (issue #107)
static void smith_lang_types_define(SmithLang *lang, const SmithHost *host, uint32_t location);

// runtime/lang_body.c: the import's functions that this file runs at compile time are defined here (issue #107)
static void smith_lang_body_use(SmithLang *lang, const SmithHost *host, uint32_t location);

static void smith_lang_import_smith(SmithLang *lang, const SmithHost *host, const SmithToken *keyword) {
    SmithLangModuleName name = {};
    if (!smith_lang_read_module_name(lang, host, &name)) {
        smith_lang_skip_line(lang, host);
        return;
    }
    const char *bind = name.last;
    size_t bind_length = strlen(name.last);
    SmithToken alias = {};
    if (smith_lang_ident(smith_lang_peek(lang, host, 0, false), "as")) {
        smith_lang_take(lang, host);
        alias = smith_lang_take(lang, host);
        if (alias.kind != SmithTokenKind_Identifier || smith_lang_is_word(&alias)) {
            smith_lang_error(lang, host, alias.location, "`as` must be followed by the name to bind");
            smith_lang_skip_line(lang, host);
            return;
        }
        bind = alias.text;
        bind_length = alias.length;
    }
    if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), ";")) {
        smith_lang_error(lang, host, keyword->location, "`import %s` must end with a semicolon", name.colons);
    } else {
        smith_lang_take(lang, host);
    }
    if (smith_lang_equals(bind, bind_length, "c")) {
        smith_lang_error(lang, host, name.location, "`c` is the name of C's namespace; bind this import with `as`");
        return;
    }
    if (!smith_lang_add_binding(lang, host, bind, bind_length, name.location, &name, false)) return;
    if (smith_lang_import_is_target(lang, name.colons, bind, bind_length)) return;
    if (strcmp(name.colons, "smith::io") == 0) lang->io_import = true; // its `main` may take the io
    if (lang->is_module && strcmp(name.colons, lang->module) == 0) {
        smith_lang_error(lang, host, name.location, "module `%s` imports itself", name.colons);
        smith_lang_binding_find(lang, bind, bind_length)->missing = true;
        return;
    }
    char header[smith_lang_name_max];
    smith_lang_module_to_path(name.colons, header, sizeof header);
    const char *text = nullptr;
    uint32_t length = 0;
    if (host->read_header != nullptr && !host->read_header(host->context, header, &text, &length)) {
        SmithLangBinding *failed = smith_lang_binding_find(lang, bind, bind_length);
        if (failed != nullptr) failed->missing = true;
        const char *problem = host->read_problem != nullptr ? host->read_problem(host->context) : nullptr;
        if (problem != nullptr) {
            smith_lang_error(lang, host, name.location, "%s", problem);
        } else {
            smith_lang_error(
                lang, host, name.location,
                "cannot find module `%s`: no `%s` in the include paths (smith writes it from the module's source)",
                name.colons, header);
        }
        return;
    }
    if (smith_lang_refuse_c_header(lang, host, &name, bind, bind_length, header, text, length)) return;
    char chain[smith_lang_message_max];
    if (lang->is_module && host->read_header != nullptr &&
        smith_lang_find_cycle(lang, host, name.colons, chain, sizeof chain)) {
        smith_lang_error(lang, host, name.location, "import cycle: %s", chain);
        smith_lang_binding_find(lang, bind, bind_length)->missing = true;
        return;
    }
    if (lang->imports_count < smith_lang_imports_max) {
        lang->smith_imports[lang->imports_count++] = smith_lang_store(lang, name.colons, strlen(name.colons));
    }
    smith_lang_written_read(lang, host, header, name.colons, bind, bind_length);
    smith_lang_generics_load(lang, host, name.colons);
    host->include(host->context, header, false);
}

// import <h>; or import "h";
static void smith_lang_import_c(SmithLang *lang, const SmithHost *host, const SmithToken *keyword) {
    char header[smith_lang_name_max] = "";
    size_t used = 0;
    bool angled = smith_lang_punct(smith_lang_peek(lang, host, 0, false), "<");
    if (angled) {
        smith_lang_take(lang, host);
        for (size_t i = 0; i < smith_lang_name_max; i++) { // bounded by the name's size
            SmithToken part = smith_lang_take(lang, host);
            if (smith_lang_punct(&part, ">") || part.kind == SmithTokenKind_Eof ||
                used + part.length + 1 >= sizeof header)
                break;
            memcpy(header + used, part.text, part.length);
            used += part.length;
        }
    } else {
        SmithToken quoted = smith_lang_take(lang, host);
        if (quoted.length >= 2 && quoted.length - 2 + 1 < sizeof header) {
            used = quoted.length - 2;
            memcpy(header, quoted.text + 1, used);
        }
    }
    header[used] = 0;
    if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), ";")) {
        smith_lang_error(lang, host, keyword->location, "`import` of a header must end with a semicolon");
    } else {
        smith_lang_take(lang, host);
    }
    if (used == 0) {
        smith_lang_error(lang, host, keyword->location,
                         "`import` of a header needs its name, as in `import <math.h>;`");
        return;
    }
    if (!lang->has_c) {
        lang->has_c = true;
        smith_lang_add_binding(lang, host, "c", 1, keyword->location, nullptr, true);
    }
    smith_lang_add_include(lang, header, used, angled);
    host->include(host->context, header, angled);
}

// Whether `import` here starts an import line, rather than a C declaration that happens to use the name.
static bool smith_lang_is_import(SmithLang *lang, const SmithHost *host, const SmithToken *keyword) {
    const SmithToken *next = smith_lang_peek(lang, host, 0, false);
    bool shaped = smith_lang_punct(next, "<") || next->kind == SmithTokenKind_String ||
                  (next->kind == SmithTokenKind_Identifier && !smith_lang_is_word(next));
    if (!shaped) return false;
    return !smith_lang_is_own_word(lang, host, keyword);
}

static void smith_lang_import(SmithLang *lang, const SmithHost *host, const SmithToken *keyword) {
    if (lang->seen_declaration || (host->declared != nullptr && host->declared(host->context))) {
        smith_lang_error(lang, host, keyword->location, "imports come before any declaration");
    }
    lang->imports_seen += 1;
    const SmithToken *next = smith_lang_peek(lang, host, 0, false);
    if (smith_lang_punct(next, "<") || next->kind == SmithTokenKind_String) {
        smith_lang_import_c(lang, host, keyword);
    } else {
        smith_lang_import_smith(lang, host, keyword);
    }
    // The last import line: the functions over `type` of the imports are defined after it (runtime/lang_type.c).
    if (lang->type_import && lang->imports_seen == host->import_lines)
        smith_lang_types_define(lang, host, keyword->location);
    if (lang->imports_seen == host->import_lines) smith_lang_body_use(lang, host, keyword->location);
}

static bool smith_lang_is_module_line(SmithLang *lang, const SmithHost *host, const SmithToken *keyword) {
    const SmithToken *next = smith_lang_peek(lang, host, 0, false);
    bool shaped = smith_lang_punct(next, ";") || (next->kind == SmithTokenKind_Identifier && !smith_lang_is_word(next));
    if (!shaped) return false;
    return !smith_lang_is_own_word(lang, host, keyword);
}
