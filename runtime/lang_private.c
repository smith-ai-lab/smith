// The private names an exported generic's body uses (issue #75). An instance is compiled in the importer's file, so it
// needs what the body names of the module's private code: a function's or a variable's symbol, a type's or a constant's
// definition. Included by runtime/lang.c, after runtime/lang_stream.c.
//
//   - The header pass (a raw run) keeps every private declaration it reads, as the lowering wrote it: a function or a
//     variable as the declaration that carries its symbol (`int m_helper(int x) __asm__("_m::helper");`, without its
//     body or its initializer), a type or a `constexpr` in full. The body of an exported generic that names one marks
//     it (smith_lang_generic_private_name), and what the kept declarations name in turn is marked too.
//   - What was marked is the header's *instance section*, between `#ifdef SMITH_INSTANCES` and `#endif`: .smith
//     importers have the macro defined while they include the header (shim/hook.cpp), C and C++ never see the section.
//     The functions and variables of it are listed in a `// smith-linked:` line.
//   - The module's own compile reads that line from its header (smith_lang_linked_read) and gives those functions and
//     variables the symbol (the label `_m::helper`), where every other private one is `static`.
// A private name is still private to source: it is in no `// smith-names:` line, so an importer cannot write it.

static void smith_lang_priv_begin(SmithLang *lang) {
    lang->private_active = false;
    lang->priv_range = 0;
    if (!lang->tee || !lang->is_module || lang->priv_ranges_count >= smith_lang_priv_ranges_max) return;
    SmithLangPrivRange *range = &lang->priv_ranges[lang->priv_ranges_count];
    *range = (SmithLangPrivRange){.from = (uint32_t)lang->priv_used,
                                  .to = (uint32_t)lang->priv_used,
                                  .note_first = (uint32_t)lang->priv_notes_count};
    lang->priv_ranges_count += 1;
    lang->priv_range = (uint32_t)lang->priv_ranges_count;
    lang->private_active = true;
}

// The declaration is complete (or its text is: a function's prototype, a variable's `extern`).
static void smith_lang_priv_end(SmithLang *lang) {
    if (lang->priv_range != 0) {
        SmithLangPrivRange *range = &lang->priv_ranges[lang->priv_range - 1];
        range->to = (uint32_t)lang->priv_used;
        range->done = true;
    }
    lang->private_active = false;
    lang->priv_range = 0;
}

// The declaration is not kept after all (a function body the head did not give a prototype for).
static void smith_lang_priv_abandon(SmithLang *lang) {
    smith_lang_priv_end(lang);
    if (lang->priv_ranges_count > 0) lang->priv_ranges[lang->priv_ranges_count - 1].done = false;
}

// The declaration is a function or a variable with a symbol, or a type or constant with none.
static void smith_lang_priv_form(SmithLang *lang, uint8_t form, bool linked) {
    if (lang->priv_range == 0) return;
    lang->priv_ranges[lang->priv_range - 1].form = form;
    lang->priv_ranges[lang->priv_range - 1].linked = linked;
}

// A token of the private declaration being kept: its text, and the names it writes through an import.
static void smith_lang_priv_write(SmithLang *lang, const SmithToken *token) {
    if (lang->priv_range == 0 || token->kind == SmithTokenKind_Eof) return;
    SmithLangPrivRange *range = &lang->priv_ranges[lang->priv_range - 1];
    if (token->via != 0 && lang->priv_notes_count < smith_lang_priv_notes_max) {
        SmithLangPrivNote *note = &lang->priv_notes[lang->priv_notes_count++];
        note->via = token->via;
        note->tag = smith_lang_tag_before(lang->priv_text, lang->priv_used);
        note->name = smith_lang_store(lang, token->text, token->length);
        range->note_count += 1;
    }
    const char *text = token->header_text != nullptr ? token->header_text : token->text;
    size_t length = token->header_text != nullptr ? token->header_length : token->length;
    bool first = lang->priv_used == range->from;
    smith_lang_put(lang->priv_text, smith_lang_priv_text_max, &lang->priv_used, text, length,
                   !first && token->line_start, !first && token->space_before);
}

// A name the kept declaration declares: what the generics that name it find it by.
static void smith_lang_priv_name(SmithLang *lang, const char *name, size_t length, const char *cname,
                                 size_t cname_length) {
    if (lang->priv_range == 0 || lang->priv_names_count >= smith_lang_priv_names_max) return;
    SmithLangPrivName *entry = &lang->priv_names[lang->priv_names_count++];
    entry->name = smith_lang_store(lang, name, length);
    entry->cname = smith_lang_store(lang, cname, cname_length);
    entry->range = lang->priv_range - 1;
}

// Whether the kept text of a range writes the word.
static bool smith_lang_priv_writes(const SmithLang *lang, const SmithLangPrivRange *range, const char *word,
                                   size_t length) {
    const char *text = lang->priv_text;
    for (uint32_t at = range->from; at < range->to;) { // bounded by the text
        if (!(isalpha((unsigned char)text[at]) || text[at] == '_')) {
            at += 1;
            continue;
        }
        uint32_t end = at;
        while (end < range->to && (isalnum((unsigned char)text[end]) || text[end] == '_'))
            end += 1;
        if (end - at == length && memcmp(text + at, word, length) == 0) return true;
        at = end;
    }
    return false;
}

// Chooses the declarations the header's instance section holds: those the exported generics name, and those these
// name. The names they write through imports are noted for the includes of the header.
static void smith_lang_priv_resolve(SmithLang *lang) {
    if (lang->priv_resolved) return;
    lang->priv_resolved = true;
    for (size_t i = 0; i < lang->priv_names_count; i++) { // what a generic's body names
        const SmithLangPrivName *entry = &lang->priv_names[i];
        const SmithLangName *known = smith_lang_name_find(lang, smith_lang_str(entry->name), entry->name.length);
        if (known != nullptr && known->needed && lang->priv_ranges[entry->range].done) {
            lang->priv_ranges[entry->range].needed = true;
        }
    }
    for (size_t round = 0; round < lang->priv_ranges_count; round++) { // what those name, until nothing is added
        bool added = false;
        for (size_t i = 0; i < lang->priv_ranges_count; i++) {
            if (!lang->priv_ranges[i].needed) continue;
            for (size_t j = 0; j < lang->priv_names_count; j++) {
                const SmithLangPrivName *entry = &lang->priv_names[j];
                SmithLangPrivRange *other = &lang->priv_ranges[entry->range];
                if (other->needed || !other->done || entry->range == i) continue;
                if (smith_lang_priv_writes(lang, &lang->priv_ranges[i], smith_lang_str(entry->cname),
                                           entry->cname.length)) {
                    other->needed = true;
                    added = true;
                }
            }
        }
        if (!added) break;
    }
    for (size_t i = 0; i < lang->priv_ranges_count; i++) { // the names written through imports
        const SmithLangPrivRange *range = &lang->priv_ranges[i];
        for (uint32_t n = 0; range->needed && n < range->note_count; n++) {
            const SmithLangPrivNote *note = &lang->priv_notes[range->note_first + n];
            smith_lang_note_use(lang, note->via, smith_lang_str(note->name), note->name.length, note->tag);
        }
    }
}

// Whether an earlier declaration that is needed has the same text (a prototype and the definition's: one is enough).
static bool smith_lang_priv_repeated(const SmithLang *lang, size_t index) {
    const SmithLangPrivRange *range = &lang->priv_ranges[index];
    for (size_t i = 0; i < index; i++) {
        const SmithLangPrivRange *before = &lang->priv_ranges[i];
        if (before->needed && before->to - before->from == range->to - range->from &&
            memcmp(lang->priv_text + before->from, lang->priv_text + range->from, range->to - range->from) == 0) {
            return true;
        }
    }
    return false;
}

static bool smith_lang_priv_any(const SmithLang *lang) {
    for (size_t i = 0; i < lang->priv_ranges_count; i++) {
        if (lang->priv_ranges[i].needed) return true;
    }
    return false;
}

// The instance section: the declarations the exported generics need of the module's private code.
static size_t smith_lang_priv_section(SmithLang *lang, char *out, size_t capacity, size_t used) {
    smith_lang_priv_resolve(lang);
    if (!smith_lang_priv_any(lang)) return used;
    used = smith_lang_append(out, capacity, used, "#ifdef SMITH_INSTANCES\n");
    for (size_t i = 0; i < lang->priv_ranges_count; i++) { // in the order declared
        const SmithLangPrivRange *range = &lang->priv_ranges[i];
        if (!range->needed || smith_lang_priv_repeated(lang, i)) continue;
        used = smith_lang_append(out, capacity, used, "%.*s\n", (int)(range->to - range->from),
                                 lang->priv_text + range->from);
    }
    return smith_lang_append(out, capacity, used, "#endif\n");
}

// `// smith-linked: helper counter`: the private functions and variables the section declares, which the module's own
// compile gives a symbol.
static size_t smith_lang_priv_linked_line(const SmithLang *lang, char *out, size_t capacity, size_t used) {
    bool any = false;
    for (size_t i = 0; i < lang->priv_names_count; i++) {
        const SmithLangPrivName *entry = &lang->priv_names[i];
        const SmithLangPrivRange *range = &lang->priv_ranges[entry->range];
        if (!range->needed || !range->linked) continue;
        bool again = false;
        for (size_t j = 0; j < i; j++) { // a name once
            const SmithLangPrivName *before = &lang->priv_names[j];
            again =
                again || (lang->priv_ranges[before->range].needed && lang->priv_ranges[before->range].linked &&
                          before->name.length == entry->name.length &&
                          memcmp(smith_lang_str(before->name), smith_lang_str(entry->name), entry->name.length) == 0);
        }
        if (again) continue;
        used = smith_lang_append(out, capacity, used, any ? " %s" : "// smith-linked: %s", smith_lang_str(entry->name));
        any = true;
    }
    return any ? smith_lang_append(out, capacity, used, "\n") : used;
}

// `// smith-hidden: offset Pair helper`: the private names the section declares. They are in the importers' C, and no
// importer may write them (`m::helper` is an error there).
static size_t smith_lang_priv_hidden_line(const SmithLang *lang, char *out, size_t capacity, size_t used) {
    bool any = false;
    for (size_t i = 0; i < lang->priv_names_count; i++) {
        const SmithLangPrivName *entry = &lang->priv_names[i];
        if (!lang->priv_ranges[entry->range].needed) continue;
        bool again = false;
        for (size_t j = 0; j < i; j++) { // a name once
            const SmithLangPrivName *before = &lang->priv_names[j];
            again =
                again || (lang->priv_ranges[before->range].needed && before->name.length == entry->name.length &&
                          memcmp(smith_lang_str(before->name), smith_lang_str(entry->name), entry->name.length) == 0);
        }
        if (again) continue;
        used = smith_lang_append(out, capacity, used, any ? " %s" : "// smith-hidden: %s", smith_lang_str(entry->name));
        any = true;
    }
    return any ? smith_lang_append(out, capacity, used, "\n") : used;
}

// The names an imported header says it declares for instances only, as the lowering writes them.
static void smith_lang_hidden_read(SmithLang *lang, const char *text, size_t length, const char *prefix,
                                   size_t prefix_length) {
    size_t cursor = 0;
    size_t start = 0;
    size_t end = 0;
    while (smith_lang_header_word(text, length, "// smith-hidden:", &cursor, &start, &end)) {
        char lowered[smith_lang_name_max];
        if (prefix_length + (end - start) + 1 > sizeof lowered || lang->hidden_count >= smith_lang_hidden_max) continue;
        memcpy(lowered, prefix, prefix_length);
        memcpy(lowered + prefix_length, text + start, end - start);
        lang->hidden[lang->hidden_count++] = smith_lang_store(lang, lowered, prefix_length + (end - start));
    }
}

static bool smith_lang_is_hidden(const SmithLang *lang, const char *lowered, size_t length) {
    for (size_t i = 0; i < lang->hidden_count; i++) {
        if (lang->hidden[i].length == length && memcmp(smith_lang_str(lang->hidden[i]), lowered, length) == 0)
            return true;
    }
    return false;
}

// ---- The module's own compile
// ----------------------------------------------------------------------------------------------

// Reads the `// smith-linked:` line of the module's own header, once. A compile with no header on its path asks the
// host for the module's interface made from its source, which has the line too.
static void smith_lang_linked_read(SmithLang *lang, const SmithHost *host) {
    if (lang->linked_read || host->previous == nullptr || host->read_header == nullptr) return;
    lang->linked_read = true;
    char path[smith_lang_name_max];
    smith_lang_module_to_path(lang->module, path, sizeof path);
    const char *text = nullptr;
    uint32_t length = 0;
    if (!host->read_header(host->context, path, &text, &length)) return;
    static const char marker[] = "// smith-linked:";
    for (uint32_t i = 0; i + sizeof marker < length; i++) { // bounded by the header
        if ((i != 0 && text[i - 1] != '\n') || memcmp(text + i, marker, sizeof marker - 1) != 0) continue;
        uint32_t at = i + sizeof marker - 1;
        while (at < length && text[at] != '\n' && lang->linked_count < smith_lang_linked_max) {
            while (at < length && text[at] == ' ')
                at += 1;
            uint32_t start = at;
            while (at < length && text[at] != ' ' && text[at] != '\n')
                at += 1;
            if (at > start) lang->linked[lang->linked_count++] = smith_lang_store(lang, text + start, at - start);
        }
        return;
    }
}

static bool smith_lang_is_linked(const SmithLang *lang, const char *name, size_t length) {
    for (size_t i = 0; i < lang->linked_count; i++) {
        if (lang->linked[i].length == length && memcmp(smith_lang_str(lang->linked[i]), name, length) == 0) return true;
    }
    return false;
}
