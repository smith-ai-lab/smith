// Reading a Makefile: logical lines, assignments, `define`, `include`, conditionals (a test of the platform or an
// option keeps both branches) and rules. Included by smith.c after make_expand.c.

constexpr size_t smith_make_includes_max = 16;
constexpr size_t smith_make_conds_max = 64;
constexpr size_t smith_make_rules_max = 16384;
constexpr size_t smith_make_targets_max = 32768;
constexpr size_t smith_make_refs_max = 65536;
constexpr size_t smith_make_recipe_lines_max = 262144;
constexpr size_t smith_make_log_max = 8192;

typedef struct {
    const char *name;
    char *text;
    size_t length;
    size_t pos;
    uint32_t line;
    bool eval; // the text of an $(eval), not a file
} SmithMakeSource;

typedef struct {
    const char *text;
    SmithMakeWhere where;
} SmithMakeRecipeLine;

typedef struct {
    SmithMakeWhere where;
    const char **targets;
    size_t target_count;
    const char **prerequisites;
    size_t prerequisite_count;
    const char **order_only;
    size_t order_only_count;
    size_t recipe_first;
    size_t recipe_count;
    const char *guard; // a C condition, or nullptr: the rule is under a test of the platform or an option
    bool pattern;
    bool from_eval;      // made by an $(eval): the names of its variables were made by an expansion
    const char *stem;    // a static pattern rule's stem for its one target
    int64_t recipe_from; // the rule whose recipe lines these are, when a line made several rules; else -1
} SmithMakeRule;

typedef struct {
    uint32_t rule;
    uint32_t next; // index + 1 of the next reference, 0 for none
} SmithMakeRef;

typedef struct {
    const char *name;
    uint32_t first; // index + 1 into refs
    uint32_t last;
    bool phony;
    SmithMakeWhere where;
} SmithMakeTarget;

typedef struct {
    SmithMakeVar *var;
    SmithMakeVar state;
} SmithMakeLogEntry;

typedef struct {
    bool outer;   // the region around the conditional is read
    bool active;  // the branch being read is read
    bool taken;   // a branch was read already
    bool dynamic; // build.c decides
    bool chained; // an `else ifeq`: its endif is the parent's too
    bool in_else;
    const char *test;
    size_t log_start;
    size_t results_start;
    size_t results_end;
} SmithMakeCond;

typedef struct {
    SmithMakeSource sources[smith_make_includes_max];
    size_t source_count;
    SmithMakeCond conds[smith_make_conds_max];
    size_t cond_count;
    size_t dynamic_count;
    SmithMakeLogEntry log[smith_make_log_max];
    size_t log_count;
    SmithMakeLogEntry results[smith_make_log_max];
    size_t result_count;
    SmithMakeRule *rules;
    size_t rule_count;
    SmithMakeRef *refs;
    size_t ref_count;
    SmithMakeTarget *targets;
    size_t target_count;
    uint32_t *target_slots;
    uint32_t *patterns; // rules that are pattern rules, in order
    size_t pattern_count;
    SmithMakeRecipeLine *recipe_lines;
    size_t recipe_line_count;
    bool in_rule;
    int64_t current_rule; // -1 for none
    const char *default_goal;
    const char **generated_includes;
    size_t generated_count;
} SmithMakeReader;

static SmithMakeReader smith_make_reader;

static void smith_make_reader_init(void) {
    SmithMakeReader *r = &smith_make_reader;
    *r = (SmithMakeReader){.current_rule = -1};
    r->rules = smith_make_alloc(smith_make_rules_max * sizeof(SmithMakeRule));
    r->refs = smith_make_alloc(smith_make_refs_max * sizeof(SmithMakeRef));
    r->targets = smith_make_alloc(smith_make_targets_max * sizeof(SmithMakeTarget));
    r->target_slots = smith_make_alloc(smith_make_targets_max * 2 * sizeof(uint32_t));
    r->patterns = smith_make_alloc(smith_make_rules_max * sizeof(uint32_t));
    r->recipe_lines = smith_make_alloc(smith_make_recipe_lines_max * sizeof(SmithMakeRecipeLine));
    r->generated_includes = smith_make_new_list(1024);
}

// ---- Files and lines

static bool smith_make_slurp(const char *path, char **text, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return false;
    char *data = smith_make_alloc(smith_make_input_max + 1);
    size_t got = fread(data, 1, smith_make_input_max, file);
    bool whole = feof(file) != 0;
    fclose(file);
    if (!whole) smith_die(smith_exit_usage, "the Makefile is too large", path);
    data[got] = '\0';
    *text = data;
    *length = got;
    return true;
}

// The number of backslashes that end text, so that an odd number continues the line.
static size_t smith_make_trailing_backslashes(const char *text, size_t length) {
    size_t count = 0;
    while (count < length && text[length - 1 - count] == '\\')
        count++;
    return count;
}

// The next physical line of the source, without its newline (and carriage return); false at the end.
static bool smith_make_physical(SmithMakeSource *source, const char **line, size_t *length) {
    if (source->pos >= source->length) return false;
    const char *start = source->text + source->pos;
    const char *newline = memchr(start, '\n', source->length - source->pos);
    size_t size = newline != nullptr ? (size_t)(newline - start) : source->length - source->pos;
    source->pos += size + (newline != nullptr ? 1 : 0);
    source->line++;
    if (size > 0 && start[size - 1] == '\r') size--;
    *line = start;
    *length = size;
    return true;
}

typedef struct {
    const char *text;
    SmithMakeWhere where;
    bool recipe;
} SmithMakeLine;

// The buffers of the reader, made on first use in the pool (smith_make_reset forgets them).
static SmithMakeBuffer smith_make_joined;
static SmithMakeBuffer smith_make_define_body;

// The next logical line: a backslash and the newline join the next line (kept in a recipe line, one space otherwise).
static bool smith_make_next_line(SmithMakeSource *source, bool in_rule, SmithMakeLine *out) {
    const char *line = nullptr;
    size_t length = 0;
    if (smith_make_joined.data == nullptr) smith_make_buffer_init(&smith_make_joined, smith_make_input_max);
    smith_make_joined.length = 0;
    uint32_t first = source->line + 1;
    if (!smith_make_physical(source, &line, &length)) return false;
    bool recipe = in_rule && length > 0 && line[0] == '\t';
    smith_make_put(&smith_make_joined, line, length);
    for (size_t joined = 0; joined < smith_make_recipe_lines_max; joined++) { // bounded by the input
        if (smith_make_trailing_backslashes(smith_make_joined.data, smith_make_joined.length) % 2 == 0) break;
        const char *next = nullptr;
        size_t next_length = 0;
        if (!smith_make_physical(source, &next, &next_length)) {
            if (!recipe) smith_make_joined.length--;
            break;
        }
        if (recipe) {
            smith_make_put_char(&smith_make_joined, '\n');
            if (next_length > 0 && next[0] == '\t') {
                next++;
                next_length--;
            }
        } else {
            smith_make_joined.length--;
            while (smith_make_joined.length > 0 &&
                   smith_make_blank(smith_make_joined.data[smith_make_joined.length - 1]))
                smith_make_joined.length--;
            while (next_length > 0 && smith_make_blank(*next)) {
                next++;
                next_length--;
            }
            smith_make_put_char(&smith_make_joined, ' ');
        }
        smith_make_put(&smith_make_joined, next, next_length);
    }
    *out = (SmithMakeLine){smith_make_take(&smith_make_joined, 0), {source->name, first}, recipe};
    return true;
}

// The text without a comment: a `#` starts one unless a backslash is before it (`\#` is a `#`).
static const char *smith_make_strip_comment(const char *text) {
    size_t length = strlen(text);
    if (memchr(text, '#', length) == nullptr) return text;
    char *out = smith_make_alloc(length + 1);
    size_t used = 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '#') break;
        if (text[i] == '\\' && i + 1 < length && text[i + 1] == '#') {
            out[used++] = '#';
            i++;
        } else {
            out[used++] = text[i];
        }
    }
    out[used] = '\0';
    return out;
}

// ---- Directives

// Whether text starts with the keyword and then a blank or the end; *rest is what follows, blanks skipped.
static bool smith_make_keyword(const char *text, const char *keyword, const char **rest) {
    size_t length = strlen(keyword);
    if (strncmp(text, keyword, length) != 0) return false;
    if (text[length] != '\0' && !smith_make_blank(text[length])) return false;
    const char *at = text + length;
    while (smith_make_blank(*at))
        at++;
    *rest = at;
    return true;
}

// ifeq and ifneq may be followed directly by the opening parenthesis or quote.
static bool smith_make_if_keyword(const char *text, const char *keyword, const char **rest) {
    size_t length = strlen(keyword);
    if (strncmp(text, keyword, length) == 0 && (text[length] == '(' || text[length] == '"' || text[length] == '\'')) {
        *rest = text + length;
        return true;
    }
    return smith_make_keyword(text, keyword, rest);
}

// Where the first top-level character of `chars` is in text (outside $( ) and ${ }), or nullptr.
static const char *smith_make_find_top(const char *text, const char *chars) {
    size_t depth = 0;
    for (const char *c = text; *c != '\0'; c++) {
        if (*c == '$' && (c[1] == '(' || c[1] == '{')) {
            depth++;
            c++;
        } else if ((*c == ')' || *c == '}') && depth > 0) {
            depth--;
        } else if (depth == 0 && strchr(chars, *c) != nullptr) {
            return c;
        } else if (depth == 0 && *c == '$' && c[1] == '$') {
            c++;
        }
    }
    return nullptr;
}

// ---- Conditionals: the undo log of a branch that build.c decides

static void smith_make_log_touch(SmithMakeVar *var) {
    SmithMakeReader *r = &smith_make_reader;
    if (r->dynamic_count == 0) return;
    if (r->log_count >= smith_make_log_max)
        smith_die(smith_exit_usage, "make: too many assignments in a test", nullptr);
    r->log[r->log_count++] = (SmithMakeLogEntry){var, *var};
}

// The test of the conditionals around the line, all of them that build.c decides, as one C condition; nullptr for none.
static const char *smith_make_guard(void) {
    const SmithMakeReader *r = &smith_make_reader;
    if (r->dynamic_count == 0) return nullptr;
    SmithMakeBuffer out;
    smith_make_buffer_init(&out, 65536);
    for (size_t i = 0; i < r->cond_count; i++) {
        const SmithMakeCond *cond = &r->conds[i];
        if (!cond->dynamic) continue;
        if (out.length > 0) smith_make_put_text(&out, " && ");
        smith_make_put_format(&out, cond->in_else ? "!(%s)" : "(%s)", cond->test);
    }
    return smith_make_take(&out, 0);
}

static const char *smith_make_state_text(const SmithMakeVar *state, bool *value_ok) {
    *value_ok = state->defined;
    if (!state->defined) return "";
    if (state->simple) return state->value;
    return smith_make_expand_text(state->value, false);
}

// A variable that differs between the two branches becomes the conditional expression of its two values.
static void smith_make_merge_var(SmithMakeVar *var, const SmithMakeVar *a, const SmithMakeVar *b, const char *test) {
    bool a_ok = false;
    bool b_ok = false;
    const char *a_text = smith_make_state_text(a, &a_ok);
    const char *b_text = smith_make_state_text(b, &b_ok);
    if (a_ok == b_ok && strcmp(a_text, b_text) == 0) {
        *var = a_ok ? *a : *b;
        return;
    }
    const char *expression = smith_make_format("((%s) ? %s : %s)", test, smith_make_render(a_text, strlen(a_text)),
                                               smith_make_render(b_text, strlen(b_text)));
    const char *text = smith_make_hole_text(expression);
    *var = *b;
    var->source = "(depends on the platform or an option)";
    var->knob = false;
    smith_make_var_set(var, text, text, true);
}

static void smith_make_roll_back(size_t to) {
    SmithMakeReader *r = &smith_make_reader;
    for (size_t i = r->log_count; i > to; i--)
        *r->log[i - 1].var = r->log[i - 1].state;
    r->log_count = to;
}

static bool smith_make_in_results(size_t from, size_t to, const SmithMakeVar *var, size_t *at) {
    const SmithMakeReader *r = &smith_make_reader;
    for (size_t i = from; i < to; i++) {
        if (r->results[i].var == var) {
            *at = i;
            return true;
        }
    }
    return false;
}

// Collects the final state of each variable the log changed since `from`, once each, into the results.
static void smith_make_collect(size_t from) {
    SmithMakeReader *r = &smith_make_reader;
    for (size_t i = from; i < r->log_count; i++) {
        SmithMakeVar *var = r->log[i].var;
        bool seen = false;
        for (size_t k = from; k < i; k++) {
            if (r->log[k].var == var) seen = true;
        }
        if (seen) continue;
        if (r->result_count >= smith_make_log_max)
            smith_die(smith_exit_usage, "make: too many assignments in a test", nullptr);
        r->results[r->result_count++] = (SmithMakeLogEntry){var, *var};
    }
}

// `else` of a conditional build.c decides: keeps the first branch's results, and goes back to read the second.
static void smith_make_dynamic_else(SmithMakeCond *cond) {
    SmithMakeReader *r = &smith_make_reader;
    cond->results_start = r->result_count;
    smith_make_collect(cond->log_start);
    cond->results_end = r->result_count;
    smith_make_roll_back(cond->log_start);
}

// `endif` of a conditional build.c decides: merges the two branches' variables.
static void smith_make_dynamic_end(SmithMakeCond *cond) {
    SmithMakeReader *r = &smith_make_reader;
    size_t b_start = r->result_count;
    smith_make_collect(cond->log_start);
    size_t b_end = r->result_count;
    smith_make_roll_back(cond->log_start);
    size_t a_start = cond->results_start;
    size_t a_end = cond->in_else ? cond->results_end : a_start;
    if (!cond->in_else) { // no else: the first branch's changes are in the second collection
        a_start = b_start;
        a_end = b_end;
        b_end = b_start;
    }
    for (size_t i = a_start; i < a_end; i++) {
        SmithMakeVar *var = r->results[i].var;
        size_t at = 0;
        SmithMakeVar original = *var;
        const SmithMakeVar *b = smith_make_in_results(b_start, b_end, var, &at) ? &r->results[at].state : &original;
        smith_make_merge_var(var, &r->results[i].state, b, cond->test);
    }
    for (size_t i = b_start; i < b_end; i++) {
        SmithMakeVar *var = r->results[i].var;
        size_t at = 0;
        if (smith_make_in_results(a_start, a_end, var, &at)) continue;
        SmithMakeVar original = *var;
        smith_make_merge_var(var, &original, &r->results[i].state, cond->test);
    }
    r->result_count = cond->in_else ? cond->results_start : a_start;
}

// ---- Conditionals

// The two sides of `ifeq (a,b)` or `ifeq "a" "b"`.
static bool smith_make_split_test(const char *text, const char **left, const char **right) {
    size_t length = strlen(text);
    if (length >= 2 && text[0] == '(' && text[length - 1] == ')') {
        const char *inner = smith_make_dup(text + 1, length - 2);
        const char *comma = smith_make_find_top(inner, ",");
        if (comma == nullptr) return false;
        *left = smith_make_dup(inner, (size_t)(comma - inner));
        *right = smith_make_copy(comma + 1);
        return true;
    }
    if (text[0] == '"' || text[0] == '\'') {
        char quote = text[0];
        const char *end = strchr(text + 1, quote);
        if (end == nullptr) return false;
        *left = smith_make_dup(text + 1, (size_t)(end - text - 1));
        const char *rest = end + 1;
        while (smith_make_blank(*rest))
            rest++;
        if (*rest != '"' && *rest != '\'') return false;
        const char *last = strchr(rest + 1, *rest);
        if (last == nullptr) return false;
        *right = smith_make_dup(rest + 1, (size_t)(last - rest - 1));
        return true;
    }
    return false;
}

typedef enum { SmithMakeTest_False, SmithMakeTest_True, SmithMakeTest_Dynamic } SmithMakeTest;

// ifeq/ifneq; for a test of the platform or an option, `*test` is the C condition that is true when the branch is read.
static SmithMakeTest smith_make_test_eq(const char *args, bool negate, const char **test) {
    const char *left = nullptr;
    const char *right = nullptr;
    if (!smith_make_split_test(args, &left, &right)) {
        smith_make_report("a test that is not ifeq (a,b) or ifeq \"a\" \"b\"");
        return SmithMakeTest_False;
    }
    const char *a = smith_make_trim(smith_make_unmark(smith_make_expand_text(left, false)));
    const char *b = smith_make_trim(smith_make_unmark(smith_make_expand_text(right, false)));
    if (!smith_make_has_hole(a, strlen(a)) && !smith_make_has_hole(b, strlen(b)))
        return (strcmp(a, b) == 0) != negate ? SmithMakeTest_True : SmithMakeTest_False;
    *test = smith_make_format("strcmp(%s, %s) %s 0", smith_make_render(a, strlen(a)), smith_make_render(b, strlen(b)),
                              negate ? "!=" : "==");
    return SmithMakeTest_Dynamic;
}

static SmithMakeTest smith_make_test_def(const char *args, bool negate, const char **test) {
    const char *name = smith_make_trim(smith_make_expand_text(args, false));
    const SmithMakeVar *var = smith_make_var_find(name, strlen(name));
    if (var != nullptr && var->defined && !var->knob && !smith_make_has_hole(var->value, strlen(var->value)))
        return (var->value[0] != '\0') != negate ? SmithMakeTest_True : SmithMakeTest_False;
    const char *value = smith_make_expand_text(smith_make_format("$(%s)", name), false);
    if (!smith_make_has_hole(value, strlen(value)))
        return (value[0] != '\0') != negate ? SmithMakeTest_True : SmithMakeTest_False;
    *test = smith_make_format("(%s)[0] %s '\\0'", smith_make_render(value, strlen(value)), negate ? "==" : "!=");
    return SmithMakeTest_Dynamic;
}

static SmithMakeTest smith_make_test(const char *text, const char **test) {
    const char *args = nullptr;
    if (smith_make_if_keyword(text, "ifeq", &args)) return smith_make_test_eq(args, false, test);
    if (smith_make_if_keyword(text, "ifneq", &args)) return smith_make_test_eq(args, true, test);
    if (smith_make_keyword(text, "ifdef", &args)) return smith_make_test_def(args, false, test);
    if (smith_make_keyword(text, "ifndef", &args)) return smith_make_test_def(args, true, test);
    return SmithMakeTest_False;
}

static bool smith_make_is_if(const char *text) {
    const char *rest = nullptr;
    return smith_make_if_keyword(text, "ifeq", &rest) || smith_make_if_keyword(text, "ifneq", &rest) ||
           smith_make_keyword(text, "ifdef", &rest) || smith_make_keyword(text, "ifndef", &rest);
}

static bool smith_make_reading(void) {
    const SmithMakeReader *r = &smith_make_reader;
    return r->cond_count == 0 || (r->conds[r->cond_count - 1].outer && r->conds[r->cond_count - 1].active);
}

static void smith_make_push_cond(const char *text, bool chained) {
    SmithMakeReader *r = &smith_make_reader;
    if (r->cond_count >= smith_make_conds_max) smith_die(smith_exit_usage, "make: conditionals nest too deep", nullptr);
    SmithMakeCond *cond = &r->conds[r->cond_count++];
    *cond = (SmithMakeCond){.outer = r->cond_count == 1 ? true : false, .chained = chained};
    if (r->cond_count > 1) {
        const SmithMakeCond *parent = &r->conds[r->cond_count - 2];
        cond->outer = parent->outer && parent->active;
    }
    if (!cond->outer) {
        cond->taken = true; // nothing in a skipped region is read
        return;
    }
    const char *test = nullptr;
    SmithMakeTest result = smith_make_test(text, &test);
    if (result == SmithMakeTest_Dynamic) {
        cond->dynamic = true;
        cond->active = true;
        cond->test = test;
        cond->log_start = r->log_count;
        r->dynamic_count++;
    } else {
        cond->active = result == SmithMakeTest_True;
        cond->taken = cond->active;
    }
}

static void smith_make_else(const char *rest) {
    SmithMakeReader *r = &smith_make_reader;
    if (r->cond_count == 0) {
        smith_make_report("else with no if");
        return;
    }
    SmithMakeCond *cond = &r->conds[r->cond_count - 1];
    if (cond->in_else) {
        smith_make_report("a second else");
        return;
    }
    cond->in_else = true;
    if (cond->dynamic) {
        smith_make_dynamic_else(cond);
        cond->active = true;
    } else {
        cond->active = cond->outer && !cond->taken;
        cond->taken = true;
    }
    if (smith_make_is_if(rest)) smith_make_push_cond(rest, true);
}

static void smith_make_endif(void) {
    SmithMakeReader *r = &smith_make_reader;
    for (size_t pops = 0; pops < smith_make_conds_max; pops++) { // an else-if chain ends with its first if
        if (r->cond_count == 0) {
            smith_make_report("endif with no if");
            return;
        }
        SmithMakeCond *cond = &r->conds[r->cond_count - 1];
        if (cond->dynamic) {
            smith_make_dynamic_end(cond);
            r->dynamic_count--;
        }
        bool chained = cond->chained;
        r->cond_count--;
        if (!chained) return;
    }
}

// A conditional directive: whether the line was one.
static bool smith_make_conditional(const char *text) {
    const char *rest = nullptr;
    if (smith_make_is_if(text)) {
        smith_make_push_cond(text, false);
        return true;
    }
    if (smith_make_keyword(text, "else", &rest)) {
        smith_make_else(rest);
        return true;
    }
    if (smith_make_keyword(text, "endif", &rest)) {
        smith_make_endif();
        return true;
    }
    return false;
}

// ---- Assignments

typedef enum {
    SmithMakeOp_Recursive,
    SmithMakeOp_Simple,
    SmithMakeOp_Append,
    SmithMakeOp_Conditional,
    SmithMakeOp_Shell,
} SmithMakeOp;

// If the line is an assignment (the first = or := before any rule colon), where its name ends and its value begins.
static bool smith_make_find_assignment(const char *text, size_t *name_end, SmithMakeOp *op, size_t *value_at) {
    const char *at = smith_make_find_top(text, "=:");
    if (at == nullptr) return false;
    if (*at == ':') {
        size_t colons = 0;
        while (at[colons] == ':')
            colons++;
        if (at[colons] != '=' || colons > 2) return false;
        *name_end = (size_t)(at - text);
        *op = SmithMakeOp_Simple;
        *value_at = (size_t)(at - text) + colons + 1;
        return true;
    }
    *name_end = (size_t)(at - text);
    *op = SmithMakeOp_Recursive;
    *value_at = *name_end + 1;
    if (*name_end > 0) {
        char before = text[*name_end - 1];
        if (before == '+' || before == '?' || before == '!') {
            *name_end -= 1;
            *op = before == '+' ? SmithMakeOp_Append : (before == '?' ? SmithMakeOp_Conditional : SmithMakeOp_Shell);
        }
    }
    return true;
}

static const char *smith_make_join_value(const char *old, const char *add) {
    if (old[0] == '\0') return add;
    if (add[0] == '\0') return old;
    return smith_make_format("%s %s", old, add);
}

static void smith_make_assign_append(SmithMakeVar *var, const char *raw) {
    if (!var->defined) {
        smith_make_var_set(var, raw, raw, false);
    } else if (var->simple) {
        var->source = smith_make_join_value(var->source != nullptr ? var->source : var->value, smith_make_trim(raw));
        const char *marked = smith_make_expand_text(raw, true);
        const char *value = smith_make_unmark(marked);
        bool was_knob = var->knob;
        smith_make_var_set(var, smith_make_join_value(var->value, value), smith_make_join_value(var->marked, marked),
                           true);
        if (was_knob) var->knob = false;
    } else {
        var->source = smith_make_join_value(var->value, smith_make_trim(raw));
        smith_make_var_set(var, smith_make_join_value(var->value, raw), smith_make_join_value(var->value, raw), false);
    }
}

// Sets variable `name` as the line says. raw is the value's text, unexpanded.
static void smith_make_assign(const char *name, SmithMakeOp op, const char *raw) {
    while (smith_make_blank(*raw))
        raw++;
    name = smith_make_trim(smith_make_expand_text(name, false));
    if (name[0] == '\0') {
        smith_make_report("an assignment with no variable name");
        return;
    }
    if (strcmp(name, "VPATH") == 0) smith_make_report("VPATH is not supported");
    SmithMakeVar *var = smith_make_var_get(name, strlen(name));
    if (op == SmithMakeOp_Shell) {
        smith_make_report("!= (an assignment from a shell command) is not supported");
        return;
    }
    if (op == SmithMakeOp_Conditional && var->defined && !var->knob) return;
    smith_make_log_touch(var);
    if (op == SmithMakeOp_Conditional) {
        if (var->knob) {
            if (var->fallback == nullptr || var->fallback[0] == '\0') var->fallback = smith_make_expand_text(raw, true);
            return;
        }
        smith_make_make_knob(var, smith_make_expand_text(raw, true));
        var->source = smith_make_trim(raw);
        return;
    }
    if (op == SmithMakeOp_Append) {
        smith_make_assign_append(var, raw);
        return;
    }
    var->knob = false;
    var->built_in = false;
    var->source = smith_make_trim(raw);
    if (op == SmithMakeOp_Simple) {
        const char *marked = smith_make_expand_text(raw, true);
        smith_make_var_set(var, smith_make_unmark(marked), marked, true);
    } else {
        smith_make_var_set(var, smith_make_copy(raw), "", false);
    }
}

// `define NAME [op]` to `endef`: the lines between, joined by newlines.
static void smith_make_define(SmithMakeSource *source, const char *header) {
    size_t name_end = 0;
    size_t value_at = 0;
    SmithMakeOp op = SmithMakeOp_Recursive;
    const char *name = header;
    if (smith_make_find_assignment(header, &name_end, &op, &value_at)) name = smith_make_dup(header, name_end);
    SmithMakeBuffer *body = &smith_make_define_body;
    if (body->data == nullptr) smith_make_buffer_init(body, smith_make_input_max);
    body->length = 0;
    size_t depth = 1;
    const char *line = nullptr;
    size_t length = 0;
    bool closed = false;
    while (smith_make_physical(source, &line, &length)) { // bounded by the input
        const char *text = smith_make_dup(line, length);
        const char *skip = text;
        while (smith_make_blank(*skip))
            skip++;
        const char *rest = nullptr;
        if (smith_make_keyword(skip, "define", &rest)) depth++;
        if (smith_make_keyword(skip, "endef", &rest) && --depth == 0) {
            closed = true;
            break;
        }
        if (body->length > 0) smith_make_put_char(body, '\n');
        smith_make_put(body, line, length);
    }
    if (!closed) smith_make_report("define with no endef");
    smith_make_assign(smith_make_trim(name), op, smith_make_take(body, 0));
}

// ---- Rules

static uint32_t smith_make_target_hash(const char *name) {
    return smith_make_hash(name, strlen(name)) & (uint32_t)(smith_make_targets_max * 2 - 1);
}

static SmithMakeTarget *smith_make_target_find(const char *name) {
    SmithMakeReader *r = &smith_make_reader;
    uint32_t at = smith_make_target_hash(name);
    for (size_t probes = 0; probes < smith_make_targets_max * 2; probes++) {
        uint32_t slot = r->target_slots[at];
        if (slot == 0) return nullptr;
        if (strcmp(r->targets[slot - 1].name, name) == 0) return &r->targets[slot - 1];
        at = (at + 1) & (uint32_t)(smith_make_targets_max * 2 - 1);
    }
    return nullptr;
}

static SmithMakeTarget *smith_make_target_get(const char *name) {
    SmithMakeReader *r = &smith_make_reader;
    SmithMakeTarget *found = smith_make_target_find(name);
    if (found != nullptr) return found;
    if (r->target_count >= smith_make_targets_max) smith_die(smith_exit_usage, "make: too many targets", nullptr);
    uint32_t at = smith_make_target_hash(name);
    while (r->target_slots[at] != 0)
        at = (at + 1) & (uint32_t)(smith_make_targets_max * 2 - 1);
    r->targets[r->target_count] = (SmithMakeTarget){.name = name, .where = smith_make.here};
    r->target_slots[at] = (uint32_t)++r->target_count;
    return &r->targets[r->target_count - 1];
}

static void smith_make_target_attach(const char *name, size_t rule) {
    SmithMakeReader *r = &smith_make_reader;
    SmithMakeTarget *target = smith_make_target_get(name);
    if (r->ref_count >= smith_make_refs_max) smith_die(smith_exit_usage, "make: too many rules", nullptr);
    uint32_t ref = (uint32_t)++r->ref_count;
    r->refs[ref - 1] = (SmithMakeRef){(uint32_t)rule, 0};
    if (target->last != 0) r->refs[target->last - 1].next = ref;
    if (target->first == 0) target->first = ref;
    target->last = ref;
}

static const char **smith_make_word_list(const char *text, size_t *count) {
    size_t total = 0;
    SmithMakeWord *words = smith_make_words(text, strlen(text), &total);
    const char **list = smith_make_new_list((total + 1));
    for (size_t i = 0; i < total; i++)
        list[i] = smith_make_word_text(words[i]);
    list[total] = nullptr;
    *count = total;
    return list;
}

// Prerequisites before and after a `|`.
static void smith_make_split_prerequisites(const char *text, SmithMakeRule *rule) {
    const char *bar = strchr(text, '|');
    const char *normal = bar != nullptr ? smith_make_dup(text, (size_t)(bar - text)) : text;
    rule->prerequisites = smith_make_word_list(normal, &rule->prerequisite_count);
    rule->order_only = smith_make_word_list(bar != nullptr ? bar + 1 : "", &rule->order_only_count);
}

static SmithMakeRule *smith_make_rule_new(void) {
    SmithMakeReader *r = &smith_make_reader;
    if (r->rule_count >= smith_make_rules_max) smith_die(smith_exit_usage, "make: too many rules", nullptr);
    SmithMakeRule *rule = &r->rules[r->rule_count++];
    *rule = (SmithMakeRule){
        .where = smith_make.here, .guard = smith_make_guard(), .recipe_from = -1, .from_eval = smith_make.in_eval};
    rule->recipe_first = r->recipe_line_count;
    return rule;
}

static void smith_make_add_recipe(size_t rule_index, const char *text, SmithMakeWhere where) {
    SmithMakeReader *r = &smith_make_reader;
    if (r->recipe_line_count >= smith_make_recipe_lines_max)
        smith_die(smith_exit_usage, "make: too many recipe lines", nullptr);
    SmithMakeRule *rule = &r->rules[rule_index];
    SMITH_ASSERT(rule->recipe_first + rule->recipe_count == r->recipe_line_count);
    r->recipe_lines[r->recipe_line_count++] = (SmithMakeRecipeLine){smith_make_copy(text), where};
    rule->recipe_count++;
}

static bool smith_make_is_special(const char *name) {
    return name[0] == '.' && name[1] >= 'A' && name[1] <= 'Z';
}

// `.c.o`: an old-style suffix rule.
static bool smith_make_is_suffix_rule(const char *name) {
    if (name[0] != '.' || name[1] < 'a' || name[1] > 'z') return false;
    size_t dots = 0;
    for (const char *c = name; *c != '\0'; c++) {
        if (*c == '.')
            dots++;
        else if (*c < 'a' || *c > 'z')
            return false;
    }
    return dots <= 2;
}

static void smith_make_special_target(const char *name, const SmithMakeRule *rule) {
    if (strcmp(name, ".PHONY") == 0) {
        for (size_t i = 0; i < rule->prerequisite_count; i++)
            smith_make_target_get(rule->prerequisites[i])->phony = true;
    } else if (strcmp(name, ".SECONDEXPANSION") == 0 || strcmp(name, ".ONESHELL") == 0 ||
               strcmp(name, ".DEFAULT") == 0) {
        smith_make_report("%s is not supported", name);
    }
}

// `targets: patterns: prerequisites`: one explicit rule per target, its prerequisites from the stem.
static void smith_make_static_pattern(const SmithMakeRule *shape, const char *pattern_text, const char *prerequisites,
                                      const char *recipe) {
    SmithMakeReader *r = &smith_make_reader;
    size_t pattern_count = 0;
    const char **patterns = smith_make_word_list(pattern_text, &pattern_count);
    int64_t first = -1;
    if (pattern_count != 1) {
        smith_make_report("a static pattern rule takes one target pattern");
        return;
    }
    for (size_t i = 0; i < shape->target_count; i++) {
        const char *stem = nullptr;
        size_t stem_length = 0;
        const char *target = shape->targets[i];
        if (!smith_make_match(patterns[0], strlen(patterns[0]), target, strlen(target), &stem, &stem_length)) {
            smith_make_report("%s does not match the pattern %s", target, patterns[0]);
            continue;
        }
        SmithMakeRule *rule = smith_make_rule_new();
        size_t index = r->rule_count - 1;
        rule->targets = smith_make_new_list(2);
        rule->targets[0] = target;
        rule->target_count = 1;
        rule->stem = smith_make_dup(stem, stem_length);
        const char *bar = strchr(prerequisites, '|');
        const char *normal =
            bar != nullptr ? smith_make_dup(prerequisites, (size_t)(bar - prerequisites)) : prerequisites;
        size_t count = 0;
        const char **raw = smith_make_word_list(normal, &count);
        rule->prerequisites = smith_make_new_list((count + 1));
        for (size_t k = 0; k < count; k++)
            rule->prerequisites[k] = smith_make_substitute(raw[k], strlen(raw[k]), stem, stem_length);
        rule->prerequisite_count = count;
        raw = smith_make_word_list(bar != nullptr ? bar + 1 : "", &count);
        rule->order_only = smith_make_new_list((count + 1));
        for (size_t k = 0; k < count; k++)
            rule->order_only[k] = smith_make_substitute(raw[k], strlen(raw[k]), stem, stem_length);
        rule->order_only_count = count;
        smith_make_target_attach(target, index);
        if (first < 0) {
            first = (int64_t)index;
            r->current_rule = first;
            if (r->default_goal == nullptr) r->default_goal = target;
            if (recipe != nullptr) smith_make_add_recipe(index, recipe, smith_make.here);
        } else {
            rule->recipe_from = first;
        }
    }
    r->in_rule = true;
}

static const char *smith_make_first_non_blank(const char *text) {
    while (smith_make_blank(*text))
        text++;
    return text;
}

// A rule line: the targets, a colon, the prerequisites and perhaps a recipe after a semicolon.
static void smith_make_rule_line(const char *text) {
    SmithMakeReader *r = &smith_make_reader;
    const char *colon = smith_make_find_top(text, ":");
    SMITH_ASSERT(colon != nullptr);
    const char *after = colon + 1;
    if (*after == ':') {
        smith_make_report("a double-colon rule is not supported");
        after++;
    }
    const char *semicolon = smith_make_find_top(after, ";");
    const char *recipe = semicolon != nullptr ? smith_make_first_non_blank(semicolon + 1) : nullptr;
    const char *prerequisites = semicolon != nullptr ? smith_make_dup(after, (size_t)(semicolon - after)) : after;
    const char *second = smith_make_find_top(prerequisites, ":");
    const char *targets_text = smith_make_expand_text(smith_make_dup(text, (size_t)(colon - text)), false);
    SmithMakeRule shape = {};
    shape.targets = smith_make_word_list(targets_text, &shape.target_count);
    if (shape.target_count == 0) {
        r->in_rule = false;
        return;
    }
    if (second != nullptr && smith_make_find_assignment(prerequisites, &(size_t){0}, &(SmithMakeOp){0}, &(size_t){0})) {
        smith_make_report("a target-specific variable is not supported");
        r->in_rule = false;
        return;
    }
    if (second != nullptr) {
        const char *patterns =
            smith_make_expand_text(smith_make_dup(prerequisites, (size_t)(second - prerequisites)), false);
        const char *prereq = smith_make_expand_text(second + 1, false);
        smith_make_static_pattern(&shape, patterns, prereq, recipe);
        return;
    }
    if (smith_make_find_assignment(prerequisites, &(size_t){0}, &(SmithMakeOp){0}, &(size_t){0})) {
        smith_make_report("a target-specific variable is not supported");
        r->in_rule = false;
        return;
    }
    SmithMakeRule *rule = smith_make_rule_new();
    size_t index = r->rule_count - 1;
    rule->targets = shape.targets;
    rule->target_count = shape.target_count;
    smith_make_split_prerequisites(smith_make_expand_text(prerequisites, false), rule);
    for (size_t i = 0; i < rule->target_count; i++) {
        if (strchr(rule->targets[i], '%') != nullptr) rule->pattern = true;
    }
    if (rule->pattern) {
        r->patterns[r->pattern_count++] = (uint32_t)index;
    } else {
        for (size_t i = 0; i < rule->target_count; i++) {
            smith_make_target_attach(rule->targets[i], index);
            if (smith_make_is_special(rule->targets[i])) {
                smith_make_special_target(rule->targets[i], rule);
            } else if (smith_make_is_suffix_rule(rule->targets[i])) {
                smith_make_report("the suffix rule %s is not supported", rule->targets[i]);
            } else if (r->default_goal == nullptr) {
                r->default_goal = rule->targets[i];
            }
        }
    }
    r->current_rule = (int64_t)index;
    r->in_rule = true;
    if (recipe != nullptr) smith_make_add_recipe(index, recipe, smith_make.here);
}

// ---- Includes

static bool smith_make_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0;
}

static bool smith_make_has_suffix(const char *text, const char *suffix) {
    size_t length = strlen(text);
    size_t tail = strlen(suffix);
    return length >= tail && strcmp(text + length - tail, suffix) == 0;
}

static void smith_make_open(const char *path) {
    SmithMakeReader *r = &smith_make_reader;
    if (r->source_count >= smith_make_includes_max) {
        smith_make_report("includes nest more than %zu deep", smith_make_includes_max);
        return;
    }
    char *text = nullptr;
    size_t length = 0;
    if (!smith_make_slurp(path, &text, &length)) {
        smith_make_report("cannot read %s", path);
        return;
    }
    r->sources[r->source_count++] = (SmithMakeSource){.name = smith_make_copy(path), .text = text, .length = length};
}

// Text that a $(eval) made, read as lines after the line that held it.
static void smith_make_flush_evals(void) {
    SmithMakeReader *r = &smith_make_reader;
    for (size_t i = smith_make.eval_count; i > 0; i--) { // the first is read first: it goes on the stack last
        if (r->source_count >= smith_make_includes_max) {
            smith_make_report("includes nest more than %zu deep", smith_make_includes_max);
            break;
        }
        const char *text = smith_make.evals[i - 1];
        r->sources[r->source_count++] = (SmithMakeSource){.name = smith_make.here.file,
                                                          .text = (char *)text,
                                                          .length = strlen(text),
                                                          .line = smith_make.here.line - 1,
                                                          .eval = true};
    }
    smith_make.eval_count = 0;
}

static void smith_make_include(const char *args, bool optional) {
    SmithMakeReader *r = &smith_make_reader;
    size_t count = 0;
    const char **names = smith_make_word_list(smith_make_expand_text(args, false), &count);
    for (size_t i = count; i > 0; i--) { // the first is read first: they go on the stack last
        const char *name = names[i - 1];
        if (smith_make_has_suffix(name, ".d")) {
            smith_make_report_at(smith_make.here,
                                 "note: -include %s is dropped: smith tracks the headers of each compile", name);
            continue;
        }
        const SmithMakeTarget *made = smith_make_target_find(name);
        bool generated = made != nullptr && made->first != 0; // a file this Makefile makes is never read
        if (generated) smith_make.generated_include = true;
        if (smith_make_exists(name) && !generated) {
            smith_make_open(name);
        } else if (optional) {
            if (r->generated_count < 1024) r->generated_includes[r->generated_count++] = name;
        } else {
            smith_make_report("include %s: no such file", name);
        }
    }
}

// ---- Lines

static void smith_make_directive_export(const char *rest, bool unexport) {
    smith_make_report("%s is not supported: the steps run with the environment smith build has",
                      unexport ? "unexport" : "export");
    size_t name_end = 0;
    size_t value_at = 0;
    SmithMakeOp op = SmithMakeOp_Recursive;
    if (rest[0] != '\0' && smith_make_find_assignment(rest, &name_end, &op, &value_at))
        smith_make_assign(smith_make_dup(rest, name_end), op, rest + value_at);
}

// include, define, export and the other directives that are not assignments or rules: whether the line was one.
static bool smith_make_directive(SmithMakeSource *source, const char *text) {
    const char *rest = nullptr;
    if (smith_make_keyword(text, "include", &rest)) {
        smith_make_include(rest, false);
    } else if (smith_make_keyword(text, "-include", &rest) || smith_make_keyword(text, "sinclude", &rest)) {
        smith_make_include(rest, true);
    } else if (smith_make_keyword(text, "define", &rest)) {
        smith_make_define(source, rest);
    } else if (smith_make_keyword(text, "export", &rest)) {
        smith_make_directive_export(rest, false);
    } else if (smith_make_keyword(text, "unexport", &rest)) {
        smith_make_directive_export(rest, true);
    } else if (smith_make_keyword(text, "vpath", &rest)) {
        smith_make_report("vpath is not supported");
    } else {
        return false;
    }
    return true;
}

// A line outside a recipe. Returns after handling it.
static void smith_make_line(SmithMakeSource *source, const char *raw) {
    const char *text = smith_make_first_non_blank(smith_make_strip_comment(raw));
    size_t trimmed_from = 0;
    size_t trimmed = smith_make_trim_length(text, strlen(text), &trimmed_from);
    text = smith_make_dup(text, trimmed);
    if (text[0] == '\0') return;
    if (smith_make_conditional(text)) return;
    if (!smith_make_reading()) return;
    const char *rest = nullptr;
    if (smith_make_directive(source, text)) return;
    if (smith_make_keyword(text, "undefine", &rest)) {
        SmithMakeVar *var = smith_make_var_find(rest, strlen(rest));
        if (var != nullptr) {
            smith_make_log_touch(var);
            var->defined = false;
        }
        return;
    }
    if (smith_make_keyword(text, "override", &rest) || smith_make_keyword(text, "private", &rest)) text = rest;
    size_t name_end = 0;
    size_t value_at = 0;
    SmithMakeOp op = SmithMakeOp_Recursive;
    if (smith_make_find_assignment(text, &name_end, &op, &value_at)) {
        smith_make_reader.in_rule = false;
        smith_make_assign(smith_make_dup(text, name_end), op, text + value_at);
        return;
    }
    if (smith_make_find_top(text, ":") != nullptr) {
        smith_make_rule_line(text);
        return;
    }
    if (text[0] == '$') { // a line of text for make to expand, such as $(foreach ...$(eval ...))
        const char *expanded = smith_make_expand_text(text, false);
        size_t from = 0;
        if (smith_make_trim_length(expanded, strlen(expanded), &from) == 0) return;
    }
    smith_make_report("missing separator: %.40s", text);
}

static void smith_make_recipe_line(const char *text) {
    SmithMakeReader *r = &smith_make_reader;
    if (!smith_make_reading()) return;
    if (r->current_rule < 0) {
        smith_make_report("a recipe line with no rule");
        return;
    }
    smith_make_add_recipe((size_t)r->current_rule, text + 1, smith_make.here);
}

static void smith_make_define_defaults(void) {
    static const char *const simple[][2] = {
        {"MAKE", "make"},   {"MAKEFLAGS", ""}, {"MFLAGS", ""},  {"MAKECMDGOALS", ""}, {"SHELL", "/bin/sh"},
        {"MAKELEVEL", "0"}, {"CC", "cc"},      {"CXX", "g++"},  {"AR", "ar"},         {"ARFLAGS", "rv"},
        {"AS", "as"},       {"LD", "ld"},      {"RM", "rm -f"}, {"CPP", "cc -E"}};
    for (size_t i = 0; i < sizeof simple / sizeof simple[0]; i++) {
        SmithMakeVar *var = smith_make_var_get(simple[i][0], strlen(simple[i][0]));
        smith_make_var_set(var, simple[i][1], simple[i][1], true);
        var->built_in = true;
        var->order = 0;
    }
    SmithMakeVar *curdir = smith_make_var_get("CURDIR", 6);
    const char *root = smith_make_hole_text("b->root");
    smith_make_var_set(curdir, root, root, true);
    curdir->built_in = true;
    curdir->order = 0;
}

// Reads the Makefile and everything it includes.
static void smith_make_read(const char *path) {
    SmithMakeReader *r = &smith_make_reader;
    smith_make_reader_init();
    smith_make_define_defaults();
    smith_make.reading = true;
    smith_make_open(path);
    for (uint64_t steps = 0; r->source_count > 0; steps++) {
        if (steps > smith_make_steps_max) smith_die(smith_exit_usage, "make: the Makefile does not end", nullptr);
        SmithMakeSource *source = &r->sources[r->source_count - 1];
        SmithMakeLine line;
        if (!smith_make_next_line(source, r->in_rule, &line)) {
            r->source_count--;
            continue;
        }
        smith_make.here = line.where;
        smith_make.in_eval = source->eval;
        if (line.recipe) {
            smith_make_recipe_line(line.text);
        } else {
            smith_make_line(source, line.text);
        }
        if (smith_make.eval_count > 0) smith_make_flush_evals();
    }
    smith_make.reading = false;
    if (r->cond_count > 0) smith_make_report("a conditional is not closed by an endif");
    smith_make.generated_include = false; // recipes are expanded later, with the variables a command line can set
    for (size_t i = 0; i < r->generated_count; i++) {
        const SmithMakeTarget *made = smith_make_target_find(r->generated_includes[i]);
        if (made != nullptr && made->first != 0)
            smith_make_report_at(made->where,
                                 "-include %s names a file this Makefile makes, so it is not read (what "
                                 "it defines stays undefined)",
                                 r->generated_includes[i]);
    }
}
