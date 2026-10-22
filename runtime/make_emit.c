// Writing build.c: each target the Makefile builds becomes smith_object, smith_program, smith_step, smith_task or
// smith_alias, a variable a recipe uses as whole words becomes a named array, and an option becomes
// smith_option_value. Included by smith.c after make_graph.c.

constexpr size_t smith_make_lex_depth = 32;
constexpr size_t smith_make_body_size = (size_t)256 << 20;

typedef struct {
    const char *text; // the word as the shell reads it: unquoted, with its holes
    int32_t span;     // the longest whole-word use of a variable that starts here, or -1
} SmithMakeArg;

typedef struct {
    size_t var;
    size_t first; // the words first .. last - 1 are the variable's value
    size_t last;
} SmithMakeSpan2;

typedef struct {
    SmithMakeArg *args;
    size_t count;
    SmithMakeSpan2 *spans;
    size_t span_count;
} SmithMakeArgv;

// ---- Reading a command as the shell would

typedef struct {
    size_t var;
    size_t start;
    bool clean_start;
} SmithMakeOpen;

typedef struct {
    SmithMakeBuffer word;
    SmithMakeOpen stack[smith_make_lex_depth];
    size_t depth;
    bool in_word;
    SmithMakeArg *args;
    size_t count;
    SmithMakeSpan2 *spans;
    size_t span_count;
} SmithMakeLexer;

static void smith_make_lex_flush(SmithMakeLexer *lex) {
    if (!lex->in_word) return;
    lex->args[lex->count++] = (SmithMakeArg){smith_make_take(&lex->word, 0), -1};
    lex->word.length = 0;
    lex->in_word = false;
}

// A use mark begins at text[i] (the mark, a number, the separator): pushes it and returns where the separator is.
static size_t smith_make_lex_begin(SmithMakeLexer *lex, const char *text, size_t i, bool clean) {
    size_t var = 0;
    size_t j = i + 1;
    while (text[j] != smith_make_use_sep && text[j] != '\0') {
        var = var * 10 + (size_t)(text[j] - '0');
        j++;
    }
    if (lex->depth < smith_make_lex_depth) lex->stack[lex->depth++] = (SmithMakeOpen){var, lex->count, clean};
    return j;
}

static void smith_make_lex_end(SmithMakeLexer *lex, const char *text, size_t length, size_t i, bool unquoted) {
    if (lex->depth == 0) return;
    SmithMakeOpen open = lex->stack[--lex->depth];
    size_t j = i + 1;
    while (j < length && text[j] == smith_make_use_end)
        j++;
    bool clean_end = unquoted && (j >= length || smith_make_space(text[j]));
    size_t end = lex->in_word ? lex->count + 1 : lex->count;
    if (open.clean_start && clean_end && end > open.start)
        lex->spans[lex->span_count++] = (SmithMakeSpan2){open.var, open.start, end};
}

// One character outside quotes. Returns false for what the shell would do more with than split: a pipe, a redirect, a
// substitution, a glob.
static bool smith_make_lex_plain(SmithMakeLexer *lex, const char *text, size_t length, size_t *at, bool shell,
                                 char *quote) {
    size_t i = *at;
    char c = text[i];
    if (smith_make_space(c)) {
        smith_make_lex_flush(lex);
        return true;
    }
    if (shell) {
        if (strchr("|&;<>()`$*?[", c) != nullptr) return false;
        if ((c == '#' || c == '~') && !lex->in_word) return false;
        if (c == '\'' || c == '"') {
            *quote = c;
            lex->in_word = true;
            return true;
        }
        if (c == '\\') {
            if (i + 1 < length && text[i + 1] != '\n') {
                smith_make_put_char(&lex->word, text[i + 1]);
                lex->in_word = true;
            }
            *at = i + 1 < length ? i + 1 : i;
            return true;
        }
    }
    smith_make_put_char(&lex->word, c);
    lex->in_word = true;
    return true;
}

static bool smith_make_lex_quoted(SmithMakeLexer *lex, const char *text, size_t length, size_t *at, char *quote) {
    size_t i = *at;
    char c = text[i];
    if (c == *quote) {
        *quote = '\0';
    } else if (*quote == '"' && (c == '$' || c == '`')) {
        return false;
    } else if (*quote == '"' && c == '\\' && i + 1 < length && strchr("\"\\$`\n", text[i + 1]) != nullptr) {
        if (text[i + 1] != '\n') smith_make_put_char(&lex->word, text[i + 1]);
        *at = i + 1;
    } else {
        smith_make_put_char(&lex->word, c);
    }
    return true;
}

// Splits text into words. With shell, as sh does: quotes and backslashes removed; false for any other shell syntax.
static bool smith_make_lex(const char *text, bool shell, SmithMakeArgv *out) {
    size_t length = strlen(text);
    SmithMakeLexer lex = {};
    smith_make_buffer_init(&lex.word, length + 16);
    lex.args = smith_make_alloc((length + 2) * sizeof(SmithMakeArg));
    lex.spans = smith_make_alloc((length / 3 + 4) * sizeof(SmithMakeSpan2));
    char quote = '\0';
    for (size_t i = 0; i < length; i++) { // bounded by the length
        char c = text[i];
        if (c == smith_make_use_begin) {
            i = smith_make_lex_begin(&lex, text, i, !lex.in_word && quote == '\0');
        } else if (c == smith_make_use_end) {
            smith_make_lex_end(&lex, text, length, i, quote == '\0');
        } else if (quote == '\0') {
            if (!smith_make_lex_plain(&lex, text, length, &i, shell, &quote)) return false;
        } else if (!smith_make_lex_quoted(&lex, text, length, &i, &quote)) {
            return false;
        }
    }
    if (quote != '\0') return false;
    smith_make_lex_flush(&lex);
    for (size_t s = 0; s < lex.span_count; s++) { // the longest span that starts at a word is the one used
        SmithMakeArg *arg = &lex.args[lex.spans[s].first];
        if (arg->span < 0 || lex.spans[arg->span].last < lex.spans[s].last) arg->span = (int32_t)s;
    }
    *out = (SmithMakeArgv){lex.args, lex.count, lex.spans, lex.span_count};
    return true;
}

// ---- Names in C

// The C name of a Makefile variable's array: build_ and its name in lower case (the same rule as an option's).
static const char *smith_make_array_name(const SmithMakeVar *var) {
    return smith_make_ident(var->name);
}

// ---- Lists

typedef struct {
    SmithMakeBuffer *body;
    bool words_used; // build.c needs the helper that splits an option into words
} SmithMakeOutput;

static SmithMakeOutput smith_make_output;

// A unit is a piece of build.c that stands alone: an option, an array or what one target makes. A build.c that does not
// fit a function of 70 lines (readability-function-size in smith's .clang-tidy) is written as one function for each run
// of units, and a unit's variable (an option, an array, an object a program names) moves to the top of the file as a
// `static`, where each function reaches it.
constexpr size_t smith_make_units_max = 65536;

typedef struct {
    size_t start; // the unit's text is buffer[start .. end)
    size_t end;
    const char *type; // the variable it declares, if any, as `type name = ` at text[decl_at .. decl_at + decl_length)
    const char *name;
    size_t decl_at;
    size_t decl_length;
    bool maybe_unused; // nothing reads the variable (an option the build never uses): [[maybe_unused]]
} SmithMakeUnit;

typedef struct {
    SmithMakeBuffer *buffer;
    SmithMakeUnit *units;
    size_t count;
} SmithMakeUnits;

static SmithMakeUnits *smith_make_unit_list; // the list that units are being made in

static void smith_make_units_init(SmithMakeUnits *list, SmithMakeBuffer *buffer) {
    list->buffer = buffer;
    list->units = smith_make_alloc(smith_make_units_max * sizeof(SmithMakeUnit));
    list->count = 0;
}

static void smith_make_unit_begin(SmithMakeUnits *list) {
    SMITH_ASSERT(list->count < smith_make_units_max);
    smith_make_unit_list = list;
    list->units[list->count] = (SmithMakeUnit){.start = list->buffer->length, .end = list->buffer->length};
}

// Ends the unit; one that wrote nothing is not kept.
static void smith_make_unit_end(void) {
    SmithMakeUnits *list = smith_make_unit_list;
    SmithMakeUnit *unit = &list->units[list->count];
    unit->end = list->buffer->length;
    if (unit->end > unit->start) list->count++;
}

// The declaration `    type name = ` of the unit's variable; the caller writes what it is set to.
static void smith_make_put_decl(SmithMakeBuffer *out, const char *type, const char *name, bool maybe_unused) {
    SmithMakeUnit *unit = &smith_make_unit_list->units[smith_make_unit_list->count];
    unit->type = type;
    unit->name = name;
    unit->maybe_unused = maybe_unused;
    unit->decl_at = out->length;
    smith_make_put_format(out, "    %s%s%s%s = ", maybe_unused ? "[[maybe_unused]] " : "", type,
                          type[strlen(type) - 1] == '*' ? "" : " ", name);
    unit->decl_length = out->length - unit->decl_at;
}

// Every array a list named, in order, so that an array is defined after the arrays its definition names.
constexpr size_t smith_make_ref_log_max = 65536;
static size_t smith_make_ref_log[smith_make_ref_log_max];
static size_t smith_make_ref_log_count;

static bool smith_make_is_hole_word(const char *text) {
    size_t length = strlen(text);
    size_t number = 0;
    return length > 0 && text[0] == smith_make_hole_begin && smith_make_hole_at(text, length, 0, &number) == length;
}

// The whole words of a variable as a span says, if the variable can be named as an array: its value is the same
// wherever it is used.
static bool smith_make_array_ok(size_t var_index, const SmithMakeArgv *argv, size_t span) {
    const SmithMakeVar *var = &smith_make.vars[var_index];
    if (var->knob || !var->defined) return false;
    if (!var->simple) {
        const char *text = var->value;
        for (const char *c = strchr(text, '$'); c != nullptr; c = strchr(c + 1, '$')) {
            if (c[1] == '$') {
                c++;
                continue;
            }
            const char *name = c[1] == '(' || c[1] == '{' ? c + 2 : c + 1;
            if (strchr("@<^+?*|0123456789", *name) != nullptr) return false;
        }
    }
    const SmithMakeSpan2 *s = &argv->spans[span];
    if (s->last - s->first > 1) return true;
    // one flag, or a list build.c reads when it runs, is worth naming; one word such as CC is not
    return argv->args[s->first].text[0] == '-' || smith_make_is_hole_word(argv->args[s->first].text);
}

typedef enum { SmithMakeItem_Literal, SmithMakeItem_Words, SmithMakeItem_Array } SmithMakeItemKind;

// One element of a list expression, written in C.
static void smith_make_put_item(SmithMakeBuffer *out, const char *text) {
    size_t length = strlen(text);
    smith_make_put_text(out, smith_make_render(text, length));
}

// The C expression (a list) of the words args[indices[0 .. count)]. A run of words that is a variable's whole value is
// the variable's array. An empty list is `(const char *[]){nullptr}`.
static const char *smith_make_list(const SmithMakeArgv *argv, const size_t *indices, size_t count) {
    SmithMakeBuffer out;
    smith_make_buffer_init(&out, 1024 + count * 512);
    size_t parts = 0;
    bool open = false; // a compound literal of words is being written
    for (size_t k = 0; k < count; k++) {
        const SmithMakeArg *arg = &argv->args[indices[k]];
        bool array = false;
        if (arg->span >= 0) {
            const SmithMakeSpan2 *s = &argv->spans[arg->span];
            size_t length = s->last - s->first;
            array = k + length <= count && smith_make_array_ok(s->var, argv, (size_t)arg->span);
            for (size_t j = 0; array && j < length; j++) {
                if (indices[k + j] != indices[k] + j) array = false;
            }
            if (array) {
                if (open) smith_make_put_text(&out, ", nullptr}");
                open = false;
                smith_make_put_text(&out, parts > 0 ? ", " : "");
                smith_make_put_text(&out, smith_make_array_name(&smith_make.vars[s->var]));
                smith_make.vars[s->var].used_as_array = true;
                if (smith_make_ref_log_count < smith_make_ref_log_max)
                    smith_make_ref_log[smith_make_ref_log_count++] = s->var;
                parts++;
                k += length - 1;
                continue;
            }
        }
        if (smith_make_is_hole_word(arg->text)) {
            if (open) smith_make_put_text(&out, ", nullptr}");
            open = false;
            smith_make_put_text(&out, parts > 0 ? ", " : "");
            smith_make_put_format(&out, "make_words(b, %s)", smith_make_render(arg->text, strlen(arg->text)));
            smith_make_output.words_used = true;
            parts++;
            continue;
        }
        if (!open) {
            smith_make_put_text(&out, parts > 0 ? ", (const char *[]){" : "(const char *[]){");
            open = true;
            parts++;
        } else {
            smith_make_put_text(&out, ", ");
        }
        smith_make_put_item(&out, arg->text);
    }
    if (open) smith_make_put_text(&out, ", nullptr}");
    if (parts == 0) return "(const char *[]){nullptr}";
    if (parts == 1) return smith_make_take(&out, 0);
    return smith_make_format("smith_cat(b, %.*s, nullptr)", (int)out.length, out.data);
}

// The list of every word of argv.
static const char *smith_make_list_all(const SmithMakeArgv *argv) {
    size_t *indices = smith_make_alloc((argv->count + 1) * sizeof(size_t));
    for (size_t i = 0; i < argv->count; i++)
        indices[i] = i;
    return smith_make_list(argv, indices, argv->count);
}

// A list of plain strings (file names, which may hold holes): no arrays.
static const char *smith_make_names(const char *const *names, size_t count) {
    SmithMakeArg *args = smith_make_alloc((count + 1) * sizeof(SmithMakeArg));
    for (size_t i = 0; i < count; i++)
        args[i] = (SmithMakeArg){names[i], -1};
    SmithMakeArgv argv = {args, count, nullptr, 0};
    return smith_make_list_all(&argv);
}
