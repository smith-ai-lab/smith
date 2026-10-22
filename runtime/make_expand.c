// The expander of make's `$(...)`: variables, substitution references, the automatic variables and the functions of
// issue #44. It is a machine with a stack of frames and one buffer, not a recursion: a text frame scans its text, and
// meeting `$(` it pushes a call frame, which expands its arguments by pushing a text frame for each, then computes. The
// buffer is a stack too: a call's result goes where its first argument began. Included by smith.c after make.c.

constexpr size_t smith_make_depth_max = 96;
constexpr size_t smith_make_args_max = 16;
constexpr uint64_t smith_make_steps_max = (uint64_t)1 << 32;

typedef struct {
    const char *p;
    size_t n;
} SmithMakeSpan;

typedef enum {
    SmithMakeFn_Var,
    SmithMakeFn_Mark,
    SmithMakeFn_Subst,
    SmithMakeFn_Patsubst,
    SmithMakeFn_Strip,
    SmithMakeFn_Findstring,
    SmithMakeFn_Filter,
    SmithMakeFn_FilterOut,
    SmithMakeFn_Sort,
    SmithMakeFn_Word,
    SmithMakeFn_Wordlist,
    SmithMakeFn_Words,
    SmithMakeFn_Firstword,
    SmithMakeFn_Lastword,
    SmithMakeFn_Join,
    SmithMakeFn_Dir,
    SmithMakeFn_Notdir,
    SmithMakeFn_Suffix,
    SmithMakeFn_Basename,
    SmithMakeFn_Addsuffix,
    SmithMakeFn_Addprefix,
    SmithMakeFn_Wildcard,
    SmithMakeFn_Abspath,
    SmithMakeFn_Realpath,
    SmithMakeFn_Shell,
    SmithMakeFn_Eval,
    SmithMakeFn_Error,
    SmithMakeFn_Warning,
    SmithMakeFn_Info,
    SmithMakeFn_Origin,
    SmithMakeFn_Flavor,
    SmithMakeFn_Value,
    SmithMakeFn_If,
    SmithMakeFn_Or,
    SmithMakeFn_And,
    SmithMakeFn_Foreach,
    SmithMakeFn_Call,
} SmithMakeFn;

typedef struct {
    const char *name;
    SmithMakeFn fn;
    size_t min_args;
    size_t max_args;
    bool holes_ok; // the function works on a value that has a hole in it
} SmithMakeFunction;

static const SmithMakeFunction smith_make_functions[] = {
    {"subst", SmithMakeFn_Subst, 3, 3, false},
    {"patsubst", SmithMakeFn_Patsubst, 3, 3, false},
    {"strip", SmithMakeFn_Strip, 1, 1, true},
    {"findstring", SmithMakeFn_Findstring, 2, 2, false},
    {"filter", SmithMakeFn_Filter, 2, 2, true},
    {"filter-out", SmithMakeFn_FilterOut, 2, 2, true},
    {"sort", SmithMakeFn_Sort, 1, 1, false},
    {"word", SmithMakeFn_Word, 2, 2, false},
    {"wordlist", SmithMakeFn_Wordlist, 3, 3, false},
    {"words", SmithMakeFn_Words, 1, 1, false},
    {"firstword", SmithMakeFn_Firstword, 1, 1, false},
    {"lastword", SmithMakeFn_Lastword, 1, 1, false},
    {"join", SmithMakeFn_Join, 2, 2, true},
    {"dir", SmithMakeFn_Dir, 1, 1, false},
    {"notdir", SmithMakeFn_Notdir, 1, 1, false},
    {"suffix", SmithMakeFn_Suffix, 1, 1, false},
    {"basename", SmithMakeFn_Basename, 1, 1, false},
    {"addsuffix", SmithMakeFn_Addsuffix, 2, 2, true},
    {"addprefix", SmithMakeFn_Addprefix, 2, 2, true},
    {"wildcard", SmithMakeFn_Wildcard, 1, 1, false},
    {"abspath", SmithMakeFn_Abspath, 1, 1, false},
    {"realpath", SmithMakeFn_Realpath, 1, 1, false},
    {"shell", SmithMakeFn_Shell, 1, 1, false},
    {"eval", SmithMakeFn_Eval, 1, 1, true},
    {"error", SmithMakeFn_Error, 1, 1, true},
    {"warning", SmithMakeFn_Warning, 1, 1, true},
    {"info", SmithMakeFn_Info, 1, 1, true},
    {"origin", SmithMakeFn_Origin, 1, 1, true},
    {"flavor", SmithMakeFn_Flavor, 1, 1, true},
    {"value", SmithMakeFn_Value, 1, 1, true},
    {"if", SmithMakeFn_If, 2, 3, true},
    {"or", SmithMakeFn_Or, 1, smith_make_args_max, true},
    {"and", SmithMakeFn_And, 1, smith_make_args_max, true},
    {"foreach", SmithMakeFn_Foreach, 3, 3, true},
    {"call", SmithMakeFn_Call, 1, smith_make_args_max, true},
};

// A variable's state, saved while a foreach or a call lends it a value.
typedef struct {
    SmithMakeVar *var;
    SmithMakeVar state;
} SmithMakeSaved;

typedef struct {
    bool is_call;
    bool top;     // a whole variable used here is marked as a use (make.c)
    bool pending; // an argument was pushed and its end is not recorded yet
    SmithMakeFn fn;
    size_t stage;
    const char *src; // a text frame: what it scans
    size_t pos;
    size_t end;
    size_t start; // a call frame: where its result goes in the buffer
    SmithMakeSpan raw[smith_make_args_max];
    size_t raw_count;
    size_t done[smith_make_args_max]; // the buffer's length once argument i was expanded
    size_t cursor;
    size_t aux;
    const char *text; // foreach: the list; var: the substitution's patterns
    SmithMakeSaved saved[smith_make_args_max + 1];
    size_t saved_count;
} SmithMakeFrame;

typedef struct {
    SmithMakeFrame frames[smith_make_depth_max];
    size_t depth;
    SmithMakeBuffer buffer;
    SmithMakeBuffer scratch;
} SmithMakeMachine;

static SmithMakeMachine smith_make_machine;

// The automatic variables of the rule being expanded, if any.
typedef struct {
    bool active;
    const char *target;
    const char *first;
    const char *prerequisites; // $^, each once
    const char *all;           // $+
    const char *newer;         // $?
    const char *order_only;    // $|
    const char *stem;
} SmithMakeAuto;

static SmithMakeAuto smith_make_auto;

static const char *smith_make_render(const char *text, size_t length);

// ---- C text

static void smith_make_c_escape(SmithMakeBuffer *out, const char *text, size_t length, bool percent) {
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') {
            smith_make_put_char(out, '\\');
            smith_make_put_char(out, (char)c);
        } else if (c == '\n') {
            smith_make_put_text(out, "\\n");
        } else if (c == '\t') {
            smith_make_put_text(out, "\\t");
        } else if (c < 0x20 || c == 0x7f) {
            smith_make_put_format(out, "\\%03o", (unsigned)c);
        } else if (c == '%' && percent) {
            smith_make_put_text(out, "%%");
        } else if (c == '?') {
            smith_make_put_text(out, "\\?"); // never a trigraph
        } else {
            smith_make_put_char(out, (char)c);
        }
    }
}

// The C string literal of text.
static const char *smith_make_literal(const char *text, size_t length) {
    SmithMakeBuffer out;
    smith_make_buffer_init(&out, length * 4 + 16);
    smith_make_put_char(&out, '"');
    smith_make_c_escape(&out, text, length, false);
    smith_make_put_char(&out, '"');
    return smith_make_take(&out, 0);
}

// A C expression of type const char * for text, which may hold holes: a literal, a hole's expression, or smith_fmt.
static const char *smith_make_render(const char *text, size_t length) {
    text = smith_make_unmark(smith_make_dup(text, length));
    length = strlen(text);
    if (!smith_make_has_hole(text, length)) return smith_make_literal(text, length);
    SmithMakeBuffer format;
    SmithMakeBuffer arguments;
    smith_make_buffer_init(&format, length * 4 + 16);
    smith_make_buffer_init(&arguments, length * 64 + 64);
    size_t holes = 0;
    size_t only = 0;
    size_t i = 0;
    while (i < length) { // bounded by the length
        if (text[i] == smith_make_hole_begin) {
            size_t number = 0;
            i = smith_make_hole_at(text, length, i, &number);
            smith_make_put_text(&format, "%s");
            smith_make_put_text(&arguments, ", ");
            smith_make_put_text(&arguments, smith_make.holes[number].expression);
            holes++;
            only = number;
        } else {
            size_t run = i;
            while (run < length && text[run] != smith_make_hole_begin)
                run++;
            smith_make_c_escape(&format, text + i, run - i, true);
            i = run;
        }
    }
    if (holes == 1 && format.length == 2) return smith_make.holes[only].expression;
    return smith_make_format("smith_fmt(b, \"%.*s\"%.*s)", (int)format.length, format.data, (int)arguments.length,
                             arguments.data);
}

// ---- Spans, words

static bool smith_make_span_empty(SmithMakeSpan span) {
    size_t from = 0;
    return smith_make_trim_length(span.p, span.n, &from) == 0;
}

static const char *smith_make_span_text(SmithMakeSpan span) {
    return smith_make_dup(span.p, span.n);
}

static const char *smith_make_span_trimmed(SmithMakeSpan span) {
    size_t from = 0;
    size_t size = smith_make_trim_length(span.p, span.n, &from);
    return smith_make_dup(span.p + from, size);
}

// Each word of text, separated by one space.
static void smith_make_put_word(SmithMakeBuffer *out, const char *word, size_t length) {
    if (out->length > 0 && out->data[out->length - 1] != ' ') smith_make_put_char(out, ' ');
    smith_make_put(out, word, length);
}

// ---- Variables

static const char *const smith_make_env_names[] = {"HOME", "PATH", "USER", "SHELL",   "PWD",      "TMPDIR",
                                                   "LANG", "TERM", "OS",   "LOGNAME", "HOSTNAME", "XDG_CACHE_HOME"};

static bool smith_make_is_env_name(const char *name, size_t length) {
    for (size_t i = 0; i < sizeof smith_make_env_names / sizeof smith_make_env_names[0]; i++) {
        if (strlen(smith_make_env_names[i]) == length && memcmp(smith_make_env_names[i], name, length) == 0)
            return true;
    }
    return false;
}

// The name of a knob's C variable and its option: letters, digits and underscores.
static const char *smith_make_c_name(const char *name) {
    size_t length = strlen(name);
    char *out = smith_make_alloc(length + 1);
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)name[i];
        out[i] = (char)((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : '_');
    }
    out[length] = '\0';
    return out;
}

// Makes the variable a knob whose option is -DNAME: its value is the hole for the option's C variable.
static void smith_make_make_knob(SmithMakeVar *var, const char *fallback) {
    const char *text = smith_make_hole_text(smith_make_format("option_%s", smith_make_c_name(var->name)));
    var->knob = true;
    var->option = true;
    var->fallback = fallback;
    smith_make_var_set(var, text, text, true);
}

// A variable the Makefile never assigns: an environment variable is a hole for it; any other is a knob (`make X=1`),
// unless its name was made by an expansion ($(BINARY_$(1))), which is how a Makefile asks whether a setting exists.
static void smith_make_undefined(SmithMakeMachine *m, const char *name, size_t length, bool computed) {
    if (length == 0 || smith_make.no_knobs) return;
    if (computed && !smith_make_is_env_name(name, length)) return;
    if (smith_make.generated_include) { // reported once for each name
        static const char *reported[256];
        static size_t reported_count;
        bool seen = false;
        for (size_t i = 0; i < reported_count && !seen; i++)
            seen = strlen(reported[i]) == length && memcmp(reported[i], name, length) == 0;
        if (!seen && reported_count < 256) {
            reported[reported_count++] = smith_make_dup(name, length);
            smith_make_report(
                "%.*s is empty here: a Makefile that this one generates may define it, and it is not read", (int)length,
                name);
        }
        return;
    }
    if (smith_make_is_env_name(name, length)) {
        const char *key = smith_make_dup(name, length);
        smith_make_put_text(&m->buffer, smith_make_hole_text(smith_make_format("smith_env(b, \"%s\", \"\")", key)));
        return;
    }
    SmithMakeVar *var = smith_make_var_get(name, length);
    smith_make_make_knob(var, "");
    smith_make_put_text(&m->buffer, var->value);
}

static bool smith_make_is_automatic(const char *name, size_t length) {
    if (!smith_make_auto.active || length == 0 || length > 2) return false;
    if (strchr("@<^+?*|", name[0]) == nullptr) return false;
    return length == 1 || name[1] == 'D' || name[1] == 'F';
}

static const char *smith_make_automatic_text(char which) {
    const SmithMakeAuto *a = &smith_make_auto;
    switch (which) {
    case '@':
        return a->target;
    case '<':
        return a->first;
    case '^':
        return a->prerequisites;
    case '+':
        return a->all;
    case '?':
        return a->newer;
    case '*':
        return a->stem;
    default:
        return a->order_only;
    }
}

static void smith_make_put_dir(SmithMakeBuffer *out, const char *word, size_t length, bool directory) {
    const char *slash = nullptr;
    for (size_t i = 0; i < length; i++) {
        if (word[i] == '/') slash = word + i;
    }
    if (directory) {
        if (slash == nullptr) {
            smith_make_put_text(out, ".");
        } else {
            smith_make_put(out, word, slash == word ? 1 : (size_t)(slash - word));
        }
    } else {
        const char *name = slash == nullptr ? word : slash + 1;
        smith_make_put(out, name, length - (size_t)(name - word));
    }
}

static void smith_make_put_automatic(SmithMakeMachine *m, const char *name, size_t length) {
    const char *text = smith_make_automatic_text(name[0]);
    if (text == nullptr) return;
    if (length == 1) {
        smith_make_put_text(&m->buffer, text);
        return;
    }
    size_t at = 0;
    size_t start = 0;
    size_t size = 0;
    size_t total = strlen(text);
    bool first = true;
    while (smith_make_next_word(text, total, &at, &start, &size)) { // bounded by the length
        if (!first) smith_make_put_char(&m->buffer, ' ');
        smith_make_put_dir(&m->buffer, text + start, size, name[1] == 'D');
        first = false;
    }
}

static SmithMakeFrame *smith_make_push(SmithMakeMachine *m) {
    if (m->depth >= smith_make_depth_max) {
        smith_make_report("an expansion nests more than %zu deep (a variable that refers to itself?)",
                          smith_make_depth_max);
        return nullptr;
    }
    SmithMakeFrame *frame = &m->frames[m->depth++];
    frame->is_call = false;
    frame->top = false;
    frame->pending = false;
    frame->stage = 0;
    frame->pos = 0;
    frame->raw_count = 0;
    frame->cursor = 0;
    frame->saved_count = 0;
    frame->text = nullptr;
    return frame;
}

static void smith_make_push_text(SmithMakeMachine *m, const char *text, size_t length, bool top) {
    SmithMakeFrame *frame = smith_make_push(m);
    if (frame == nullptr) return;
    frame->src = text;
    frame->end = length;
    frame->top = top;
}

// A mark around a variable's expansion (the uses of a recursive variable): begin now, end when its text is done.
static void smith_make_push_mark(SmithMakeMachine *m, const SmithMakeVar *var) {
    SmithMakeFrame *frame = smith_make_push(m);
    if (frame == nullptr) return;
    frame->is_call = true;
    frame->fn = SmithMakeFn_Mark;
    frame->start = m->buffer.length;
    frame->aux = smith_make_var_index(var);
    smith_make_put_format(&m->buffer, "%c%zu%c", smith_make_use_begin, frame->aux, smith_make_use_sep);
    frame->cursor = m->buffer.length;
}

// Appends the value of the variable `name` (or pushes the frames that will).
static void smith_make_lookup(SmithMakeMachine *m, const char *name, size_t length, bool top, bool computed) {
    size_t from = 0;
    length = smith_make_trim_length(name, length, &from);
    name += from;
    if (smith_make_is_automatic(name, length)) {
        smith_make_put_automatic(m, name, length);
        return;
    }
    SmithMakeVar *var = smith_make_var_find(name, length);
    if (var == nullptr || !var->defined) {
        smith_make_undefined(m, name, length, computed);
        return;
    }
    if (var->simple) {
        bool mark = top && !var->knob && var->marked[0] != '\0';
        if (mark) {
            smith_make_put_format(&m->buffer, "%c%zu%c", smith_make_use_begin, smith_make_var_index(var),
                                  smith_make_use_sep);
            smith_make_put_text(&m->buffer, var->marked);
            smith_make_put_char(&m->buffer, smith_make_use_end);
        } else {
            smith_make_put_text(&m->buffer, var->value);
        }
        return;
    }
    if (var->value[0] == '\0') return;
    if (top) smith_make_push_mark(m, var);
    smith_make_push_text(m, var->value, strlen(var->value), top);
}

// ---- Calls

static const SmithMakeFunction *smith_make_function_named(const char *name, size_t length) {
    for (size_t i = 0; i < sizeof smith_make_functions / sizeof smith_make_functions[0]; i++) {
        if (strlen(smith_make_functions[i].name) == length && memcmp(smith_make_functions[i].name, name, length) == 0)
            return &smith_make_functions[i];
    }
    return nullptr;
}

// If text starts with a function's name and a blank, that function, and where its arguments begin.
static const SmithMakeFunction *smith_make_function_at(const char *text, size_t length, size_t *args_at) {
    size_t i = 0;
    while (i < length && ((text[i] >= 'a' && text[i] <= 'z') || text[i] == '-'))
        i++;
    if (i == 0 || i >= length || !smith_make_blank(text[i])) return nullptr;
    const SmithMakeFunction *function = smith_make_function_named(text, i);
    if (function == nullptr) return nullptr;
    while (i < length && smith_make_blank(text[i]))
        i++;
    *args_at = i;
    return function;
}

// Splits text at the commas outside nested brackets (of the kind that opened the reference), into at most max spans.
static size_t smith_make_split(const char *text, size_t length, size_t max, char open, SmithMakeSpan *out) {
    char close = open == '(' ? ')' : '}';
    size_t count = 0;
    size_t from = 0;
    size_t depth = 0;
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        if (c == open) {
            depth++;
        } else if (c == close && depth > 0) {
            depth--;
        } else if (c == ',' && depth == 0 && count + 1 < max) {
            out[count++] = (SmithMakeSpan){text + from, i - from};
            from = i + 1;
        }
    }
    out[count++] = (SmithMakeSpan){text + from, length - from};
    return count;
}

static void smith_make_start_call(SmithMakeMachine *m, bool top, const char *text, size_t length, char open) {
    size_t args_at = 0;
    const SmithMakeFunction *function = smith_make_function_at(text, length, &args_at);
    if (function == nullptr && memchr(text, '$', length) == nullptr && memchr(text, ':', length) == nullptr) {
        smith_make_lookup(m, text, length, top, false);
        return;
    }
    SmithMakeFrame *frame = smith_make_push(m);
    if (frame == nullptr) return;
    frame->is_call = true;
    frame->top = top;
    frame->start = m->buffer.length;
    if (function == nullptr) {
        frame->fn = SmithMakeFn_Var;
        frame->raw[0] = (SmithMakeSpan){text, length};
        frame->raw_count = 1;
        return;
    }
    frame->fn = function->fn;
    frame->raw_count = smith_make_split(text + args_at, length - args_at, function->max_args, open, frame->raw);
    if (frame->raw_count < function->min_args) {
        smith_make_report("$(%s) takes %zu arguments", function->name, function->min_args);
        for (size_t i = frame->raw_count; i < function->min_args; i++)
            frame->raw[i] = (SmithMakeSpan){"", 0};
        frame->raw_count = function->min_args;
    }
}

static void smith_make_step_reference(SmithMakeMachine *m, SmithMakeFrame *f, char open) {
    size_t at = f->pos;
    char close = open == '(' ? ')' : '}';
    size_t depth = 1;
    size_t j = at + 1;
    while (j < f->end && depth > 0) { // bounded by the text
        if (f->src[j] == open) {
            depth++;
        } else if (f->src[j] == close) {
            depth--;
        }
        j++;
    }
    if (depth > 0) {
        smith_make_report("a $%c is not closed", open);
        smith_make_put(&m->buffer, f->src + at - 1, f->end - at + 1);
        m->depth--;
        return;
    }
    f->pos = j;
    smith_make_start_call(m, f->top, f->src + at + 1, j - at - 2, open);
}

static void smith_make_step_text(SmithMakeMachine *m, SmithMakeFrame *f) {
    size_t from = f->pos;
    size_t i = from;
    while (i < f->end && f->src[i] != '$')
        i++;
    smith_make_put(&m->buffer, f->src + from, i - from);
    if (i + 1 >= f->end) {
        m->depth--;
        return;
    }
    char c = f->src[i + 1];
    f->pos = i + 1;
    if (c == '$') {
        smith_make_put_char(&m->buffer, '$');
        f->pos = i + 2;
    } else if (c == '(' || c == '{') {
        smith_make_step_reference(m, f, c);
    } else {
        f->pos = i + 2;
        smith_make_lookup(m, f->src + i + 1, 1, f->top, false);
    }
}

// ---- Calls: stages

// The expanded argument i of a call frame, in the buffer.
static SmithMakeSpan smith_make_arg(const SmithMakeMachine *m, const SmithMakeFrame *f, size_t i) {
    size_t begin = i == 0 ? f->start : f->done[i - 1];
    return (SmithMakeSpan){m->buffer.data + begin, f->done[i] - begin};
}

// Records that the argument being expanded is done; false when there is none pending.
static void smith_make_settle(const SmithMakeMachine *m, SmithMakeFrame *f) {
    if (!f->pending) return;
    f->done[f->stage - 1] = m->buffer.length;
    f->pending = false;
}

// Starts expanding raw argument `stage` of the frame (as a text frame above it).
static void smith_make_expand_arg(SmithMakeMachine *m, SmithMakeFrame *f, size_t i) {
    SmithMakeSpan raw = f->raw[i];
    f->stage = i + 1;
    f->pending = true;
    smith_make_push_text(m, raw.p, raw.n, false);
}

// The result of a call is in the scratch buffer: it replaces everything from the call's start.
static void smith_make_finish(SmithMakeMachine *m, SmithMakeFrame *f) {
    m->buffer.length = f->start;
    smith_make_put(&m->buffer, m->scratch.data, m->scratch.length);
    m->depth--;
}

static void smith_make_restore(SmithMakeFrame *f) {
    for (size_t i = f->saved_count; i > 0; i--)
        *f->saved[i - 1].var = f->saved[i - 1].state;
    f->saved_count = 0;
}

// Lends the variable a simple value until the frame restores it.
static void smith_make_lend(SmithMakeFrame *f, const char *name, const char *value) {
    SMITH_ASSERT(f->saved_count < smith_make_args_max + 1);
    SmithMakeVar *var = smith_make_var_get(name, strlen(name));
    f->saved[f->saved_count++] = (SmithMakeSaved){var, *var};
    var->value = value;
    var->marked = value;
    var->simple = true;
    var->defined = true;
    var->knob = false;
}

// ---- The functions

// The words of an argument, a pool array.
static SmithMakeWord *smith_make_arg_words(SmithMakeSpan span, size_t *count) {
    return smith_make_words(span.p, span.n, count);
}

static void smith_make_fn_subst(const SmithMakeSpan *args, SmithMakeBuffer *out) {
    const char *from = args[0].p;
    size_t from_length = args[0].n;
    const char *text = args[2].p;
    size_t length = args[2].n;
    if (from_length == 0) {
        smith_make_put(out, text, length);
        return;
    }
    size_t i = 0;
    while (i < length) { // bounded by the length
        if (i + from_length <= length && memcmp(text + i, from, from_length) == 0) {
            smith_make_put(out, args[1].p, args[1].n);
            i += from_length;
        } else {
            smith_make_put_char(out, text[i++]);
        }
    }
}

// patsubst on each word: a word that matches the pattern becomes the replacement with the stem.
static void smith_make_patsubst(const char *pattern, size_t pattern_length, const char *replacement,
                                size_t replacement_length, SmithMakeSpan text, SmithMakeBuffer *out) {
    size_t at = 0;
    size_t start = 0;
    size_t size = 0;
    while (smith_make_next_word(text.p, text.n, &at, &start, &size)) { // bounded by the text
        const char *stem = nullptr;
        size_t stem_length = 0;
        if (smith_make_match(pattern, pattern_length, text.p + start, size, &stem, &stem_length) &&
            memchr(pattern, '%', pattern_length) != nullptr) {
            const char *word = smith_make_substitute(replacement, replacement_length, stem, stem_length);
            smith_make_put_word(out, word, strlen(word));
        } else if (memchr(pattern, '%', pattern_length) == nullptr && size == pattern_length &&
                   memcmp(text.p + start, pattern, size) == 0) {
            smith_make_put_word(out, replacement, replacement_length);
        } else {
            smith_make_put_word(out, text.p + start, size);
        }
    }
}

static void smith_make_fn_strip(const SmithMakeSpan *args, SmithMakeBuffer *out) {
    size_t at = 0;
    size_t start = 0;
    size_t size = 0;
    while (smith_make_next_word(args[0].p, args[0].n, &at, &start, &size))
        smith_make_put_word(out, args[0].p + start, size);
}

static void smith_make_fn_findstring(const SmithMakeSpan *args, SmithMakeBuffer *out) {
    if (args[0].n == 0 || args[0].n > args[1].n) {
        if (args[0].n == 0) smith_make_put(out, "", 0);
        return;
    }
    for (size_t i = 0; i + args[0].n <= args[1].n; i++) {
        if (memcmp(args[1].p + i, args[0].p, args[0].n) == 0) {
            smith_make_put(out, args[0].p, args[0].n);
            return;
        }
    }
}

static bool smith_make_matches_any(const SmithMakeWord *patterns, size_t count, const char *word, size_t length) {
    for (size_t i = 0; i < count; i++) {
        const char *stem = nullptr;
        size_t stem_length = 0;
        if (smith_make_match(patterns[i].text, patterns[i].length, word, length, &stem, &stem_length)) return true;
    }
    return false;
}

// filter, or filter-out (keep false): a text that is one hole (`$(shell uname -s)`) is kept or dropped by a condition
// that build.c evaluates, written as a hole whose value is the word or nothing.
static void smith_make_fn_filter_hole(const SmithMakeWord *patterns, size_t pattern_count, SmithMakeSpan text,
                                      bool keep, SmithMakeBuffer *out) {
    const char *trimmed = smith_make_span_trimmed(text);
    size_t length = strlen(trimmed);
    size_t number = 0;
    if (trimmed[0] != smith_make_hole_begin || smith_make_hole_at(trimmed, length, 0, &number) != length) {
        smith_make_report("$(%s) of several words that depend on the platform or an option is not supported",
                          keep ? "filter" : "filter-out");
        smith_make_put(out, text.p, text.n);
        return;
    }
    SmithMakeBuffer condition;
    smith_make_buffer_init(&condition, (pattern_count + 1) * 512);
    const char *expression = smith_make.holes[number].expression;
    for (size_t i = 0; i < pattern_count; i++) {
        if (memchr(patterns[i].text, '%', patterns[i].length) != nullptr) {
            smith_make_report("a pattern with %% in $(filter) of a value that depends on the platform or an option "
                              "is not supported");
        }
        if (i > 0) smith_make_put_text(&condition, " || ");
        smith_make_put_format(&condition, "strcmp(%s, %s) == 0", expression,
                              smith_make_literal(patterns[i].text, patterns[i].length));
    }
    if (pattern_count == 0) smith_make_put_text(&condition, "false");
    const char *choice = smith_make_format("((%.*s) ? %s : \"\")", (int)condition.length, condition.data, expression);
    if (!keep) {
        choice = smith_make_format("((%.*s) ? \"\" : %s)", (int)condition.length, condition.data, expression);
    }
    smith_make_put_text(out, smith_make_hole_text(choice));
}

static void smith_make_fn_filter(const SmithMakeSpan *args, bool keep, SmithMakeBuffer *out) {
    size_t pattern_count = 0;
    SmithMakeWord *patterns = smith_make_arg_words(args[0], &pattern_count);
    if (smith_make_has_hole(args[1].p, args[1].n)) {
        smith_make_fn_filter_hole(patterns, pattern_count, args[1], keep, out);
        return;
    }
    size_t at = 0;
    size_t start = 0;
    size_t size = 0;
    while (smith_make_next_word(args[1].p, args[1].n, &at, &start, &size)) { // bounded by the text
        bool matches = smith_make_matches_any(patterns, pattern_count, args[1].p + start, size);
        if (matches == keep) smith_make_put_word(out, args[1].p + start, size);
    }
}

static int smith_make_compare_words(const void *a, const void *b) {
    const SmithMakeWord *x = a;
    const SmithMakeWord *y = b;
    size_t common = x->length < y->length ? x->length : y->length;
    int order = memcmp(x->text, y->text, common);
    if (order != 0) return order;
    return x->length < y->length ? -1 : (x->length > y->length ? 1 : 0);
}

static void smith_make_fn_sort(const SmithMakeSpan *args, SmithMakeBuffer *out) {
    size_t count = 0;
    SmithMakeWord *words = smith_make_arg_words(args[0], &count);
    qsort(words, count, sizeof *words, smith_make_compare_words);
    for (size_t i = 0; i < count; i++) {
        if (i > 0 && smith_make_compare_words(&words[i], &words[i - 1]) == 0) continue;
        smith_make_put_word(out, words[i].text, words[i].length);
    }
}

static size_t smith_make_number(SmithMakeSpan span, bool *ok) {
    const char *text = smith_make_span_trimmed(span);
    char *end = nullptr;
    unsigned long value = strtoul(text, &end, 10);
    *ok = text[0] != '\0' && *end == '\0' && text[0] != '-';
    return (size_t)value;
}

static void smith_make_fn_word(const SmithMakeSpan *args, SmithMakeBuffer *out) {
    bool ok = false;
    size_t n = smith_make_number(args[0], &ok);
    size_t count = 0;
    SmithMakeWord *words = smith_make_arg_words(args[1], &count);
    if (!ok || n == 0) {
        smith_make_report("$(word) takes a number from 1");
        return;
    }
    if (n <= count) smith_make_put(out, words[n - 1].text, words[n - 1].length);
}

static void smith_make_fn_wordlist(const SmithMakeSpan *args, SmithMakeBuffer *out) {
    bool ok_from = false;
    bool ok_to = false;
    size_t from = smith_make_number(args[0], &ok_from);
    size_t to = smith_make_number(args[1], &ok_to);
    size_t count = 0;
    SmithMakeWord *words = smith_make_arg_words(args[2], &count);
    if (!ok_from || !ok_to || from == 0) {
        smith_make_report("$(wordlist) takes numbers from 1");
        return;
    }
    for (size_t i = from; i <= to && i <= count; i++)
        smith_make_put_word(out, words[i - 1].text, words[i - 1].length);
}

static void smith_make_fn_join(const SmithMakeSpan *args, SmithMakeBuffer *out) {
    size_t left_count = 0;
    size_t right_count = 0;
    SmithMakeWord *left = smith_make_arg_words(args[0], &left_count);
    SmithMakeWord *right = smith_make_arg_words(args[1], &right_count);
    size_t most = left_count > right_count ? left_count : right_count;
    for (size_t i = 0; i < most; i++) {
        if (i > 0) smith_make_put_char(out, ' ');
        if (i < left_count) smith_make_put(out, left[i].text, left[i].length);
        if (i < right_count) smith_make_put(out, right[i].text, right[i].length);
    }
}

// dir, notdir, suffix and basename on each word.
static void smith_make_fn_path(SmithMakeFn fn, const SmithMakeSpan *args, SmithMakeBuffer *out) {
    size_t at = 0;
    size_t start = 0;
    size_t size = 0;
    while (smith_make_next_word(args[0].p, args[0].n, &at, &start, &size)) { // bounded by the text
        const char *word = args[0].p + start;
        const char *slash = nullptr;
        const char *dot = nullptr;
        for (size_t i = 0; i < size; i++) {
            if (word[i] == '/') {
                slash = word + i;
                dot = nullptr;
            } else if (word[i] == '.') {
                dot = word + i;
            }
        }
        if (out->length > 0 && out->data[out->length - 1] != ' ') smith_make_put_char(out, ' ');
        if (fn == SmithMakeFn_Dir) {
            if (slash == nullptr) {
                smith_make_put_text(out, "./");
            } else {
                smith_make_put(out, word, (size_t)(slash - word) + 1);
            }
        } else if (fn == SmithMakeFn_Notdir) {
            const char *name = slash == nullptr ? word : slash + 1;
            smith_make_put(out, name, size - (size_t)(name - word));
        } else if (fn == SmithMakeFn_Suffix) {
            if (dot != nullptr) smith_make_put(out, dot, size - (size_t)(dot - word));
        } else {
            smith_make_put(out, word, dot == nullptr ? size : (size_t)(dot - word));
        }
    }
}

static void smith_make_fn_affix(const SmithMakeSpan *args, bool prefix, SmithMakeBuffer *out) {
    size_t at = 0;
    size_t start = 0;
    size_t size = 0;
    while (smith_make_next_word(args[1].p, args[1].n, &at, &start, &size)) { // bounded by the text
        if (out->length > 0 && out->data[out->length - 1] != ' ') smith_make_put_char(out, ' ');
        if (prefix) smith_make_put(out, args[0].p, args[0].n);
        smith_make_put(out, args[1].p + start, size);
        if (!prefix) smith_make_put(out, args[0].p, args[0].n);
    }
}

static void smith_make_fn_wildcard(const SmithMakeSpan *args, SmithMakeBuffer *out) {
    size_t count = 0;
    SmithMakeWord *patterns = smith_make_arg_words(args[0], &count);
    for (size_t i = 0; i < count; i++) {
        glob_t found;
        const char *pattern = smith_make_word_text(patterns[i]);
        if (glob(pattern, 0, nullptr, &found) != 0) {
            globfree(&found);
            continue;
        }
        for (size_t k = 0; k < found.gl_pathc; k++) {
            smith_make_put_word(out, found.gl_pathv[k], strlen(found.gl_pathv[k]));
        }
        globfree(&found);
    }
}

// An absolute, normalised path: the project's folder is a hole, so the path is right wherever build.c runs.
static void smith_make_normalise(const char *path, SmithMakeBuffer *out) {
    size_t length = strlen(path);
    size_t keep[512];
    size_t kept = 0;
    size_t base = out->length;
    size_t at = path[0] == '/' ? 1 : 0;
    if (path[0] != '/') smith_make_put_text(out, smith_make_hole_text("b->root"));
    while (at <= length) { // bounded by the length
        size_t end = at;
        while (end < length && path[end] != '/')
            end++;
        size_t size = end - at;
        if (size == 2 && path[at] == '.' && path[at + 1] == '.') {
            if (kept > 0) out->length = keep[--kept];
        } else if (size > 0 && !(size == 1 && path[at] == '.') && kept < 512) {
            keep[kept++] = out->length;
            smith_make_put_char(out, '/');
            smith_make_put(out, path + at, size);
        }
        at = end + 1;
    }
    if (out->length == base) smith_make_put_char(out, '/');
}

static void smith_make_fn_abspath(const SmithMakeSpan *args, SmithMakeBuffer *out) {
    size_t at = 0;
    size_t start = 0;
    size_t size = 0;
    while (smith_make_next_word(args[0].p, args[0].n, &at, &start, &size)) { // bounded by the text
        if (out->length > 0 && out->data[out->length - 1] != ' ') smith_make_put_char(out, ' ');
        smith_make_normalise(smith_make_dup(args[0].p + start, size), out);
    }
}

static const char *smith_make_platform_command(const char *command) {
    const char *trimmed = smith_make_trim(command);
    static const char *const nulls[] = {" 2>/dev/null", " 2> /dev/null", " 2>&1"};
    char *copy = smith_make_dup(trimmed, strlen(trimmed));
    for (size_t i = 0; i < sizeof nulls / sizeof nulls[0]; i++) {
        size_t cut = strlen(nulls[i]);
        size_t length = strlen(copy);
        if (length >= cut && strcmp(copy + length - cut, nulls[i]) == 0) copy[length - cut] = '\0';
    }
    if (strcmp(copy, "uname") == 0 || strcmp(copy, "uname -s") == 0) return "b->os";
    if (strcmp(copy, "uname -m") == 0 || strcmp(copy, "arch") == 0) return "b->arch";
    if (strcmp(copy, "pwd") == 0) return "b->root";
    if (strcmp(copy, "sysctl -n hw.ncpu") == 0 || strcmp(copy, "nproc") == 0 ||
        strcmp(copy, "getconf _NPROCESSORS_ONLN") == 0)
        return "smith_fmt(b, \"%d\", b->cpus)";
    return nullptr;
}

// Runs `sh -c command` and appends what it writes to out (at most 1 MiB). False if it cannot start.
static bool smith_make_run_shell(const char *command, SmithMakeBuffer *out) {
    int fds[2];
    if (pipe(fds) != 0) return false;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    char *const argv[] = {(char *)"sh", (char *)"-c", (char *)command, nullptr};
    pid_t pid = 0;
    fflush(nullptr);
    int started = posix_spawn(&pid, "/bin/sh", &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (started != 0) {
        close(fds[0]);
        return false;
    }
    char chunk[4096];
    size_t total = 0;
    ssize_t got = 0;
    while (total < ((size_t)1 << 20) && (got = read(fds[0], chunk, sizeof chunk)) > 0) { // at most 1 MiB
        smith_make_put(out, chunk, (size_t)got);
        total += (size_t)got;
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return true;
}

// $(shell command): the platform is a hole; anything else runs now and is frozen at its output.
static void smith_make_fn_shell(const SmithMakeSpan *args, SmithMakeBuffer *out) {
    const char *command = smith_make_span_text(args[0]);
    const char *platform = smith_make_platform_command(command);
    if (platform != nullptr) {
        smith_make_put_text(out, smith_make_hole_text(platform));
        return;
    }
    size_t base = out->length;
    if (!smith_make_run_shell(command, out)) {
        smith_make_report("$(shell %s) cannot run", command);
        return;
    }
    for (size_t i = base; i < out->length; i++) {
        if (out->data[i] == '\n' || out->data[i] == '\r') out->data[i] = ' ';
    }
    while (out->length > base && out->data[out->length - 1] == ' ')
        out->length--;
    smith_make_report("$(shell %s) was run now and is frozen at its output; build.c does not run it", command);
}

static void smith_make_fn_origin(SmithMakeFn fn, const SmithMakeSpan *args, SmithMakeBuffer *out) {
    const char *name = smith_make_span_trimmed(args[0]);
    const SmithMakeVar *var = smith_make_var_find(name, strlen(name));
    bool defined = var != nullptr && var->defined;
    if (fn == SmithMakeFn_Origin) {
        smith_make_put_text(out, !defined ? "undefined"
                                          : (var->knob ? "command line" : (var->built_in ? "default" : "file")));
    } else if (fn == SmithMakeFn_Flavor) {
        smith_make_put_text(out, !defined ? "undefined" : (var->simple ? "simple" : "recursive"));
    } else if (defined) {
        smith_make_put_text(out, var->value);
    }
}

// $(eval text) adds the text to the Makefile as lines: the reader takes it after the line that holds the call, which is
// when make reads it for a line that is only the call (the usual $(foreach ...$(eval $(call template,...))).
static void smith_make_fn_eval(const SmithMakeSpan *args) {
    if (!smith_make.reading) {
        smith_make_report("$(eval) in a recipe is not supported");
    } else if (smith_make.eval_count >= sizeof smith_make.evals / sizeof smith_make.evals[0]) {
        smith_make_report("too many $(eval) calls in one line");
    } else if (!smith_make_span_empty(args[0])) {
        smith_make.evals[smith_make.eval_count++] = smith_make_span_text(args[0]);
    }
}

static void smith_make_fn_message(SmithMakeFn fn, const SmithMakeSpan *args) {
    if (fn == SmithMakeFn_Error)
        smith_make_report("$(error) is reached: %s", smith_make_span_trimmed(args[0]));
    else if (fn == SmithMakeFn_Eval)
        smith_make_fn_eval(args);
}

static void smith_make_compute_more(SmithMakeFn fn, const SmithMakeSpan *args, SmithMakeBuffer *out) {
    switch (fn) {
    case SmithMakeFn_Join:
        smith_make_fn_join(args, out);
        break;
    case SmithMakeFn_Dir:
    case SmithMakeFn_Notdir:
    case SmithMakeFn_Suffix:
    case SmithMakeFn_Basename:
        smith_make_fn_path(fn, args, out);
        break;
    case SmithMakeFn_Addsuffix:
    case SmithMakeFn_Addprefix:
        smith_make_fn_affix(args, fn == SmithMakeFn_Addprefix, out);
        break;
    case SmithMakeFn_Wildcard:
        smith_make_fn_wildcard(args, out);
        break;
    case SmithMakeFn_Abspath:
    case SmithMakeFn_Realpath:
        smith_make_fn_abspath(args, out);
        break;
    case SmithMakeFn_Shell:
        smith_make_fn_shell(args, out);
        break;
    case SmithMakeFn_Origin:
    case SmithMakeFn_Flavor:
    case SmithMakeFn_Value:
        smith_make_fn_origin(fn, args, out);
        break;
    case SmithMakeFn_Error:
    case SmithMakeFn_Warning:
    case SmithMakeFn_Info:
    case SmithMakeFn_Eval:
        smith_make_fn_message(fn, args);
        break;
    default:
        break;
    }
}

static void smith_make_compute_words(SmithMakeFn fn, const SmithMakeSpan *args, SmithMakeBuffer *out) {
    size_t count = 0;
    switch (fn) {
    case SmithMakeFn_Words:
        smith_make_arg_words(args[0], &count);
        smith_make_put_format(out, "%zu", count);
        break;
    case SmithMakeFn_Firstword:
    case SmithMakeFn_Lastword: {
        SmithMakeWord *words = smith_make_arg_words(args[0], &count);
        if (count > 0) {
            SmithMakeWord word = fn == SmithMakeFn_Firstword ? words[0] : words[count - 1];
            smith_make_put(out, word.text, word.length);
        }
        break;
    }
    case SmithMakeFn_Word:
        smith_make_fn_word(args, out);
        break;
    case SmithMakeFn_Wordlist:
        smith_make_fn_wordlist(args, out);
        break;
    default:
        smith_make_compute_more(fn, args, out);
        break;
    }
}

static const SmithMakeFunction *smith_make_function_of(SmithMakeFn fn) {
    for (size_t i = 0; i < sizeof smith_make_functions / sizeof smith_make_functions[0]; i++) {
        if (smith_make_functions[i].fn == fn) return &smith_make_functions[i];
    }
    return nullptr;
}

// Runs an eager function on its expanded arguments, into the scratch buffer.
static void smith_make_compute(SmithMakeMachine *m, SmithMakeFrame *f) {
    SmithMakeSpan args[smith_make_args_max];
    for (size_t i = 0; i < f->raw_count; i++)
        args[i] = smith_make_arg(m, f, i);
    const SmithMakeFunction *function = smith_make_function_of(f->fn);
    if (function != nullptr && !function->holes_ok) {
        for (size_t i = 0; i < f->raw_count; i++) {
            if (smith_make_has_hole(args[i].p, args[i].n))
                smith_make_report("a value that depends on the platform or an option reaches $(%s), which keeps it "
                                  "as it is",
                                  function->name);
        }
    }
    SmithMakeBuffer *out = &m->scratch;
    out->length = 0;
    switch (f->fn) {
    case SmithMakeFn_Subst:
        smith_make_fn_subst(args, out);
        break;
    case SmithMakeFn_Patsubst:
        smith_make_patsubst(args[0].p, args[0].n, args[1].p, args[1].n, args[2], out);
        break;
    case SmithMakeFn_Strip:
        smith_make_fn_strip(args, out);
        break;
    case SmithMakeFn_Findstring:
        smith_make_fn_findstring(args, out);
        break;
    case SmithMakeFn_Filter:
    case SmithMakeFn_FilterOut:
        smith_make_fn_filter(args, f->fn == SmithMakeFn_Filter, out);
        break;
    case SmithMakeFn_Sort:
        smith_make_fn_sort(args, out);
        break;
    default:
        smith_make_compute_words(f->fn, args, out);
        break;
    }
    smith_make_finish(m, f);
}

// ---- The lazy functions

// $(var:from=to): the expansion of var, each word that ends with `from` with `to` instead (or a % pattern).
static void smith_make_step_var(SmithMakeMachine *m, SmithMakeFrame *f) {
    if (f->stage == 0) {
        smith_make_expand_arg(m, f, 0);
        return;
    }
    if (f->stage == 1) {
        smith_make_settle(m, f);
        const char *name = smith_make_take(&m->buffer, f->start);
        m->buffer.length = f->start;
        const char *colon = strchr(name, ':');
        const char *equals = colon != nullptr ? strchr(colon, '=') : nullptr;
        if (colon == nullptr || equals == nullptr) {
            bool top = f->top;
            m->depth--;
            smith_make_lookup(m, name, strlen(name), top, true);
            return;
        }
        f->text = smith_make_format("%.*s", (int)(equals - colon - 1), colon + 1);
        f->aux = (size_t)(equals - name + 1);
        f->cursor = 0;
        f->stage = 2;
        const char *replacement = smith_make_copy(name + f->aux);
        f->raw[1] = (SmithMakeSpan){replacement, strlen(replacement)};
        smith_make_lookup(m, name, (size_t)(colon - name), false, true);
        return;
    }
    SmithMakeSpan text = {m->buffer.data + f->start, m->buffer.length - f->start};
    if (smith_make_has_hole(text.p, text.n))
        smith_make_report("a value that depends on the platform or an option reaches a substitution reference, which "
                          "keeps it as it is");
    const char *pattern = f->text;
    SmithMakeBuffer *out = &m->scratch;
    out->length = 0;
    bool percent = strchr(pattern, '%') != nullptr;
    if (percent) {
        smith_make_patsubst(pattern, strlen(pattern), f->raw[1].p, f->raw[1].n, text, out);
    } else {
        const char *from = smith_make_format("%%%s", pattern);
        const char *to = smith_make_format("%%%.*s", (int)f->raw[1].n, f->raw[1].p);
        smith_make_patsubst(from, strlen(from), to, strlen(to), text, out);
    }
    smith_make_finish(m, f);
}

static void smith_make_step_if(SmithMakeMachine *m, SmithMakeFrame *f) {
    if (f->stage == 0) {
        smith_make_expand_arg(m, f, 0);
        return;
    }
    smith_make_settle(m, f);
    if (f->stage == 1) {
        SmithMakeSpan condition = smith_make_arg(m, f, 0);
        bool dynamic = smith_make_has_hole(condition.p, condition.n);
        if (!dynamic) {
            bool yes = !smith_make_span_empty(condition);
            m->buffer.length = f->start;
            bool top = f->top;
            SmithMakeSpan branch = yes ? f->raw[1] : (f->raw_count > 2 ? f->raw[2] : (SmithMakeSpan){"", 0});
            m->depth--;
            smith_make_push_text(m, branch.p, branch.n, top);
            return;
        }
        smith_make_expand_arg(m, f, 1);
        return;
    }
    if (f->stage == 2) {
        if (f->raw_count > 2) {
            smith_make_expand_arg(m, f, 2);
            return;
        }
        f->done[2] = f->done[1];
        f->stage = 3;
    }
    SmithMakeSpan condition = smith_make_arg(m, f, 0);
    SmithMakeSpan yes = smith_make_arg(m, f, 1);
    SmithMakeSpan no = smith_make_arg(m, f, 2);
    const char *test = smith_make_render(condition.p, condition.n);
    const char *expression = smith_make_format("((%s)[0] != '\\0' ? %s : %s)", test, smith_make_render(yes.p, yes.n),
                                               smith_make_render(no.p, no.n));
    m->buffer.length = f->start;
    smith_make_put_text(&m->buffer, smith_make_hole_text(expression));
    m->depth--;
}

// $(or a,b,...) and $(and a,b,...): the arguments one at a time, as far as the answer needs.
static void smith_make_step_logic(SmithMakeMachine *m, SmithMakeFrame *f) {
    smith_make_settle(m, f);
    bool is_or = f->fn == SmithMakeFn_Or;
    if (f->stage > 0) {
        SmithMakeSpan last = {m->buffer.data + f->start, m->buffer.length - f->start};
        if (smith_make_has_hole(last.p, last.n))
            smith_make_report("$(%s) of a value that depends on the platform or an option is not supported",
                              is_or ? "or" : "and");
        bool empty = smith_make_span_empty(last);
        if (is_or && !empty) {
            m->depth--;
            return;
        }
        if (!is_or && (empty || f->stage == f->raw_count)) {
            if (empty) m->buffer.length = f->start;
            m->depth--;
            return;
        }
        m->buffer.length = f->start;
        if (f->stage == f->raw_count) {
            m->depth--;
            return;
        }
    }
    smith_make_expand_arg(m, f, f->stage);
}

static void smith_make_step_foreach(SmithMakeMachine *m, SmithMakeFrame *f) {
    if (f->stage < 2) {
        smith_make_settle(m, f);
        smith_make_expand_arg(m, f, f->stage);
        return;
    }
    smith_make_settle(m, f);
    if (f->stage == 2) {
        SmithMakeSpan name = smith_make_arg(m, f, 0);
        SmithMakeSpan list = smith_make_arg(m, f, 1);
        f->text = smith_make_span_text(list);
        f->aux = 0;
        const char *variable = smith_make_span_trimmed(name);
        m->buffer.length = f->start;
        smith_make_lend(f, variable, "");
        f->stage = 3;
        f->cursor = 0;
    }
    size_t length = strlen(f->text);
    size_t at = f->aux;
    size_t start = 0;
    size_t size = 0;
    if (f->cursor > 0) smith_make_put_char(&m->buffer, ' ');
    if (!smith_make_next_word(f->text, length, &at, &start, &size)) {
        if (f->cursor > 0 && m->buffer.length > f->start && m->buffer.data[m->buffer.length - 1] == ' ')
            m->buffer.length--;
        smith_make_restore(f);
        m->depth--;
        return;
    }
    f->aux = at;
    f->cursor++;
    const char *word = smith_make_dup(f->text + start, size);
    f->saved[0].var->value = word;
    f->saved[0].var->marked = word;
    smith_make_push_text(m, f->raw[2].p, f->raw[2].n, f->top);
}

static void smith_make_step_call(SmithMakeMachine *m, SmithMakeFrame *f) {
    smith_make_settle(m, f);
    if (f->stage > f->raw_count) {
        smith_make_restore(f);
        m->depth--;
        return;
    }
    if (f->stage < f->raw_count) {
        smith_make_expand_arg(m, f, f->stage);
        return;
    }
    const char *name = smith_make_span_trimmed(smith_make_arg(m, f, 0));
    char number[32];
    for (size_t i = 0; i < f->raw_count; i++) {
        SmithMakeSpan arg = smith_make_arg(m, f, i);
        snprintf(number, sizeof number, "%zu", i);
        smith_make_lend(f, number, i == 0 ? name : smith_make_span_text(arg));
    }
    for (size_t i = f->raw_count; i < 10; i++) { // a parameter the call does not give is empty, not an outer call's
        snprintf(number, sizeof number, "%zu", i);
        smith_make_lend(f, number, "");
    }
    const SmithMakeVar *macro = smith_make_var_find(name, strlen(name));
    const char *body = macro != nullptr && macro->defined ? macro->value : "";
    if (macro == nullptr && smith_make_function_named(name, strlen(name)) != nullptr)
        smith_make_report("$(call %s) of a built-in function is not supported", name);
    m->buffer.length = f->start;
    f->stage = f->raw_count + 1;
    smith_make_push_text(m, body, strlen(body), f->top);
}

static void smith_make_step_mark(SmithMakeMachine *m, SmithMakeFrame *f) {
    if (m->buffer.length > f->cursor) {
        smith_make_put_char(&m->buffer, smith_make_use_end);
    } else {
        m->buffer.length = f->start; // an empty use leaves no mark
    }
    m->depth--;
}

static void smith_make_step_eager(SmithMakeMachine *m, SmithMakeFrame *f) {
    smith_make_settle(m, f);
    if (f->stage < f->raw_count) {
        smith_make_expand_arg(m, f, f->stage);
        return;
    }
    smith_make_compute(m, f);
}

static void smith_make_step_dispatch(SmithMakeMachine *m, SmithMakeFrame *f) {
    switch (f->fn) {
    case SmithMakeFn_Var:
        smith_make_step_var(m, f);
        break;
    case SmithMakeFn_Mark:
        smith_make_step_mark(m, f);
        break;
    case SmithMakeFn_If:
        smith_make_step_if(m, f);
        break;
    case SmithMakeFn_Or:
    case SmithMakeFn_And:
        smith_make_step_logic(m, f);
        break;
    case SmithMakeFn_Foreach:
        smith_make_step_foreach(m, f);
        break;
    case SmithMakeFn_Call:
        smith_make_step_call(m, f);
        break;
    default:
        smith_make_step_eager(m, f);
        break;
    }
}

// Expands text. With top set, a variable used as a whole is marked (make.c), for the arrays of build.c.
static const char *smith_make_expand(const char *text, size_t length, bool top) {
    SmithMakeMachine *m = &smith_make_machine;
    if (m->buffer.data == nullptr) {
        smith_make_buffer_init(&m->buffer, smith_make_buffer_size);
        smith_make_buffer_init(&m->scratch, smith_make_buffer_size);
    }
    m->buffer.length = 0;
    m->depth = 0;
    smith_make_push_text(m, text, length, top);
    for (uint64_t steps = 0; m->depth > 0; steps++) {
        if (steps > smith_make_steps_max) smith_die(smith_exit_usage, "make: an expansion does not end", nullptr);
        SmithMakeFrame *frame = &m->frames[m->depth - 1];
        if (frame->is_call)
            smith_make_step_dispatch(m, frame);
        else
            smith_make_step_text(m, frame);
    }
    return smith_make_take(&m->buffer, 0);
}

static const char *smith_make_expand_text(const char *text, bool top) {
    return smith_make_expand(text, strlen(text), top);
}
