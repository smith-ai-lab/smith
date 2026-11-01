// A lambda in a generic's body, and the struct of its arguments (issues #316 and #956).
// Included by runtime/lang_generic.c, before the replay is run.
//
//   export void spawn_like(constexpr auto f, auto... rest) {
//       run([](void *raw) {                             // a function of the instance, not a local one
//           typeof(rest) *a = raw;                      // the struct of the arguments
//           f(a->[rest]...);                            // ... and its members, one for each argument
//       });
//   }
//
// A lambda directly in a generic's body is written once per instance, at file scope before the instance, as
// `static R <instance>__lambda<k>(P) {...}`, and the expression is the name: the address of a function that knows the
// instance's types. The generic's compile-time names (`T`, `constexpr auto f`, `typeof(a)`, `countof(rest)`, a number
// parameter) are not captures; a run-time parameter of the generic is, and is an error. When the lambda has no `-> R`
// the return type is `void` without a `return`, else the type of the expression of the first `return` (the body's
// first statement): the instance writes `__typeof_unqual__(e)` with each parameter of the lambda a member of a
// compound literal of the struct of its parameters, so the compiler reads it once the types are known, with no query.
// `typeof(rest)` is the struct `smith_pack_<instance>` whose members `_0`, `_1`, ... have the types of the arguments,
// defined before the functions; `x->[rest]...` and `x.[rest]...` are those members as the arguments of a call or an
// initializer. Both lower to plain C.

static void smith_lang_replay_run(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay);
static void smith_lang_replay_begin(SmithLangReplay *replay, const SmithLangGeneric *generic,
                                    const SmithLangInstance *instance, uint32_t id, uint32_t location);

constexpr size_t smith_lang_nested_max = 8;
constexpr size_t smith_lang_nested_names_max = 16;

typedef struct {
    uint32_t start; // [start, end) of smith_lang_gt: `[](P) -> R { ... }`
    uint32_t end;
    uint32_t params_from, params_to; // the tokens between the parentheses
    bool has_type;
    uint32_t type_from, type_to; // `-> R`: the tokens of R
    uint32_t body_from, body_to; // the `{` and the `}`
    char mangled[smith_lang_name_max];
} SmithLangNested;

typedef struct {
    SmithLangNested list[smith_lang_nested_max];
    size_t count;
    char pack_tag[smith_lang_name_max];
    bool failed; // an error was reported: a lambda stands for `nullptr`, so that the body adds no second error
    // the probe of an inferred return type: a parameter of the lambda is a member of a compound literal
    char probe_tag[smith_lang_name_max];
    const SmithLangGenToken *probe_names[smith_lang_nested_names_max];
    size_t probe_count;
} SmithLangNestedState;

static SmithLangNestedState smith_lang_nested;

// The token after the `)` that matches the `(` at `open`, below `limit` (UINT32_MAX when none).
static uint32_t smith_lang_nested_close(uint32_t open, uint32_t limit, const char *opener, const char *closer) {
    int depth = 0;
    for (uint32_t at = open; at < limit; at++) { // bounded by the body
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (token->special != SmithLangGen_Plain) continue;
        depth += smith_lang_gt_punct(token, opener) ? 1 : 0;
        depth -= smith_lang_gt_punct(token, closer) ? 1 : 0;
        if (depth == 0) return at;
    }
    return UINT32_MAX;
}

// Whether an expression can start after the token before `[`: a subscript or a declarator's brackets cannot.
static bool smith_lang_nested_expression_before(uint32_t at, uint32_t first) {
    if (at == first) return false;
    const SmithLangGenToken *before = &smith_lang_gt.tokens[at - 1];
    if (before->kind == SmithTokenKind_Identifier) return smith_lang_gt_is(before, SmithTokenKind_Identifier, "return");
    if (before->kind != SmithTokenKind_Punctuator) return false;
    static const char *const openers[] = {"=", "(", ",", "?", ":", "{", "&&", "||"};
    for (size_t k = 0; k < sizeof openers / sizeof openers[0]; k++) // the tokens an expression follows
        if (smith_lang_gt_punct(before, openers[k])) return true;
    return false;
}

// The `]` of the capture list that opens at `open` (`[]`, `[x]`, `[x, y]`: names and commas), or UINT32_MAX.
static uint32_t smith_lang_nested_list_end(uint32_t open, uint32_t limit) {
    bool name = true;
    for (uint32_t at = open + 1; at < limit; at++) {
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (name && smith_lang_gt_punct(token, "]")) return at == open + 1 ? at : UINT32_MAX;
        if (name) {
            if (token->kind != SmithTokenKind_Identifier) return UINT32_MAX;
            name = false;
        } else if (smith_lang_gt_punct(token, "]")) {
            return at;
        } else if (smith_lang_gt_punct(token, ",")) {
            name = true;
        } else {
            return UINT32_MAX;
        }
    }
    return UINT32_MAX;
}

// The lambda that starts at `at` (a `[`) when it is one: its parts.
static bool smith_lang_nested_lambda(uint32_t at, uint32_t first, uint32_t limit, SmithLangNested *out) {
    if (at + 3 >= limit || !smith_lang_gt_punct(&smith_lang_gt.tokens[at], "[") ||
        !smith_lang_nested_expression_before(at, first)) {
        return false;
    }
    uint32_t list = smith_lang_nested_list_end(at, limit);
    if (list == UINT32_MAX || list + 1 >= limit) return false;
    SmithLangNested found = {.start = at, .params_from = list + 1, .params_to = list + 1};
    uint32_t next = list + 1;
    const SmithLangGenToken *token = &smith_lang_gt.tokens[next];
    if (smith_lang_gt_punct(token, "(")) {
        uint32_t close = smith_lang_nested_close(next, limit, "(", ")");
        if (close == UINT32_MAX) return false;
        found.params_from = next + 1;
        found.params_to = close;
        next = close + 1;
    } else if (!smith_lang_gt_punct(token, "{") && !smith_lang_gt_punct(token, "->")) {
        return false;
    }
    if (next < limit && smith_lang_gt_punct(&smith_lang_gt.tokens[next], "->")) {
        found.has_type = true;
        found.type_from = next + 1;
        for (next = next + 1; next < limit && !smith_lang_gt_punct(&smith_lang_gt.tokens[next], "{"); next++) {
        }
        found.type_to = next;
    }
    if (next >= limit || !smith_lang_gt_punct(&smith_lang_gt.tokens[next], "{")) return false;
    uint32_t end = smith_lang_nested_close(next, limit, "{", "}");
    if (end == UINT32_MAX) return false;
    found.body_from = next;
    found.body_to = end;
    found.end = end + 1;
    *out = found;
    return true;
}

// The name a parameter declaration [from, to) declares: the first identifier that ends it (or is followed by `[` or
// `)`) and does not follow a `(`. nullptr when there is none.
static const SmithLangGenToken *smith_lang_nested_param_name(uint32_t from, uint32_t to) {
    for (uint32_t at = from; at < to; at++) { // the tokens of one declaration
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (token->kind != SmithTokenKind_Identifier || token->special != SmithLangGen_Plain) continue;
        const SmithLangGenToken *after = at + 1 < to ? &smith_lang_gt.tokens[at + 1] : nullptr;
        bool ends = after == nullptr || smith_lang_gt_punct(after, "[") || smith_lang_gt_punct(after, ")");
        if (ends && !(at > from && smith_lang_gt_punct(&smith_lang_gt.tokens[at - 1], "("))) return token;
    }
    return nullptr;
}

// Calls `visit(from, to)` for each parameter declaration in [from, to), split at the commas outside parentheses.
typedef void (*SmithLangNestedVisit)(uint32_t from, uint32_t to, void *context);

static void smith_lang_nested_each_param(uint32_t from, uint32_t to, SmithLangNestedVisit visit, void *context) {
    int depth = 0;
    uint32_t first = from;
    for (uint32_t at = from; at <= to; at++) { // the commas of the list
        const SmithLangGenToken *token = at < to ? &smith_lang_gt.tokens[at] : nullptr;
        if (token != nullptr && token->special == SmithLangGen_Plain) {
            depth += (smith_lang_gt_punct(token, "(") || smith_lang_gt_punct(token, "[")) ? 1 : 0;
            depth -= (smith_lang_gt_punct(token, ")") || smith_lang_gt_punct(token, "]")) ? 1 : 0;
        }
        if (at == to || (depth == 0 && smith_lang_gt_punct(token, ","))) {
            if (at > first) visit(first, at, context);
            first = at + 1;
        }
    }
}

typedef struct {
    const SmithLangGenToken *names[smith_lang_nested_names_max];
    size_t count;
} SmithLangNestedNames;

static void smith_lang_nested_collect(uint32_t from, uint32_t to, void *context) {
    SmithLangNestedNames *names = context;
    const SmithLangGenToken *name = smith_lang_nested_param_name(from, to);
    if (name != nullptr && names->count < smith_lang_nested_names_max) names->names[names->count++] = name;
}

static bool smith_lang_nested_same(const SmithLangGenToken *a, const SmithLangGenToken *b) {
    return a->length == b->length && memcmp(smith_lang_gt_str(a), smith_lang_gt_str(b), a->length) == 0;
}

static bool smith_lang_nested_listed(const SmithLangNestedNames *names, const SmithLangGenToken *token) {
    for (size_t k = 0; k < names->count; k++) // the names
        if (smith_lang_nested_same(names->names[k], token)) return true;
    return false;
}

static void smith_lang_nested_runtime(uint32_t from, uint32_t to, void *context) {
    if (smith_lang_gt_is(&smith_lang_gt.tokens[from], SmithTokenKind_Identifier, "constexpr")) return;
    smith_lang_nested_collect(from, to, context);
}

// The run-time parameters of the generic (every one that is not `constexpr`): a lambda cannot capture them.
static void smith_lang_nested_runtime_names(const SmithLangGeneric *generic, SmithLangNestedNames *names) {
    uint32_t first = generic->first + generic->name_index;
    uint32_t limit = generic->first + generic->body_index;
    for (uint32_t at = first; at < limit; at++) { // the head's parameter list
        if (!smith_lang_gt_punct(&smith_lang_gt.tokens[at], "(")) continue;
        uint32_t close = smith_lang_nested_close(at, limit, "(", ")");
        if (close != UINT32_MAX) smith_lang_nested_each_param(at + 1, close, smith_lang_nested_runtime, names);
        return;
    }
}

// Whether the identifier at `at` is the operand of a query that reads a name's type or count, not its value.
static bool smith_lang_nested_query_operand(uint32_t at) {
    if (at < 2 || !smith_lang_gt_punct(&smith_lang_gt.tokens[at - 1], "(")) return false;
    const SmithLangGenToken *before = &smith_lang_gt.tokens[at - 2];
    return smith_lang_gt_is(before, SmithTokenKind_Identifier, "typeof") ||
           smith_lang_gt_is(before, SmithTokenKind_Identifier, "sizeof") ||
           smith_lang_gt_is(before, SmithTokenKind_Identifier, "countof") ||
           smith_lang_gt_is(before, SmithTokenKind_Identifier, "alignof");
}

// A run-time parameter of the generic used in the lambda is a capture: an error that names it.
static bool smith_lang_nested_captures(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                       const SmithLangNested *nested) {
    SmithLangNestedNames outer = {};
    smith_lang_nested_runtime_names(replay->generic, &outer);
    SmithLangNestedNames own = {};
    smith_lang_nested_each_param(nested->params_from, nested->params_to, smith_lang_nested_collect, &own);
    for (uint32_t at = nested->body_from; at < nested->end; at++) { // the lambda's own tokens
        SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (token->kind != SmithTokenKind_Identifier || token->special != SmithLangGen_Plain ||
            smith_lang_member_before(at) || smith_lang_nested_query_operand(at) ||
            (at > 0 && smith_lang_gt_punct(&smith_lang_gt.tokens[at - 1], "[") && smith_lang_member_before(at - 1)) ||
            !smith_lang_nested_listed(&outer, token) || smith_lang_nested_listed(&own, token)) {
            continue;
        }
        smith_lang_error(lang, host, smith_lang_gen_place(host, replay->generic, token, replay->location),
                         "`%.*s` belongs to the generic's caller: a lambda that uses it would capture it, and a "
                         "lambda converts to a function pointer only when it captures nothing (a function pointer "
                         "can't carry captures); the generic's `constexpr` parameters are not captures",
                         (int)token->length, smith_lang_gt_str(token));
        return false;
    }
    return true;
}

// The lambdas in the body of the generic of `replay`, and whether the body asks for `typeof(rest)`.
static bool smith_lang_nested_scan(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                   bool *wants_pack) {
    const SmithLangGeneric *generic = replay->generic;
    uint32_t first = generic->first + generic->body_index;
    uint32_t limit = generic->first + generic->count;
    smith_lang_nested.count = 0;
    *wants_pack = false;
    uint32_t inside = 0; // the end of the lambda being read, for a lambda inside it
    bool good = true;
    for (uint32_t at = first; at < limit; at++) { // the body's tokens
        SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (token->special != SmithLangGen_Plain) continue;
        if (smith_lang_has_pack(generic) && smith_lang_gt_is(token, SmithTokenKind_Identifier, "typeof") &&
            at + 3 < limit && smith_lang_gt_punct(token + 1, "(") && smith_lang_gt_punct(token + 3, ")")) {
            SmithLangPackRef ref;
            *wants_pack = *wants_pack || smith_lang_pack_resolve(replay, at + 2, &ref);
        }
        SmithLangNested found;
        if (!smith_lang_nested_lambda(at, first, limit, &found)) continue;
        uint32_t place = smith_lang_gen_place(host, generic, token, replay->location);
        if (at < inside) {
            smith_lang_error(lang, host, place,
                             "a lambda inside a lambda in a generic is not built: the generic writes one function "
                             "for each instance, so write the inner one as a function of its own");
            good = false;
            continue;
        }
        if (smith_lang_nested.count >= smith_lang_nested_max) {
            smith_lang_error(lang, host, place, "a generic has at most %zu lambdas in its body", smith_lang_nested_max);
            good = false;
            continue;
        }
        snprintf(found.mangled, sizeof found.mangled, "%s__lambda%zu", smith_lang_str(replay->instance->cname),
                 smith_lang_nested.count);
        good = smith_lang_nested_captures(lang, host, replay, &found) && good; // the list is kept: the body names them
        inside = found.end;
        smith_lang_nested.list[smith_lang_nested.count++] = found;
    }
    return good;
}

// `struct smith_pack_<instance> { T0 _0; T1 _1; };` at file scope: `typeof(rest)` in the instance's body.
static void smith_lang_nested_define_pack(SmithLang *lang, const SmithHost *host, const SmithLangInstance *instance,
                                          uint32_t location, uint32_t id) {
    const SmithLangGeneric *generic = &smith_lang_generics[instance->generic];
    size_t count = smith_lang_pack_size(generic, instance);
    snprintf(smith_lang_nested.pack_tag, sizeof smith_lang_nested.pack_tag, "smith_pack_%s",
             smith_lang_str(instance->cname));
    if (count == 0) return;
    smith_lang_gen_note_break(lang);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "struct", location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, smith_lang_nested.pack_tag, location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "{", location, id);
    for (size_t k = 0; k < count; k++) { // one member for each argument
        char member[32];
        snprintf(member, sizeof member, "_%zu", k);
        smith_lang_gen_put_arg(lang, host, instance, (unsigned)(smith_lang_pack_base(generic) + k), id);
        smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, member, location, id);
        smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ";", location, id);
    }
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, "}", location, id);
    smith_lang_gen_put(lang, host, SmithTokenKind_Punctuator, ";", location, id);
}

// What the body's replay does with a lambda, the probe of an inferred type and `typeof(rest)`. True when it took
// tokens.
static bool smith_lang_nested_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                     SmithLangFrame *frame) {
    if (replay->instance == nullptr ||
        (smith_lang_nested.count == 0 && smith_lang_nested.pack_tag[0] == 0 && smith_lang_nested.probe_count == 0)) {
        return false;
    }
    SmithLangGenToken *token = &smith_lang_gt.tokens[frame->pos];
    if (token->special != SmithLangGen_Plain) return false;
    uint32_t place = smith_lang_gen_place(host, replay->generic, token, replay->location);
    for (size_t k = 0; k < smith_lang_nested.count; k++) { // a lambda is its function's name
        const SmithLangNested *nested = &smith_lang_nested.list[k];
        if (frame->pos != nested->start) continue;
        smith_lang_gen_put(lang, host, SmithTokenKind_Identifier,
                           smith_lang_nested.failed ? "nullptr" : nested->mangled, place, replay->instance_id);
        frame->pos = nested->end;
        return true;
    }
    if (token->kind == SmithTokenKind_Identifier && token->param == 0 && !smith_lang_member_before(frame->pos)) {
        for (size_t k = 0; k < smith_lang_nested.probe_count; k++) { // `a` is `((tag){}.a)`
            if (!smith_lang_nested_same(smith_lang_nested.probe_names[k], token)) continue;
            static const char *const open[] = {"(", "(", nullptr, ")", "{", "}", ".", nullptr, ")"};
            for (size_t i = 0; i < sizeof open / sizeof open[0]; i++) { // the pieces
                const char *text = open[i] != nullptr ? open[i] : i == 2 ? smith_lang_nested.probe_tag : nullptr;
                char name[smith_lang_name_max];
                if (text == nullptr) {
                    snprintf(name, sizeof name, "%.*s", (int)token->length, smith_lang_gt_str(token));
                    text = name;
                }
                bool word = open[i] == nullptr;
                smith_lang_gen_put(lang, host, word ? SmithTokenKind_Identifier : SmithTokenKind_Punctuator, text,
                                   place, replay->instance_id);
            }
            frame->pos += 1;
            return true;
        }
    }
    SmithLangPackRef ref;
    if (smith_lang_nested.pack_tag[0] != 0 && frame->pos + 3 < frame->end &&
        smith_lang_gt_is(token, SmithTokenKind_Identifier, "typeof") && smith_lang_gt_punct(token + 1, "(") &&
        smith_lang_pack_resolve(replay, frame->pos + 2, &ref) && smith_lang_gt_punct(token + 3, ")")) {
        if (ref.count == 0) {
            smith_lang_error(lang, host, place, "`typeof(%s)` is a struct of the arguments, and there are none",
                             ref.name);
            smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "int", place,
                               replay->instance_id); // no more errors
            frame->pos += 4;
            return true;
        }
        smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, "struct", place, replay->instance_id);
        smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, smith_lang_nested.pack_tag, place,
                           replay->instance_id);
        frame->pos += 4;
        return true;
    }
    return false;
}

// Whether the tokens at `at` are `[ rest ]` after a `.` or `->`: the member of the argument.
static bool smith_lang_nested_member_at(const SmithLangReplay *replay, uint32_t at, uint32_t limit) {
    SmithLangPackRef ref;
    return at > 0 && at + 2 < limit && smith_lang_gt_punct(&smith_lang_gt.tokens[at], "[") &&
           smith_lang_member_before(at) && smith_lang_pack_resolve(replay, at + 1, &ref) &&
           smith_lang_gt_punct(&smith_lang_gt.tokens[at + 2], "]");
}

// What writing a lambda needs at every step.
typedef struct {
    SmithLang *lang;
    const SmithHost *host;
    SmithLangReplay *replay;
    const SmithLangInstance *instance;
    uint32_t location;
    uint32_t id;
} SmithLangNestedWriter;

static void smith_lang_nested_say(const SmithLangNestedWriter *w, SmithTokenKind kind, const char *text) {
    smith_lang_gen_put(w->lang, w->host, kind, text, w->location, w->id);
}

// The tokens [from, to) of the generic, written for the instance.
static void smith_lang_nested_range(const SmithLangNestedWriter *w, uint32_t from, uint32_t to) {
    smith_lang_replay_begin(w->replay, w->replay->generic, w->instance, w->id, w->location);
    if (to > from && smith_lang_replay_push(w->replay, from, to, 0)) smith_lang_replay_run(w->lang, w->host, w->replay);
}

static void smith_lang_nested_member(uint32_t from, uint32_t to, void *context) {
    const SmithLangNestedWriter *w = context;
    smith_lang_nested_range(w, from, to);
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, ";");
}

// The `return` that gives an inferred type: its expression is [*from, *to). 0: there is no `return` (or `return;`), 1:
// there is, -1: it is not the body's first statement.
static int smith_lang_nested_first_return(const SmithLangNested *nested, uint32_t *from, uint32_t *to) {
    for (uint32_t at = nested->body_from + 1; at < nested->body_to; at++) { // the body
        if (!smith_lang_gt_is(&smith_lang_gt.tokens[at], SmithTokenKind_Identifier, "return")) continue;
        if (at != nested->body_from + 1) return -1;
        int depth = 0;
        for (uint32_t i = at + 1; i < nested->body_to; i++) { // to the `;` outside any brackets
            const SmithLangGenToken *token = &smith_lang_gt.tokens[i];
            depth +=
                (smith_lang_gt_punct(token, "(") || smith_lang_gt_punct(token, "[") || smith_lang_gt_punct(token, "{"))
                    ? 1
                    : 0;
            depth -=
                (smith_lang_gt_punct(token, ")") || smith_lang_gt_punct(token, "]") || smith_lang_gt_punct(token, "}"))
                    ? 1
                    : 0;
            if (depth != 0 || !smith_lang_gt_punct(token, ";")) continue;
            *from = at + 1;
            *to = i;
            return i == at + 1 ? 0 : 1;
        }
        return -1;
    }
    return 0;
}

// The inferred return type, written before the name: `void`, or `__typeof_unqual__(e)` for the first `return e;`
// with the lambda's parameters as members of a compound literal of `<function>_args` (defined before). False after an
// error.
static bool smith_lang_nested_return_type(const SmithLangNestedWriter *w, const SmithLangNested *nested,
                                          const SmithLangNestedNames *names, uint32_t from, uint32_t to, int found) {
    if (found == 0) {
        smith_lang_nested_say(w, SmithTokenKind_Identifier, "void");
        return true;
    }
    smith_lang_nested_say(w, SmithTokenKind_Identifier, "__typeof_unqual__");
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, "(");
    for (size_t k = 0; k < names->count; k++)
        smith_lang_nested.probe_names[k] = names->names[k];
    smith_lang_nested.probe_count = names->count;
    snprintf(smith_lang_nested.probe_tag, sizeof smith_lang_nested.probe_tag, "%s_args", nested->mangled);
    smith_lang_nested_range(w, from, to);
    smith_lang_nested.probe_count = 0;
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, ")");
    return true;
}

// One lambda, written at file scope for the instance. False after an error.
static bool smith_lang_nested_write_one(const SmithLangNestedWriter *w, const SmithLangNested *nested) {
    uint32_t from = 0;
    uint32_t to = 0;
    int found = nested->has_type ? 1 : smith_lang_nested_first_return(nested, &from, &to);
    if (found < 0) {
        smith_lang_error(
            w->lang, w->host,
            smith_lang_gen_place(w->host, w->replay->generic, &smith_lang_gt.tokens[nested->start], w->location),
            "the type of this lambda is read from its first `return`, which must be the first statement "
            "of the body: write the return type, `[](...) -> T { ... }`");
        return false;
    }
    bool no_parameters =
        nested->params_to == nested->params_from ||
        (nested->params_to == nested->params_from + 1 &&
         smith_lang_gt_is(&smith_lang_gt.tokens[nested->params_from], SmithTokenKind_Identifier, "void"));
    SmithLangNestedNames names = {};
    if (!no_parameters)
        smith_lang_nested_each_param(nested->params_from, nested->params_to, smith_lang_nested_collect, &names);
    smith_lang_gen_note_break(w->lang);
    if (!nested->has_type && found == 1 && !no_parameters) { // the parameters as a struct, for the probe
        char tag[smith_lang_name_max];
        snprintf(tag, sizeof tag, "%s_args", nested->mangled);
        smith_lang_nested_say(w, SmithTokenKind_Identifier, "typedef");
        smith_lang_nested_say(w, SmithTokenKind_Identifier, "struct");
        smith_lang_nested_say(w, SmithTokenKind_Punctuator, "{");
        smith_lang_nested_each_param(nested->params_from, nested->params_to, smith_lang_nested_member, (void *)w);
        smith_lang_nested_say(w, SmithTokenKind_Punctuator, "}");
        smith_lang_nested_say(w, SmithTokenKind_Identifier, tag);
        smith_lang_nested_say(w, SmithTokenKind_Punctuator, ";");
    }
    smith_lang_nested_say(w, SmithTokenKind_Identifier, "static");
    if (nested->has_type) {
        smith_lang_nested_range(w, nested->type_from, nested->type_to);
    } else if (!smith_lang_nested_return_type(w, nested, &names, from, to, found)) {
        return false;
    }
    smith_lang_nested_say(w, SmithTokenKind_Identifier, nested->mangled);
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, "(");
    if (no_parameters) {
        smith_lang_nested_say(w, SmithTokenKind_Identifier, "void");
    } else {
        smith_lang_nested_range(w, nested->params_from, nested->params_to);
    }
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, ")");
    smith_lang_nested_range(w, nested->body_from, nested->end);
    return true;
}

// The lambdas in the body of the replay's generic, written at file scope for the instance: the struct of the
// arguments, then each function. False after an error.
static bool smith_lang_nested_write(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                    const SmithLangInstance *instance, uint32_t location, uint32_t id) {
    smith_lang_nested.pack_tag[0] = 0;
    smith_lang_nested.probe_count = 0;
    smith_lang_nested.failed = false;
    bool wants_pack = false;
    bool good = smith_lang_nested_scan(lang, host, replay, &wants_pack);
    if (wants_pack) smith_lang_nested_define_pack(lang, host, instance, location, id);
    smith_lang_nested.failed = !good;
    if (!good) return false; // the body still names its lambdas, so the error is the only one
    SmithLangNestedWriter writer = {lang, host, replay, instance, location, id};
    for (size_t k = 0; k < smith_lang_nested.count; k++) { // each function, as the instance would write its body
        if (smith_lang_nested_write_one(&writer, &smith_lang_nested.list[k])) continue;
        smith_lang_nested.failed = true;
        return false;
    }
    return true;
}

// ---- A lambda passed to a `Lambda(F)` parameter (issue #957)
// -----------------------------------------------------------------------------------------------------------
//
//   export void sort(auto xs[:], Lambda(bool (typeof(xs[0]) *, typeof(xs[0]) *)) less);
//   sort(xs, [](arithmetic *a, arithmetic *b) -> bool { return *a < *b; });
//
// The slot is a `constexpr auto` one: the lambda's tokens are the argument, so two lambdas that differ are two
// instances. The instance writes the lambda as a function before itself, `static R <instance>__arg<slot>(P) {...}`,
// with the types of the parameters written `auto` or as a concept taken from F (replayed with the instance's types)
// and the return type from `-> R`, else from F; a `_Static_assert` says F by name when the function's type is not F.
// A call of the parameter in the body is the function's name.

constexpr size_t smith_lang_argl_max = 8;

typedef struct {
    uint32_t captures_from, captures_to; // the tokens between `[` and `]`
    uint32_t params_from, params_to;     // the tokens between the parentheses
    bool has_type;
    uint32_t type_from, type_to; // `-> R`: the tokens of R
    uint32_t body_from, body_to; // the `{` and the `}`
} SmithLangArgl;

typedef struct {
    uint32_t ret_from, ret_to; // the tokens of F before its parameters
    size_t count;
    uint32_t from[smith_lang_argl_max], to[smith_lang_argl_max]; // each parameter's tokens
} SmithLangArglF;

static bool smith_lang_argl_is(const SmithLangPool *pool, const SmithLangGenToken *token, SmithTokenKind kind,
                               const char *literal) {
    return token->kind == kind && token->special == SmithLangGen_Plain &&
           smith_lang_equals(smith_lang_pool_text(pool, token), token->length, literal);
}

static bool smith_lang_argl_punct(const SmithLangPool *pool, uint32_t at, const char *literal) {
    return smith_lang_argl_is(pool, &pool->tokens[at], SmithTokenKind_Punctuator, literal);
}

// The token that closes the bracket opened at `open`, below `limit` (UINT32_MAX when none).
static uint32_t smith_lang_argl_match(const SmithLangPool *pool, uint32_t open, uint32_t limit, const char *opener,
                                      const char *closer) {
    int depth = 0;
    for (uint32_t at = open; at < limit; at++) { // bounded by the lambda
        depth += smith_lang_argl_punct(pool, at, opener) ? 1 : 0;
        depth -= smith_lang_argl_punct(pool, at, closer) ? 1 : 0;
        if (depth == 0) return at;
    }
    return UINT32_MAX;
}

// The parts of the lambda `[](P) -> R { body }` that is exactly [from, to) of the pool.
static bool smith_lang_argl_parse(const SmithLangPool *pool, uint32_t from, uint32_t to, SmithLangArgl *out) {
    if (to < from + 4 || !smith_lang_argl_punct(pool, from, "[")) return false;
    uint32_t list = from + 1;
    while (list < to && !smith_lang_argl_punct(pool, list, "]")) {
        bool name = pool->tokens[list].kind == SmithTokenKind_Identifier;
        bool comma = smith_lang_argl_punct(pool, list, ",");
        if (!name && !comma) return false; // a capture list holds names and commas only
        list += 1;
    }
    if (list >= to - 1) return false;
    SmithLangArgl found = {
        .captures_from = from + 1, .captures_to = list, .params_from = list + 1, .params_to = list + 1};
    uint32_t at = list + 1;
    if (smith_lang_argl_punct(pool, at, "(")) {
        uint32_t close = smith_lang_argl_match(pool, at, to, "(", ")");
        if (close == UINT32_MAX) return false;
        found.params_from = at + 1;
        found.params_to = close;
        at = close + 1;
    }
    if (at < to && smith_lang_argl_punct(pool, at, "->")) {
        found.has_type = true;
        found.type_from = at + 1;
        for (at = at + 1; at < to && !smith_lang_argl_punct(pool, at, "{"); at++) {
        }
        found.type_to = at;
    }
    if (at >= to || !smith_lang_argl_punct(pool, at, "{")) return false;
    uint32_t close = smith_lang_argl_match(pool, at, to, "{", "}");
    if (close != to - 1) return false;
    found.body_from = at;
    found.body_to = close;
    *out = found;
    return true;
}

// The declarations in [from, to) of the pool, split at the commas outside brackets: how many (SIZE_MAX: too many; a
// lone `void` is none).
static size_t smith_lang_argl_split_list(const SmithLangPool *pool, uint32_t from, uint32_t to,
                                         uint32_t starts[smith_lang_argl_max], uint32_t ends[smith_lang_argl_max]) {
    if (to == from ||
        (to == from + 1 && smith_lang_argl_is(pool, &pool->tokens[from], SmithTokenKind_Identifier, "void")))
        return 0;
    size_t count = 0;
    int depth = 0;
    uint32_t start = from;
    for (uint32_t at = from; at <= to; at++) { // bounded by the list
        bool end = at == to;
        if (!end) {
            const SmithLangGenToken *token = &pool->tokens[at];
            if (token->special == SmithLangGen_Plain && token->kind == SmithTokenKind_Punctuator &&
                token->length == 1) {
                char c = smith_lang_pool_text(pool, token)[0];
                depth += (c == '(' || c == '[' || c == '{') ? 1 : (c == ')' || c == ']' || c == '}') ? -1 : 0;
            }
        }
        if (!end && !(depth == 0 && smith_lang_argl_punct(pool, at, ","))) continue;
        if (count >= smith_lang_argl_max) return SIZE_MAX;
        starts[count] = start;
        ends[count++] = at;
        start = at + 1;
    }
    return count;
}

// F = `R (P0, P1)` at [from, to) of the generic's tokens.
static bool smith_lang_argl_f_split(uint32_t from, uint32_t to, SmithLangArglF *out) {
    if (to < from + 3 || !smith_lang_argl_punct(&smith_lang_gt, to - 1, ")")) return false;
    int depth = 0;
    uint32_t open = UINT32_MAX;
    for (uint32_t at = to; at > from; at--) { // back from the last `)` to its `(`
        depth += smith_lang_argl_punct(&smith_lang_gt, at - 1, ")") ? 1 : 0;
        depth -= smith_lang_argl_punct(&smith_lang_gt, at - 1, "(") ? 1 : 0;
        if (depth == 0) {
            open = at - 1;
            break;
        }
    }
    if (open == UINT32_MAX || open == from) return false;
    out->ret_from = from;
    out->ret_to = open;
    out->count = smith_lang_argl_split_list(&smith_lang_gt, open + 1, to - 1, out->from, out->to);
    return out->count != SIZE_MAX;
}

// The tokens [*from, *to) of F in `Lambda(F) name`, the parameter of slot `slot`: false when it is no such parameter.
static bool smith_lang_argl_slot(const SmithLangGeneric *generic, size_t slot, uint32_t *from, uint32_t *to) {
    if (slot >= generic->type_count || generic->type_kind[slot] != SmithLangSlot_Function) return false;
    SmithLangParam params[smith_lang_call_params_max];
    size_t count = smith_lang_generic_params(generic, params, smith_lang_call_params_max);
    size_t at = generic->type_position[slot];
    if (count == SIZE_MAX || at >= count || !smith_lang_param_is_lambda(&params[at])) return false;
    *from = params[at].start + 2;
    *to = params[at].end - 2;
    return true;
}

// Whether the declaration [from, to) of a lambda's parameter is `auto name` or `concept name` (stars between them:
// `auto *name`): its type comes from F, less the stars.
static bool smith_lang_argl_from_f(const SmithLangPool *pool, uint32_t from, uint32_t to) {
    if (to < from + 2) return false;
    for (uint32_t at = from + 1; at + 1 < to; at++) // the tokens between the type and the name
        if (!smith_lang_argl_punct(pool, at, "*")) return false;
    const SmithLangGenToken *type = &pool->tokens[from];
    if (smith_lang_argl_is(pool, type, SmithTokenKind_Identifier, "auto")) return true;
    for (size_t i = 0; i < sizeof smith_lang_concepts / sizeof smith_lang_concepts[0]; i++) { // 11 concepts
        if (smith_lang_argl_is(pool, type, SmithTokenKind_Identifier, smith_lang_concepts[i])) return true;
    }
    return false;
}

// The lambda at `span` of a call: checked against F at the call. False after the error.
static bool smith_lang_argl_check(SmithLang *lang, const SmithHost *host, const SmithLangGeneric *generic, size_t slot,
                                  SmithLangSpan span, uint32_t place) {
    uint32_t fa = 0;
    uint32_t fb = 0;
    char shown[256];
    smith_lang_argl_slot(generic, slot, &fa, &fb);
    smith_lang_infer_written((SmithLangSpan){fa, fb}, shown, sizeof shown);
    SmithLangArgl code;
    SmithLangArglF f;
    if (!smith_lang_argl_parse(&smith_lang_gt, span.start, span.end, &code)) {
        smith_lang_error(lang, host, place,
                         "the argument of `%s` is not a lambda: a lambda is `[](parameters) -> type { body }`",
                         smith_lang_str(generic->member));
        return false;
    }
    if (code.captures_to > code.captures_from) {
        const SmithLangGenToken *first = &smith_lang_gt.tokens[code.captures_from];
        smith_lang_error(lang, host, place,
                         "the lambda passed to `%s` captures `%.*s`: the capture list is read, but captures are not "
                         "lowered yet (#1074)",
                         smith_lang_str(generic->member), (int)first->length, smith_lang_gt_str(first));
        return false;
    }
    if (!smith_lang_argl_f_split(fa, fb, &f)) {
        smith_lang_error(lang, host, place, "`Lambda(%s)` of `%s` is not a function type: write `Lambda(R (P0, P1))`",
                         shown, smith_lang_str(generic->member));
        return false;
    }
    uint32_t starts[smith_lang_argl_max];
    uint32_t ends[smith_lang_argl_max];
    size_t count = smith_lang_argl_split_list(&smith_lang_gt, code.params_from, code.params_to, starts, ends);
    if (count != f.count) {
        smith_lang_error(
            lang, host, place, "the lambda passed to `%s` takes %zu parameter%s, but its type `Lambda(%s)` has %zu",
            smith_lang_str(generic->member), count == SIZE_MAX ? 0 : count, count == 1 ? "" : "s", shown, f.count);
        return false;
    }
    for (size_t k = 0; k < count; k++) { // `[](a, b)` has no types to take from F
        if (ends[k] - starts[k] >= 2) continue;
        smith_lang_error(lang, host, place,
                         "a parameter of the lambda passed to `%s` needs a type: write `auto %.*s` or a concept "
                         "(`Num %.*s`), whose type comes from `Lambda(%s)`",
                         smith_lang_str(generic->member), (int)smith_lang_gt.tokens[starts[k]].length,
                         smith_lang_gt_str(&smith_lang_gt.tokens[starts[k]]),
                         (int)smith_lang_gt.tokens[starts[k]].length,
                         smith_lang_gt_str(&smith_lang_gt.tokens[starts[k]]), shown);
        return false;
    }
    return true;
}

// The name of the function that stands for the argument of slot `slot` when it is a lambda.
static void smith_lang_argl_name(const SmithLangInstance *instance, unsigned slot, char *out, size_t capacity) {
    snprintf(out, capacity, "%s__arg%u", smith_lang_str(instance->cname), slot);
}

// The tokens [from, to) of the instance's pool, as they are.
static void smith_lang_argl_copy(const SmithLangNestedWriter *w, uint32_t from, uint32_t to) {
    for (uint32_t at = from; at < to; at++) { // the lambda's tokens
        const SmithLangGenToken *token = &smith_lang_it.tokens[at];
        if (token->special != SmithLangGen_Plain) continue;
        smith_lang_gen_put_token(w->lang, w->host, &smith_lang_it, token, token->location, w->id, false);
    }
}

// The tokens [from, to) of F, written for the instance as a head is (`typeof(xs[0])` is the slice's element).
static void smith_lang_argl_run(const SmithLangNestedWriter *w, uint32_t from, uint32_t to) {
    smith_lang_gen_put_run(w->lang, w->host, w->replay->generic, w->instance, from, to, w->location, w->id);
}

// The lambda `code` as the function `name`: the return type of `-> R`, else F's, and the parameters.
static void smith_lang_argl_put_function(const SmithLangNestedWriter *w, const SmithLangArglF *f,
                                         const SmithLangArgl *code, const char *name) {
    smith_lang_nested_say(w, SmithTokenKind_Identifier, "static");
    if (code->has_type) {
        smith_lang_argl_copy(w, code->type_from, code->type_to);
    } else {
        smith_lang_argl_run(w, f->ret_from, f->ret_to);
    }
    smith_lang_nested_say(w, SmithTokenKind_Identifier, name);
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, "(");
    uint32_t starts[smith_lang_argl_max];
    uint32_t ends[smith_lang_argl_max];
    size_t params = smith_lang_argl_split_list(&smith_lang_it, code->params_from, code->params_to, starts, ends);
    if (params == 0 || params == SIZE_MAX) smith_lang_nested_say(w, SmithTokenKind_Identifier, "void");
    for (size_t k = 0; params != SIZE_MAX && k < params && k < f->count; k++) { // each parameter
        if (k > 0) smith_lang_nested_say(w, SmithTokenKind_Punctuator, ",");
        if (smith_lang_argl_from_f(&smith_lang_it, starts[k], ends[k])) {
            uint32_t stars = ends[k] - starts[k] - 2; // F's own stars stand for these
            uint32_t to = f->to[k];
            while (stars > 0 && to > f->from[k] && smith_lang_argl_punct(&smith_lang_gt, to - 1, "*")) {
                to -= 1;
                stars -= 1;
            }
            smith_lang_argl_run(w, f->from[k], to);
            for (uint32_t i = f->to[k] - to; i > 0; i--) // the stars the lambda wrote, as F's
                smith_lang_nested_say(w, SmithTokenKind_Punctuator, "*");
            smith_lang_argl_copy(w, ends[k] - 1, ends[k]);
        } else {
            smith_lang_argl_copy(w, starts[k], ends[k]);
        }
    }
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, ")");
    smith_lang_argl_copy(w, code->body_from, code->body_to + 1);
}

// One slot's function and the check of its type. Written at file scope before the instance.
static void smith_lang_argl_write_one(const SmithLangNestedWriter *w, const SmithLangGeneric *generic, size_t slot,
                                      uint32_t fa, uint32_t fb) {
    const SmithLangInstance *instance = w->instance;
    uint32_t first = instance->arg_first[slot];
    uint32_t count = instance->arg_count[slot];
    SmithLangArglF f;
    if (!smith_lang_argl_f_split(fa, fb, &f)) return; // reported at the call
    char name[smith_lang_name_max];
    smith_lang_argl_name(instance, (unsigned)slot, name, sizeof name);
    smith_lang_gen_note_break(w->lang);
    const SmithLangGenToken *head = &smith_lang_it.tokens[first];
    SmithLangArgl code;
    bool is_lambda = count > 1 && smith_lang_argl_parse(&smith_lang_it, first, first + count, &code);
    if (count == 1 &&
        smith_lang_generic_of(w->lang, w->host, smith_lang_pool_text(&smith_lang_it, head), head->length) >= 0)
        return; // a generic function: its calls make the instances
    if (is_lambda) smith_lang_argl_put_function(w, &f, &code, name);
    char shown[256];
    smith_lang_infer_written((SmithLangSpan){fa, fb}, shown, sizeof shown);
    char message[512];
    snprintf(message, sizeof message, "\"smith: the argument for `%s` of `%s` does not have the type of `Lambda(%s)`\"",
             smith_lang_gt_str(&smith_lang_gt.tokens[generic->first + generic->type_token[slot]]),
             smith_lang_str(generic->member), shown);
    for (char *c = message + 1; *c != 0 && c[1] != 0; c++) { // the text sits in a string
        if (*c == '"' || *c == '\\') *c = '\'';
    }
    smith_lang_nested_say(w, SmithTokenKind_Identifier, "_Static_assert");
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, "(");
    smith_lang_nested_say(w, SmithTokenKind_Identifier, "__builtin_types_compatible_p");
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, "(");
    smith_lang_nested_say(w, SmithTokenKind_Identifier, "__typeof__");
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, "(");
    if (is_lambda) {
        smith_lang_nested_say(w, SmithTokenKind_Identifier, name);
    } else {
        smith_lang_argl_copy(w, first, first + count);
    }
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, ")");
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, ",");
    smith_lang_argl_run(w, fa, fb);
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, ")");
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, ",");
    smith_lang_nested_say(w, SmithTokenKind_String, message);
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, ")");
    smith_lang_nested_say(w, SmithTokenKind_Punctuator, ";");
}

// The functions of the instance's lambdas, and the check of every `Lambda(F)` argument, before the instance.
static void smith_lang_argl_write(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                  const SmithLangInstance *instance, uint32_t location, uint32_t id) {
    const SmithLangGeneric *generic = &smith_lang_generics[instance->generic];
    SmithLangNestedWriter writer = {lang, host, replay, instance, location, id};
    for (size_t slot = 0; slot < generic->type_count; slot++) { // the slots
        uint32_t fa = 0;
        uint32_t fb = 0;
        if (smith_lang_argl_slot(generic, slot, &fa, &fb) && instance->arg_count[slot] > 0)
            smith_lang_argl_write_one(&writer, generic, slot, fa, fb);
    }
}
