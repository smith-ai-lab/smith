// A function defined in a generic's body, and the struct of its arguments (issue #316, the first slice of lambdas).
// Included by runtime/lang_generic.c, before the replay is run.
//
//   export void spawn_like(constexpr auto f, auto... rest) {
//       static void entry(void *raw) {                  // a function of the instance, not a local one
//           typeof(rest) *a = raw;                      // the struct of the arguments
//           f(a->[rest]...);                            // ... and its members, one for each argument
//       }
//       run(entry, ...);                                // its address is the pointer
//   }
//
// A `static` function defined directly in the body (its outermost block) is written once per instance, at file scope
// before the instance, as `static R <instance>__name(P) {...}`; the body names it as written. It sees the generic's
// compile-time names (`T`, `f`, `typeof(rest[i])`, `countof(rest)`, a number parameter) but no local or parameter of
// the generic: it is no closure. `typeof(rest)` is the struct `smith_pack_<instance>` whose members `_0`, `_1`, ...
// have the types of the arguments, defined before the instance; `x->[rest]...` and `x.[rest]...` are those members
// as the arguments of a call or an initializer. Both lower to plain C.

static void smith_lang_replay_run(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay);
static void smith_lang_replay_begin(SmithLangReplay *replay, const SmithLangGeneric *generic,
                                    const SmithLangInstance *instance, uint32_t id, uint32_t location);

constexpr size_t smith_lang_nested_max = 8;

typedef struct {
    uint32_t start; // [start, end) of smith_lang_gt: `static R name(P) { ... }`
    uint32_t end;
    uint32_t name; // the token of the name
    char mangled[smith_lang_name_max];
} SmithLangNested;

typedef struct {
    SmithLangNested list[smith_lang_nested_max];
    size_t count;
    int hoisting; // the one being written (-1: none): its own range is not skipped
    char pack_tag[smith_lang_name_max];
} SmithLangNestedState;

static SmithLangNestedState smith_lang_nested = {.hoisting = -1};

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

// Whether a token begins a statement of the body's outermost block.
static bool smith_lang_nested_statement_start(uint32_t at, uint32_t first) {
    if (at == first) return false;
    const SmithLangGenToken *before = &smith_lang_gt.tokens[at - 1];
    return smith_lang_gt_punct(before, ";") || smith_lang_gt_punct(before, "{") || smith_lang_gt_punct(before, "}");
}

// The function defined at `at` (a `static`) when it is one: its end and the name of it.
static bool smith_lang_nested_definition(uint32_t at, uint32_t limit, SmithLangNested *out) {
    uint32_t paren = UINT32_MAX;
    for (uint32_t i = at + 1; i < limit && paren == UINT32_MAX; i++) { // the first `(`, `=`, `;` or `{`
        const SmithLangGenToken *token = &smith_lang_gt.tokens[i];
        if (token->special != SmithLangGen_Plain) continue;
        if (smith_lang_gt_punct(token, "=") || smith_lang_gt_punct(token, ";") || smith_lang_gt_punct(token, "{")) {
            return false;
        }
        if (smith_lang_gt_punct(token, "(")) paren = i;
    }
    if (paren == UINT32_MAX || paren <= at + 1) return false;
    uint32_t close = smith_lang_nested_close(paren, limit, "(", ")");
    if (close == UINT32_MAX || close + 1 >= limit || !smith_lang_gt_punct(&smith_lang_gt.tokens[close + 1], "{")) {
        return false;
    }
    uint32_t end = smith_lang_nested_close(close + 1, limit, "{", "}");
    if (end == UINT32_MAX || smith_lang_gt.tokens[paren - 1].kind != SmithTokenKind_Identifier) return false;
    *out = (SmithLangNested){.start = at, .end = end + 1, .name = paren - 1};
    return true;
}

// The functions defined in the body of the generic of `replay`, and whether the body asks for `typeof(rest)`.
static bool smith_lang_nested_scan(SmithLang *lang, const SmithHost *host, const SmithLangReplay *replay,
                                   bool *wants_pack) {
    const SmithLangGeneric *generic = replay->generic;
    uint32_t first = generic->first + generic->body_index;
    uint32_t limit = generic->first + generic->count;
    smith_lang_nested.count = 0;
    smith_lang_nested.hoisting = -1;
    *wants_pack = false;
    int depth = 0;
    for (uint32_t at = first; at < limit; at++) { // the body's tokens
        SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (token->special != SmithLangGen_Plain) continue;
        depth += smith_lang_gt_punct(token, "{") ? 1 : 0;
        depth -= smith_lang_gt_punct(token, "}") ? 1 : 0;
        if (smith_lang_has_pack(generic) && smith_lang_gt_is(token, SmithTokenKind_Identifier, "typeof") &&
            at + 3 < limit && smith_lang_gt_punct(token + 1, "(") && smith_lang_gt_punct(token + 3, ")")) {
            SmithLangPackRef ref;
            *wants_pack = *wants_pack || smith_lang_pack_resolve(replay, at + 2, &ref);
        }
        SmithLangNested found;
        if (!smith_lang_gt_is(token, SmithTokenKind_Identifier, "static") ||
            !smith_lang_nested_statement_start(at, first) || !smith_lang_nested_definition(at, limit, &found)) {
            continue;
        }
        if (depth != 1) {
            smith_lang_error(lang, host, smith_lang_gen_place(host, generic, token, replay->location),
                             "a function defined in a generic goes in the outermost block of its body: it is written "
                             "once for the instance, not where it stands");
            return false;
        }
        if (smith_lang_nested.count >= smith_lang_nested_max) {
            smith_lang_error(lang, host, smith_lang_gen_place(host, generic, token, replay->location),
                             "a generic defines at most %zu functions in its body", smith_lang_nested_max);
            return false;
        }
        const SmithLangGenToken *name = &smith_lang_gt.tokens[found.name];
        snprintf(found.mangled, sizeof found.mangled, "%s__%.*s", smith_lang_str(replay->instance->cname),
                 (int)name->length, smith_lang_gt_str(name));
        smith_lang_nested.list[smith_lang_nested.count++] = found;
        at = found.end - 1;
        depth = 1;
    }
    return true;
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

// What the body's replay does with a nested function and `typeof(rest)`. True when it took tokens.
static bool smith_lang_nested_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                     SmithLangFrame *frame) {
    if (replay->instance == nullptr || (smith_lang_nested.count == 0 && smith_lang_nested.pack_tag[0] == 0))
        return false;
    SmithLangGenToken *token = &smith_lang_gt.tokens[frame->pos];
    if (token->special != SmithLangGen_Plain) return false;
    for (size_t k = 0; k < smith_lang_nested.count; k++) { // the definitions are written apart
        const SmithLangNested *nested = &smith_lang_nested.list[k];
        if (frame->pos == nested->start && smith_lang_nested.hoisting != (int)k) {
            frame->pos = nested->end;
            return true;
        }
        if (token->kind == SmithTokenKind_Identifier && token->length == smith_lang_gt.tokens[nested->name].length &&
            memcmp(smith_lang_gt_str(token), smith_lang_gt_str(&smith_lang_gt.tokens[nested->name]), token->length) ==
                0 &&
            !smith_lang_member_before(frame->pos) && token->param == 0) {
            uint32_t place = smith_lang_gen_place(host, replay->generic, token, replay->location);
            smith_lang_gen_put(lang, host, SmithTokenKind_Identifier, nested->mangled, place, replay->instance_id);
            frame->pos += 1;
            return true;
        }
    }
    SmithLangPackRef ref;
    if (smith_lang_nested.pack_tag[0] != 0 && frame->pos + 3 < frame->end &&
        smith_lang_gt_is(token, SmithTokenKind_Identifier, "typeof") && smith_lang_gt_punct(token + 1, "(") &&
        smith_lang_pack_resolve(replay, frame->pos + 2, &ref) && smith_lang_gt_punct(token + 3, ")")) {
        uint32_t place = smith_lang_gen_place(host, replay->generic, token, replay->location);
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

// The functions defined in the body of the replay's generic, written at file scope for the instance: the struct of the
// arguments, then each function. False after an error.
static bool smith_lang_nested_write(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                    const SmithLangInstance *instance, uint32_t location, uint32_t id) {
    smith_lang_nested.pack_tag[0] = 0;
    bool wants_pack = false;
    if (!smith_lang_nested_scan(lang, host, replay, &wants_pack)) {
        smith_lang_nested.count = 0;
        return false;
    }
    if (wants_pack) smith_lang_nested_define_pack(lang, host, instance, location, id);
    for (size_t k = 0; k < smith_lang_nested.count; k++) { // each function, as the instance would write its body
        const SmithLangNested *nested = &smith_lang_nested.list[k];
        smith_lang_replay_begin(replay, replay->generic, instance, id, location);
        smith_lang_nested.hoisting = (int)k;
        smith_lang_gen_note_break(lang);
        smith_lang_replay_push(replay, nested->start, nested->end, 0);
        smith_lang_replay_run(lang, host, replay);
    }
    smith_lang_nested.hoisting = -1;
    return true;
}
