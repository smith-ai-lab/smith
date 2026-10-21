// The names as the source writes them. Included by runtime/lang.c, after runtime/lang_out.c.
//
// The compiler sees `geometry_box_Box`; the source says `box::Box` (in a file that imports it) or `Box` (in the module
// itself). A tool that shows the compiler's names to a person, such as the diagnostics and an editor's hover, symbols
// and completion, needs the way back, and it cannot be guessed from the underscores: `a_b` can be a real C name. So the
// lowering keeps a table of every name it made from one: the module's own declarations, the exports of the modules the
// file imports (a generated header lists them in its `// smith-names:` line, which is why the importer needs nothing
// else), those of the modules they import, and every `x::name` the file wrote (a name that is declared nowhere is
// still written as it was).

// Where the module's words start in a line: the words after `marker`, one at a time, as [start, end) in the text.
static bool smith_lang_header_word(const char *text, size_t length, const char *marker, size_t *cursor, size_t *start,
                                   size_t *end) {
    size_t marker_length = strlen(marker);
    if (*cursor == 0) {
        size_t at = 0;
        bool found = false;
        for (; at + marker_length <= length; at++) { // bounded by the header
            if ((at == 0 || text[at - 1] == '\n') && memcmp(text + at, marker, marker_length) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
        *cursor = at + marker_length;
    }
    size_t at = *cursor;
    while (at < length && text[at] == ' ')
        at += 1;
    if (at >= length || text[at] == '\n') return false;
    *start = at;
    while (at < length && text[at] != ' ' && text[at] != '\n')
        at += 1;
    *end = at;
    *cursor = at;
    return true;
}

static uint32_t smith_lang_written_slot(const char *text, size_t length) {
    return smith_lang_hash(text, length) & (smith_lang_written_slots - 1);
}

static SmithLangWritten *smith_lang_written_lookup(const SmithLang *lang, const char *text, size_t length) {
    uint32_t slot = smith_lang_written_slot(text, length);
    for (size_t probe = 0; probe < smith_lang_written_slots; probe++) { // the table is half empty at most
        uint32_t entry = lang->written_slots[slot];
        if (entry == 0) return nullptr;
        SmithLangWritten *found = (SmithLangWritten *)&lang->written[entry - 1];
        if (found->lowered.length == length && memcmp(smith_lang_str(found->lowered), text, length) == 0) return found;
        slot = (slot + 1) & (smith_lang_written_slots - 1);
    }
    return nullptr;
}

// The name `lowered` is written `written`. `prefix_length` is how much of `lowered` is the module's prefix (the rest is
// the member). A name the file binds itself (`direct`) wins over one reached through another module's imports.
static void smith_lang_written_add(SmithLang *lang, const char *lowered, size_t lowered_length, const char *written,
                                   size_t written_length, size_t prefix_length, SmithLangText module, bool direct) {
    SmithLangWritten *known = smith_lang_written_lookup(lang, lowered, lowered_length);
    if (known != nullptr) {
        if (direct && !known->direct) {
            known->written = smith_lang_store(lang, written, written_length);
            known->direct = true;
        }
        return;
    }
    if (lang->written_count >= smith_lang_written_max) return; // a table that is full costs only the spelling
    SmithLangWritten *entry = &lang->written[lang->written_count++];
    entry->lowered = smith_lang_store(lang, lowered, lowered_length);
    entry->written = smith_lang_store(lang, written, written_length);
    entry->prefix_length = (uint16_t)prefix_length;
    entry->module = module;
    entry->direct = direct;
    uint32_t slot = smith_lang_written_slot(lowered, lowered_length);
    for (size_t probe = 0; probe < smith_lang_written_slots; probe++) { // the table is half empty at most
        if (lang->written_slots[slot] == 0) break;
        slot = (slot + 1) & (smith_lang_written_slots - 1);
    }
    lang->written_slots[slot] = (uint32_t)lang->written_count;
}

// The exports a module's header lists (`// smith-names:`), written through `binding` (`box`), or, for a module reached
// only through another's imports, by their whole module name (`geometry::box`).
static void smith_lang_written_from_header(SmithLang *lang, const char *text, size_t length, const char *module,
                                           const char *bound, bool direct) {
    char prefix[smith_lang_name_max];
    size_t prefix_length = 0;
    for (const char *c = module; *c != 0 && prefix_length + 2 < sizeof prefix; c++) { // the module's name
        if (*c == ':') {
            if (c[1] == ':') c += 1;
            prefix[prefix_length++] = '_';
        } else {
            prefix[prefix_length++] = *c;
        }
    }
    prefix[prefix_length++] = '_';
    SmithLangText module_text = smith_lang_store(lang, module, strlen(module));
    size_t cursor = 0;
    size_t start = 0;
    size_t end = 0;
    while (smith_lang_header_word(text, length, "// smith-names:", &cursor, &start, &end)) { // each export
        size_t member = end - start;
        char lowered[smith_lang_name_max];
        char written[smith_lang_name_max];
        if (prefix_length + member + 1 > sizeof lowered || strlen(bound) + 2 + member + 1 > sizeof written) continue;
        memcpy(lowered, prefix, prefix_length);
        memcpy(lowered + prefix_length, text + start, member);
        int written_length = snprintf(written, sizeof written, "%s::%.*s", bound, (int)member, text + start);
        smith_lang_written_add(lang, lowered, prefix_length + member, written, (size_t)written_length, prefix_length,
                               module_text, direct);
    }
}

static void smith_lang_module_to_path(const char *module, char *path, size_t capacity);

// An import: the module's exports, and those of the modules it imports in their turn (a signature can name them).
static void smith_lang_written_import(SmithLang *lang, const SmithHost *host, const char *text, size_t length,
                                      const char *module, const char *bound) {
    smith_lang_written_from_header(lang, text, length, module, bound, true);
    if (host->read_header == nullptr) return;
    char seen[smith_lang_transitive_max][128];
    size_t count = 0;
    snprintf(seen[count++], sizeof seen[0], "%s", module);
    const char *current_text = text;
    uint32_t current_length = (uint32_t)length;
    for (size_t at = 0; at < count; at++) { // each module is read once
        if (at > 0) {
            char path[smith_lang_name_max];
            smith_lang_module_to_path(seen[at], path, sizeof path);
            if (!host->read_header(host->context, path, &current_text, &current_length)) continue;
            smith_lang_written_from_header(lang, current_text, current_length, seen[at], seen[at], false);
        }
        size_t cursor = 0;
        size_t start = 0;
        size_t end = 0;
        while (smith_lang_header_word(current_text, current_length, "// smith-imports:", &cursor, &start, &end)) {
            bool known = end - start >= sizeof seen[0];
            for (size_t i = 0; i < count && !known; i++)
                known = strlen(seen[i]) == end - start && memcmp(seen[i], current_text + start, end - start) == 0;
            if (known || count >= smith_lang_transitive_max) continue;
            snprintf(seen[count++], sizeof seen[0], "%.*s", (int)(end - start), current_text + start);
        }
    }
}

// Reads the header again (the cycle check read others) for the names it lists, bound as `bind`.
static void smith_lang_written_read(SmithLang *lang, const SmithHost *host, const char *header, const char *module,
                                    const char *bind, size_t bind_length) {
    char bound[smith_lang_name_max];
    snprintf(bound, sizeof bound, "%.*s", (int)bind_length, bind);
    const char *text = nullptr;
    uint32_t length = 0;
    if (host->read_header != nullptr && host->read_header(host->context, header, &text, &length)) {
        smith_lang_written_import(lang, host, text, length, module, bound);
    }
}

// `x::member` was written: the name is known, declared or not.
static void smith_lang_written_access(SmithLang *lang, const SmithLangBinding *binding, size_t lowered_length,
                                      const char *member, size_t member_length) {
    char written[smith_lang_name_max];
    int length =
        snprintf(written, sizeof written, "%s::%.*s", smith_lang_str(binding->name), (int)member_length, member);
    if (length <= 0 || (size_t)length >= sizeof written) return;
    smith_lang_written_add(lang, lang->tmp, lowered_length, written, (size_t)length, binding->prefix.length,
                           binding->module, true);
}

// ---- What the host can ask

size_t smith_lang_written_count(const SmithLang *lang) {
    return lang->written_count;
}

static void smith_lang_written_view(const SmithLangWritten *entry, SmithWrittenName *out) {
    out->lowered = smith_lang_str(entry->lowered);
    out->lowered_length = entry->lowered.length;
    out->written = smith_lang_str(entry->written);
    out->written_length = entry->written.length;
    out->member = out->lowered + entry->prefix_length;
    out->member_length = entry->lowered.length - entry->prefix_length;
    out->module = smith_lang_str(entry->module);
    out->module_length = entry->module.length;
    out->direct = entry->direct;
}

bool smith_lang_written_at(const SmithLang *lang, size_t index, SmithWrittenName *out) {
    if (index >= lang->written_count) return false;
    smith_lang_written_view(&lang->written[index], out);
    return true;
}

bool smith_lang_written_find(const SmithLang *lang, const char *lowered, size_t length, SmithWrittenName *out) {
    const SmithLangWritten *entry = smith_lang_written_lookup(lang, lowered, length);
    if (entry == nullptr) return false;
    smith_lang_written_view(entry, out);
    return true;
}

// ---- A completion after `x::`

static void smith_lang_note_qualifier(SmithLang *lang, const SmithLangBinding *binding, const SmithToken *cursor) {
    lang->qualifier_set = true;
    lang->qualifier_is_c = binding->is_c;
    lang->qualifier_location = cursor->location;
    lang->qualifier_binding = binding->name;
    lang->qualifier_prefix = binding->prefix;
    lang->qualifier_module = binding->module;
    lang->qualifier_partial = smith_lang_store(lang, cursor->partial != nullptr ? cursor->partial : "",
                                               cursor->partial != nullptr ? cursor->partial_length : 0);
}

bool smith_lang_qualifier(const SmithLang *lang, SmithQualifier *out) {
    if (!lang->qualifier_set) return false;
    out->binding = smith_lang_str(lang->qualifier_binding);
    out->prefix = lang->qualifier_is_c ? "" : smith_lang_str(lang->qualifier_prefix);
    out->module = lang->qualifier_is_c ? "" : smith_lang_str(lang->qualifier_module);
    out->partial = smith_lang_str(lang->qualifier_partial);
    out->partial_length = lang->qualifier_partial.length;
    out->location = lang->qualifier_location;
    out->is_c = lang->qualifier_is_c;
    return true;
}
