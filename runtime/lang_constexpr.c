// `if constexpr` in a generic's body (issue #15). Included by runtime/lang_generic.c.
//
//   if constexpr (T == f32) { ... } else if constexpr (sizeof(T) == 8) { ... } else { static_assert(false); }
//
// The body of a generic is recorded as tokens, and `if constexpr` is one token of its own in the record (the lowering
// took the `constexpr` out). When an instance is written, the condition is evaluated for the types of that instance and
// the branch it chooses is written; the other is never given to the parser, so it may name what T does not have.
//
// The condition is a constant expression over: integer literals, `true` and `false`, the operators of C, `sizeof(T)`
// and `alignof(T)`, `T == U` and `T != U` (two types are equal when the parser's canonical spellings are, so `u32 ==
// uint32_t`), and `is_integer(T)`, `is_float`, `is_signed`, `is_pointer`, `is_bool`, `is_enum` and `is_record`. A type
// is written as the tokens of C's type names: T, the sized types, `struct S`, a typedef, qualifiers and `*`.

constexpr size_t smith_lang_cx_stack_max = 32;
constexpr size_t smith_lang_cx_types_max = 8;
constexpr size_t smith_lang_cx_name_max = 256;

static void smith_lang_word_state(SmithLang *lang, const SmithToken *token, SmithLangWord word);
static void smith_lang_gen_mark(SmithLang *lang, SmithLangGenSpecial special, const char *text, size_t length,
                                uint32_t location);

typedef struct {
    bool is_type;
    int64_t number;
    uint8_t type; // is_type: the index of its canonical spelling
} SmithLangCxValue;

typedef struct {
    uint8_t code; // a binary operator (an index of smith_lang_cx_binary), a unary one (100 + a character), or open
    int8_t precedence;
} SmithLangCxOp;

typedef struct {
    SmithLang *lang;
    const SmithHost *host;
    const SmithLangInstance *instance;
    const SmithLangReplay *replay; // the lists and loop variables the condition may name
    uint32_t location;
    bool unknown_name; // the evaluation failed at a name that is no constant or type: `unknown_at` is its token
    uint32_t unknown_at;
    char canonical[smith_lang_cx_types_max][smith_lang_cx_name_max];
    size_t canonical_count;
    char message[smith_lang_message_max];
    SmithLangCxValue values[smith_lang_cx_stack_max];
    size_t value_count;
    SmithLangCxOp ops[smith_lang_cx_stack_max];
    size_t op_count;
} SmithLangCx;

typedef struct {
    const char *text;
    int8_t precedence;
} SmithLangCxBinary;

static const SmithLangCxBinary smith_lang_cx_binary[] = {
    {"||", 1}, {"&&", 2}, {"|", 3},  {"^", 4},  {"&", 5}, {"==", 6}, {"!=", 6}, {"<", 7},  {">", 7},
    {"<=", 7}, {">=", 7}, {"<<", 8}, {">>", 8}, {"+", 9}, {"-", 9},  {"*", 10}, {"/", 10}, {"%", 10},
};
constexpr uint32_t smith_lang_cx_value_nodes = 4096;
constexpr uint8_t smith_lang_cx_open = 200;
constexpr uint8_t smith_lang_cx_unary = 100;
constexpr int8_t smith_lang_cx_unary_precedence = 11;

[[gnu::format(printf, 2, 3)]] static bool smith_lang_cx_fail(SmithLangCx *cx, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(cx->message, sizeof cx->message, format, args);
    va_end(args);
    return false;
}

static SmithLangGenToken *smith_lang_cx_token(uint32_t at) {
    return &smith_lang_gt.tokens[at];
}

static bool smith_lang_cx_eval(SmithLangCx *cx, uint32_t first, uint32_t last, int64_t *result);
static uint32_t smith_lang_cx_primary_end(uint32_t at, uint32_t last);
static bool smith_lang_cx_ask(SmithLangCx *cx, uint32_t *at, uint32_t end, SmithLangCxValue *value);

static bool smith_lang_cx_word(const SmithLangGenToken *token, const char *word) {
    return smith_lang_gt_is(token, SmithTokenKind_Identifier, word);
}

// The index after the group that begins at `at` (an opening bracket), or UINT32_MAX when it does not close before
// `limit`.
static uint32_t smith_lang_cx_skip_group(uint32_t at, uint32_t limit) {
    int depth = 0;
    for (; at < limit; at++) { // bounded by the generic
        const SmithLangGenToken *token = smith_lang_cx_token(at);
        depth += token->special == SmithLangGen_Plain && smith_lang_gt_open(token) ? 1 : 0;
        depth -= token->special == SmithLangGen_Plain && smith_lang_gt_close(token) ? 1 : 0;
        if (depth == 0) return at + 1;
    }
    return UINT32_MAX;
}

static bool smith_lang_cx_is_if(const SmithLangGenToken *token) {
    return token->special == SmithLangGen_IfConstexpr || smith_lang_cx_word(token, "if");
}

// The index after the statement that begins at `at`: a block, an `if` with its `else`, a loop, or tokens up to a `;`.
// UINT32_MAX when it does not end before `limit`. Nested `if`s are tracked in a bit stack, not by recursion.
static uint32_t smith_lang_cx_statement_end(uint32_t at, uint32_t limit) {
    uint32_t then_part = 0; // bit n: the n-th open statement is the then-branch of an `if` (an `else` may follow)
    unsigned depth = 0;
    for (size_t guard = 0; guard < smith_lang_gt_max; guard++) { // bounded by the pool
        if (at >= limit) return UINT32_MAX;
        const SmithLangGenToken *token = smith_lang_cx_token(at);
        bool done = false;
        bool control = smith_lang_cx_is_if(token) || smith_lang_cx_word(token, "for") ||
                       token->special == SmithLangGen_ForConstexpr || smith_lang_cx_word(token, "while") ||
                       smith_lang_cx_word(token, "switch");
        if (control) {
            bool is_if = smith_lang_cx_is_if(token);
            if (at + 1 >= limit || !smith_lang_gt_punct(smith_lang_cx_token(at + 1), "(") || depth >= 32) {
                return UINT32_MAX;
            }
            at = smith_lang_cx_skip_group(at + 1, limit);
            if (at == UINT32_MAX) return UINT32_MAX;
            then_part = (then_part & ~(1U << depth)) | ((is_if ? 1U : 0U) << depth);
            depth += 1;
        } else if (smith_lang_gt_punct(token, "{")) {
            at = smith_lang_cx_skip_group(at, limit);
            if (at == UINT32_MAX) return UINT32_MAX;
            done = true;
        } else {
            int nesting = 0;
            for (; at < limit; at++) { // a statement of tokens: to its `;`
                const SmithLangGenToken *inner = smith_lang_cx_token(at);
                nesting += inner->special == SmithLangGen_Plain && smith_lang_gt_open(inner) ? 1 : 0;
                nesting -= inner->special == SmithLangGen_Plain && smith_lang_gt_close(inner) ? 1 : 0;
                if (nesting == 0 && smith_lang_gt_punct(inner, ";")) break;
            }
            if (at >= limit) return UINT32_MAX;
            at += 1;
            done = true;
        }
        while (done) { // the statements this one ends
            if (depth == 0) return at;
            depth -= 1;
            if (((then_part >> depth) & 1U) != 0 && at < limit && smith_lang_cx_word(smith_lang_cx_token(at), "else")) {
                at += 1;
                then_part &= ~(1U << depth);
                depth += 1;
                done = false; // the `else` branch begins
            }
        }
    }
    return UINT32_MAX;
}

#include "lang_fields.c"

// ---- Types in a condition ------------------------------------------------------------------------------------------

static bool smith_lang_cx_type_word(const SmithLangGenToken *token) {
    static const char *const words[] = {"void",     "char",     "short",    "int",    "long",  "float",
                                        "double",   "signed",   "unsigned", "_Bool",  "bool",  "const",
                                        "volatile", "restrict", "_Complex", "struct", "union", "enum"};
    if (token->kind != SmithTokenKind_Identifier || token->special != SmithLangGen_Plain) return false;
    if (token->param != 0) return true;
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) { // 18 words
        if (smith_lang_equals(smith_lang_gt_str(token), token->length, words[i])) return true;
    }
    const char *text = smith_lang_gt_str(token);
    return token->length > 8 && memcmp(text, "__", 2) == 0 && memcmp(text + token->length - 7, "_TYPE__", 7) == 0;
}

// Whether the tokens at `at` are `typeof ( list [`: the type of one element of a list, which the instance knows.
static bool smith_lang_cx_typeof_element_at(const SmithLangCx *cx, uint32_t at) {
    if (cx->replay == nullptr || at + 3 >= smith_lang_gt.count) return false;
    const SmithLangGenToken *token = smith_lang_cx_token(at);
    SmithLangListRef list;
    return (smith_lang_cx_word(token, "typeof") || smith_lang_cx_word(token, "__typeof__")) &&
           smith_lang_gt_punct(smith_lang_cx_token(at + 1), "(") &&
           smith_lang_list_resolve(cx->replay, at + 2, &list) && smith_lang_gt_punct(smith_lang_cx_token(at + 3), "[");
}

// Whether the tokens at `at` begin a type: T, a builtin type word, or a name the parser knows as a type.
static bool smith_lang_cx_type_start(SmithLangCx *cx, uint32_t at) {
    const SmithLangGenToken *token = smith_lang_cx_token(at);
    if (smith_lang_cx_typeof_element_at(cx, at) || smith_lang_fields_typeof_end(at, smith_lang_gt.count) != 0) {
        return true;
    }
    if (smith_lang_cx_type_word(token)) return true;
    if (token->kind != SmithTokenKind_Identifier || token->special != SmithLangGen_Plain) return false;
    return cx->host->lookup(cx->host->context, smith_lang_gt_str(token), token->length, SmithLookup_Type).kind ==
           SmithLookup_Type;
}

// The index after the type that begins at `at`: its words, a tag after struct/union/enum, its `*`s, and one name the
// parser knows as a type (`c::V2`, a C struct or vector typedef).
static uint32_t smith_lang_cx_type_end(SmithLangCx *cx, uint32_t at, uint32_t last) {
    bool named = false;
    uint32_t field_end = smith_lang_fields_typeof_end(at, smith_lang_gt.count);
    if (field_end != 0) return field_end;
    if (smith_lang_cx_typeof_element_at(cx, at)) {
        uint32_t after = smith_lang_cx_skip_group(at + 1, last);
        return after == UINT32_MAX ? at : after;
    }
    for (; at < last; at++) { // bounded by the condition
        const SmithLangGenToken *token = smith_lang_cx_token(at);
        bool tag = at > 0 && (smith_lang_cx_word(smith_lang_cx_token(at - 1), "struct") ||
                              smith_lang_cx_word(smith_lang_cx_token(at - 1), "union") ||
                              smith_lang_cx_word(smith_lang_cx_token(at - 1), "enum"));
        bool word = smith_lang_cx_type_word(token);
        bool name = !named && !word && !tag && smith_lang_cx_type_start(cx, at);
        bool part =
            word || name || smith_lang_gt_punct(token, "*") || (tag && token->kind == SmithTokenKind_Identifier);
        if (!part) break;
        named = named || name;
    }
    return at;
}

// A small constant for an index (no recursion into the evaluator, which asks for types): terms of a number, the
// variable of a `for constexpr` or `countof(list)`, joined by `+` and `-`.
static bool smith_lang_cx_simple(SmithLangCx *cx, uint32_t first, uint32_t last, int64_t *result) {
    int64_t total = 0;
    int sign = 1;
    bool want_term = true;
    for (uint32_t at = first; at < last;) { // bounded by the index
        const SmithLangGenToken *token = smith_lang_cx_token(at);
        SmithLangListRef list;
        int64_t term = 0;
        if (want_term && token->kind == SmithTokenKind_Number) {
            term = (int64_t)strtoull(smith_lang_gt_str(token), nullptr, 0);
            at += 1;
        } else if (want_term && smith_lang_var_value(cx->replay, at, &term)) {
            at += 1;
        } else if (want_term && smith_lang_fields_countof_at(at, last)) {
            if (!smith_lang_fields_count(cx->host, cx->replay->generic, cx->instance, at + 2, cx->location, &term,
                                         cx->message, sizeof cx->message)) {
                return false;
            }
            at += smith_lang_fields_countof_len(at, last);
        } else if (want_term && smith_lang_list_countof_at(cx->replay, at, last, &list)) {
            term = (int64_t)list.count;
            at += 4;
        } else if (!want_term && (smith_lang_gt_punct(token, "+") || smith_lang_gt_punct(token, "-"))) {
            sign = smith_lang_gt_punct(token, "-") ? -1 : 1;
            want_term = true;
            at += 1;
            continue;
        } else {
            return smith_lang_cx_fail(cx,
                                      "the index of `list[...]` here is a number, the variable of a `for constexpr` or "
                                      "`countof(list)`, with `+` and `-`");
        }
        total += sign * term;
        want_term = false;
    }
    if (want_term) return smith_lang_cx_fail(cx, "the index of `typeof(list[...])` ends where a term is expected");
    *result = total;
    return true;
}

// The type of an element of the argument list, `typeof(rest[i])`, the tokens [first, last): the instance's own type
// for that argument, staged in place (it is in the pool of types already).
static bool smith_lang_cx_element_type(SmithLangCx *cx, uint32_t first, uint32_t last, uint32_t *staged_first,
                                       uint32_t *staged_count) {
    SmithLangListRef list;
    smith_lang_list_resolve(cx->replay, first + 2, &list);
    uint32_t after = smith_lang_cx_skip_group(first + 3, last);
    if (after == UINT32_MAX || after + 1 != last || after - 1 <= first + 4) {
        return smith_lang_cx_fail(cx, "`typeof(%s[...])` is written `typeof(%s[i])`", list.name, list.name);
    }
    int64_t index = 0;
    if (!smith_lang_cx_simple(cx, first + 4, after - 1, &index)) return false;
    if (!list.pack) {
        return smith_lang_cx_fail(cx,
                                  "the type of `%s[%lld]` is not known while the body is written: a list made with "
                                  "braces has the types of its elements only in the C that is written; ask the "
                                  "argument list (`typeof(rest[i])`)",
                                  list.name, (long long)index);
    }
    if (index < 0 || (uint64_t)index >= list.count) {
        return smith_lang_cx_fail(cx, "`%s[%lld]` is out of range: `%s` has %zu argument%s", list.name,
                                  (long long)index, list.name, list.count, list.count == 1 ? "" : "s");
    }
    size_t entry = smith_lang_pack_base(cx->replay->generic) + (size_t)index;
    *staged_first = cx->instance->arg_first[entry];
    *staged_count = cx->instance->arg_count[entry];
    return true;
}

// What the parser says of the type written by the tokens [first, last), with T as the instance has it. The canonical
// spelling is kept in the evaluation (its index in `*canonical`).
static bool smith_lang_cx_query(SmithLangCx *cx, uint32_t first, uint32_t last, SmithTypeQuery *query,
                                uint8_t *canonical) {
    if (cx->host->type_query == nullptr || cx->canonical_count >= smith_lang_cx_types_max) {
        return smith_lang_cx_fail(cx, "this condition has too many types, or the compiler cannot answer for them");
    }
    uint32_t mark[2] = {smith_lang_it.count, smith_lang_it.text_used};
    uint32_t staged_first = 0;
    uint32_t staged_count = 0;
    SmithLangSpan span = {first, last};
    char field_spelling[smith_lang_cx_name_max] = "";
    bool field = smith_lang_fields_typeof_end(first, last) == last;
    if (field && !smith_lang_cx_field_type(cx, first, last, field_spelling, sizeof field_spelling)) return false;
    if (field) {
        staged_count = 0;
    } else if (smith_lang_cx_typeof_element_at(cx, first) && smith_lang_cx_skip_group(first + 1, last) == last) {
        if (!smith_lang_cx_element_type(cx, first, last, &staged_first, &staged_count)) return false;
    } else if (!smith_lang_stage_type(span, cx->instance, cx->location, &staged_first, &staged_count)) {
        return smith_lang_cx_fail(cx, "this type is too long");
    }
    char spelling[smith_lang_cx_name_max];
    if (field)
        snprintf(spelling, sizeof spelling, "%s", field_spelling);
    else
        smith_lang_it_spelling(staged_first, staged_count, spelling, sizeof spelling);
    smith_lang_it.count = mark[0];
    smith_lang_it.text_used = mark[1];
    *query = (SmithTypeQuery){};
    if (!cx->host->type_query(cx->host->context, spelling, (uint32_t)strlen(spelling), query)) {
        return smith_lang_cx_fail(cx, "the compiler cannot read the type `%s`", spelling);
    }
    char *slot = cx->canonical[cx->canonical_count];
    snprintf(slot, smith_lang_cx_name_max, "%.*s", (int)query->canonical_length, query->canonical);
    *canonical = (uint8_t)cx->canonical_count++;
    return true;
}

// ---- Operands and operators
// ------------------------------------------------------------------------------------------

static bool smith_lang_cx_push_value(SmithLangCx *cx, SmithLangCxValue value) {
    if (cx->value_count >= smith_lang_cx_stack_max) return smith_lang_cx_fail(cx, "this condition is too deep");
    cx->values[cx->value_count++] = value;
    return true;
}

static bool smith_lang_cx_number(SmithLangCx *cx, const SmithLangCxValue *value) {
    if (value->is_type) return smith_lang_cx_fail(cx, "a type is not a number; compare it with == or !=");
    return true;
}

static bool smith_lang_cx_apply_unary(SmithLangCx *cx, char op) {
    if (cx->value_count < 1) return smith_lang_cx_fail(cx, "an operator has no operand");
    SmithLangCxValue *value = &cx->values[cx->value_count - 1];
    if (!smith_lang_cx_number(cx, value)) return false;
    if (op == '!') value->number = value->number == 0 ? 1 : 0;
    if (op == '-') value->number = (int64_t)(0 - (uint64_t)value->number);
    if (op == '~') value->number = (int64_t)~(uint64_t)value->number;
    return true;
}

static bool smith_lang_cx_compare_types(SmithLangCx *cx, const SmithLangCxValue *left, const SmithLangCxValue *right,
                                        bool equal, SmithLangCxValue *result) {
    if (!left->is_type || !right->is_type) {
        return smith_lang_cx_fail(cx, "a type can only be compared with another type");
    }
    bool same = strcmp(cx->canonical[left->type], cx->canonical[right->type]) == 0;
    *result = (SmithLangCxValue){.number = same == equal ? 1 : 0};
    return true;
}

static bool smith_lang_cx_arithmetic(SmithLangCx *cx, uint8_t op, int64_t a, int64_t b, int64_t *out) {
    const char *text = smith_lang_cx_binary[op].text;
    uint64_t ua = (uint64_t)a;
    uint64_t ub = (uint64_t)b;
    if (strcmp(text, "+") == 0) *out = (int64_t)(ua + ub);
    if (strcmp(text, "-") == 0) *out = (int64_t)(ua - ub);
    if (strcmp(text, "*") == 0) *out = (int64_t)(ua * ub);
    if (strcmp(text, "/") == 0 || strcmp(text, "%") == 0) {
        if (b == 0) return smith_lang_cx_fail(cx, "division by zero");
        *out = strcmp(text, "/") == 0 ? a / b : a % b;
    }
    if (strcmp(text, "<<") == 0) *out = b < 0 || b > 63 ? 0 : (int64_t)(ua << ub);
    if (strcmp(text, ">>") == 0) *out = b < 0 || b > 63 ? 0 : (int64_t)(a < 0 ? ~(~ua >> ub) : ua >> ub);
    if (strcmp(text, "&") == 0) *out = (int64_t)(ua & ub);
    if (strcmp(text, "|") == 0) *out = (int64_t)(ua | ub);
    if (strcmp(text, "^") == 0) *out = (int64_t)(ua ^ ub);
    if (strcmp(text, "<") == 0) *out = a < b;
    if (strcmp(text, ">") == 0) *out = a > b;
    if (strcmp(text, "<=") == 0) *out = a <= b;
    if (strcmp(text, ">=") == 0) *out = a >= b;
    if (strcmp(text, "&&") == 0) *out = a != 0 && b != 0;
    if (strcmp(text, "||") == 0) *out = a != 0 || b != 0;
    if (strcmp(text, "==") == 0) *out = a == b;
    if (strcmp(text, "!=") == 0) *out = a != b;
    return true;
}

static bool smith_lang_cx_apply_binary(SmithLangCx *cx, uint8_t op) {
    if (cx->value_count < 2) return smith_lang_cx_fail(cx, "an operator is missing an operand");
    SmithLangCxValue right = cx->values[--cx->value_count];
    SmithLangCxValue left = cx->values[--cx->value_count];
    SmithLangCxValue result = {};
    const char *text = smith_lang_cx_binary[op].text;
    if (strcmp(text, "==") == 0 && left.is_type) {
        if (!smith_lang_cx_compare_types(cx, &left, &right, true, &result)) return false;
    } else if (strcmp(text, "!=") == 0 && left.is_type) {
        if (!smith_lang_cx_compare_types(cx, &left, &right, false, &result)) return false;
    } else {
        if (!smith_lang_cx_number(cx, &left) || !smith_lang_cx_number(cx, &right)) return false;
        if (!smith_lang_cx_arithmetic(cx, op, left.number, right.number, &result.number)) return false;
    }
    return smith_lang_cx_push_value(cx, result);
}

static bool smith_lang_cx_apply(SmithLangCx *cx, SmithLangCxOp op) {
    if (op.code >= smith_lang_cx_unary && op.code < smith_lang_cx_open) {
        return smith_lang_cx_apply_unary(cx, (char)(op.code - smith_lang_cx_unary));
    }
    return smith_lang_cx_apply_binary(cx, op.code);
}

// Applies the operators on the stack that bind at least as tightly as `precedence`.
static bool smith_lang_cx_reduce(SmithLangCx *cx, int8_t precedence) {
    while (cx->op_count > 0 && cx->ops[cx->op_count - 1].code != smith_lang_cx_open &&
           cx->ops[cx->op_count - 1].precedence >= precedence) {
        if (!smith_lang_cx_apply(cx, cx->ops[--cx->op_count])) return false;
    }
    return true;
}

static bool smith_lang_cx_push_op(SmithLangCx *cx, SmithLangCxOp op) {
    if (cx->op_count >= smith_lang_cx_stack_max) return smith_lang_cx_fail(cx, "this condition is too deep");
    cx->ops[cx->op_count++] = op;
    return true;
}

// The value of `sizeof(T)`, `alignof(T)` or `is_integer(T)` and the like, the call at `at` (the name): fills `value`
// and moves `at` past the `)`.
static bool smith_lang_cx_builtin(SmithLangCx *cx, uint32_t *at, uint32_t last, SmithLangCxValue *value) {
    const SmithLangGenToken *name = smith_lang_cx_token(*at);
    uint32_t open = *at + 1;
    if (open >= last || !smith_lang_gt_punct(smith_lang_cx_token(open), "(")) {
        return smith_lang_cx_fail(cx, "`%s` is written with a type in parentheses", smith_lang_gt_str(name));
    }
    uint32_t after = smith_lang_cx_skip_group(open, last);
    if (after == UINT32_MAX) return smith_lang_cx_fail(cx, "a parenthesis is never closed");
    SmithTypeQuery query;
    uint8_t canonical = 0;
    if (!smith_lang_cx_query(cx, open + 1, after - 1, &query, &canonical)) return false;
    const char *text = smith_lang_gt_str(name);
    bool size_question = strcmp(text, "sizeof") == 0 || strcmp(text, "alignof") == 0 || strcmp(text, "_Alignof") == 0;
    if (size_question && !query.complete) return smith_lang_cx_fail(cx, "`%s` has no size", cx->canonical[canonical]);
    if (strcmp(text, "sizeof") == 0) value->number = (int64_t)query.size;
    if (strcmp(text, "alignof") == 0 || strcmp(text, "_Alignof") == 0) value->number = (int64_t)query.alignment;
    if (strcmp(text, "is_integer") == 0) value->number = query.is_integer;
    if (strcmp(text, "is_float") == 0) value->number = query.is_float;
    if (strcmp(text, "is_signed") == 0) value->number = query.is_signed;
    if (strcmp(text, "is_pointer") == 0) value->number = query.is_pointer;
    if (strcmp(text, "is_bool") == 0) value->number = query.is_bool;
    if (strcmp(text, "is_enum") == 0) value->number = query.is_enum;
    if (strcmp(text, "is_record") == 0) value->number = query.is_record;
    if (strcmp(text, "is_array") == 0) value->number = query.is_array;
    *at = after;
    return true;
}

static bool smith_lang_cx_is_builtin(const SmithLangGenToken *token) {
    static const char *const names[] = {"sizeof",     "alignof", "_Alignof", "is_integer", "is_float", "is_signed",
                                        "is_pointer", "is_bool", "is_enum",  "is_record",  "is_array"};
    if (token->kind != SmithTokenKind_Identifier || token->special != SmithLangGen_Plain || token->param != 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) { // 11 names
        if (smith_lang_equals(smith_lang_gt_str(token), token->length, names[i])) return true;
    }
    return false;
}

// runtime/lang_target.c: the value of the fact whose macro the raw lowering wrote (`SMITH_TARGET_STATIC_ANALYZER`), as
// the compiler says it: 1 with the value, 0 when `name` is no such macro, -1 when it is one the host cannot answer
// (the raw lowering has no compiler).
static int smith_lang_target_macro_value(const SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                         int64_t *value);

// The operand at `at` when it is `countof(list)`, a list (an error) or the variable of a `for constexpr`: 1 and the
// value, -1 after an error, 0 when it is none of them.
static int smith_lang_cx_list_operand(SmithLangCx *cx, uint32_t *at, uint32_t last, SmithLangCxValue *value) {
    if (cx->replay == nullptr || cx->instance == nullptr) return 0;
    const SmithLangGenToken *token = smith_lang_cx_token(*at);
    SmithLangListRef list;
    if (smith_lang_fields_countof_at(*at, last)) { // `countof(fields(T))`: how many fields T has
        if (!smith_lang_fields_count(cx->host, cx->replay->generic, cx->instance, *at + 2, cx->location, &value->number,
                                     cx->message, sizeof cx->message)) {
            return -1;
        }
        *at += smith_lang_fields_countof_len(*at, last);
        return 1;
    }
    if (smith_lang_list_countof_at(cx->replay, *at, last, &list)) { // `countof(rest)`: how many elements it has
        value->number = (int64_t)list.count;
        *at += 4;
        return 1;
    }
    if (smith_lang_list_resolve(cx->replay, *at, &list)) {
        smith_lang_cx_fail(cx, "`%s` is a list: its size is `countof(%s)`", smith_lang_gt_str(token),
                           smith_lang_gt_str(token));
        return -1;
    }
    if (smith_lang_var_value(cx->replay, *at, &value->number)) { // the variable of a `for constexpr`
        *at += 1;
        return 1;
    }
    return 0;
}

// The operand at `at`: a number, a builtin question, or a type. Moves `at` past it.
static bool smith_lang_cx_operand(SmithLangCx *cx, uint32_t *at, uint32_t last, SmithLangCxValue *value) {
    const SmithLangGenToken *token = smith_lang_cx_token(*at);
    *value = (SmithLangCxValue){};
    if (token->kind == SmithTokenKind_Number) {
        char *end = nullptr;
        value->number = (int64_t)strtoull(smith_lang_gt_str(token), &end, 0);
        *at += 1;
        return true;
    }
    if (token->kind == SmithTokenKind_Character) {
        const char *text = smith_lang_gt_str(token);
        value->number = token->length >= 3 ? (unsigned char)(text[1] == '\\' ? (text[2] == 'n'   ? '\n'
                                                                                : text[2] == '0' ? 0
                                                                                                 : text[2])
                                                                             : text[1])
                                           : 0;
        *at += 1;
        return true;
    }
    if (smith_lang_cx_word(token, "true") || smith_lang_cx_word(token, "false")) {
        value->number = smith_lang_cx_word(token, "true") ? 1 : 0;
        *at += 1;
        return true;
    }
    int listed = smith_lang_cx_list_operand(cx, at, last, value);
    if (listed != 0) return listed > 0;
    int fact =
        token->kind == SmithTokenKind_Identifier && token->special == SmithLangGen_Plain && token->param == 0
            ? smith_lang_target_macro_value(cx->lang, cx->host, smith_lang_gt_str(token), token->length, &value->number)
            : 0;
    if (fact > 0) {
        *at += 1; // `target::name` of a generic read from its header: the header says the macro, the compiler the value
        return true;
    }
    if (fact < 0) {
        return smith_lang_cx_fail(cx,
                                  "a fact of the target in a generic's condition needs the compiler to answer it: a "
                                  "raw lowering (a bootstrapped module, the stage0) cannot choose the branch");
    }
    if (smith_lang_cx_is_builtin(token)) return smith_lang_cx_builtin(cx, at, last, value);
    if (smith_lang_cx_type_start(cx, *at)) {
        uint32_t end = smith_lang_cx_type_end(cx, *at, last);
        SmithTypeQuery query;
        value->is_type = true;
        if (!smith_lang_cx_query(cx, *at, end, &query, &value->type)) return false;
        *at = end;
        return true;
    }
    int64_t constant = 0;
    if (token->kind == SmithTokenKind_Identifier && token->special == SmithLangGen_Plain && token->param == 0 &&
        cx->host->constant != nullptr &&
        cx->host->constant(cx->host->context, smith_lang_gt_str(token), token->length, &constant)) {
        value->number = constant; // a `constexpr` integer the parser has read
        *at += 1;
        return true;
    }
    uint32_t end = token->kind == SmithTokenKind_Identifier && token->special == SmithLangGen_Plain
                       ? smith_lang_cx_primary_end(*at, last)
                       : *at;
    if (end > *at && cx->host->value_query != nullptr) return smith_lang_cx_ask(cx, at, end, value);
    cx->unknown_name = true;
    cx->unknown_at = *at;
    if (end > *at) {
        return smith_lang_cx_fail(cx,
                                  "`%s` is not a constant or a type; a call or a field of a `constexpr` value needs "
                                  "smith's compiler to evaluate it, and the raw lowering has none",
                                  smith_lang_gt_str(token));
    }
    return smith_lang_cx_fail(cx, "`%s` is not a constant or a type", smith_lang_gt_str(token));
}

static int smith_lang_cx_binary_find(const SmithLangGenToken *token) {
    if (token->kind != SmithTokenKind_Punctuator || token->special != SmithLangGen_Plain) return -1;
    for (size_t i = 0; i < sizeof smith_lang_cx_binary / sizeof smith_lang_cx_binary[0]; i++) { // 18 operators
        if (smith_lang_equals(smith_lang_gt_str(token), token->length, smith_lang_cx_binary[i].text)) return (int)i;
    }
    return -1;
}

static bool smith_lang_cx_unary_find(const SmithLangGenToken *token) {
    return token->kind == SmithTokenKind_Punctuator && token->length == 1 &&
           strchr("!-~", smith_lang_gt_str(token)[0]) != nullptr;
}

// One step of the evaluation at `*at`: an operand (with the unary operators and parentheses before it), or what follows
// an operand (a closing parenthesis or a binary operator).
static bool smith_lang_cx_step(SmithLangCx *cx, uint32_t *at, uint32_t last, bool *want_operand) {
    const SmithLangGenToken *token = smith_lang_cx_token(*at);
    if (*want_operand) {
        if (smith_lang_gt_punct(token, "(")) {
            *at += 1;
            return smith_lang_cx_push_op(cx, (SmithLangCxOp){smith_lang_cx_open, 0});
        }
        if (smith_lang_cx_unary_find(token)) {
            uint8_t code = (uint8_t)(smith_lang_cx_unary + (uint8_t)smith_lang_gt_str(token)[0]);
            *at += 1;
            return smith_lang_cx_push_op(cx, (SmithLangCxOp){code, smith_lang_cx_unary_precedence});
        }
        SmithLangCxValue value;
        if (!smith_lang_cx_operand(cx, at, last, &value) || !smith_lang_cx_push_value(cx, value)) return false;
        *want_operand = false;
        return true;
    }
    if (smith_lang_gt_punct(token, ")")) {
        if (!smith_lang_cx_reduce(cx, 0)) return false;
        if (cx->op_count == 0) return smith_lang_cx_fail(cx, "a `)` has no `(`");
        cx->op_count -= 1;
        *at += 1;
        return true;
    }
    int found = smith_lang_cx_binary_find(token);
    if (found < 0)
        return smith_lang_cx_fail(cx, "`%s` is not an operator of constant expressions", smith_lang_gt_str(token));
    int8_t precedence = smith_lang_cx_binary[found].precedence;
    if (!smith_lang_cx_reduce(cx, precedence) ||
        !smith_lang_cx_push_op(cx, (SmithLangCxOp){(uint8_t)found, precedence})) {
        return false;
    }
    *at += 1;
    *want_operand = true;
    return true;
}

// The tokens [first, last) as the compiler is asked about them (separated by \x1f): the type arguments of the instance
// for its T and the number of a `for constexpr` variable for its name.
static bool smith_lang_cx_text(const SmithLangCx *cx, uint32_t first, uint32_t last, char *text, size_t capacity) {
    size_t used = 0;
    for (uint32_t at = first; at < last; at++) { // the tokens
        char piece[256];
        int64_t number = 0;
        if (cx->replay != nullptr && smith_lang_var_value(cx->replay, at, &number)) {
            snprintf(piece, sizeof piece, "%lld", (long long)number);
        } else if (!smith_lang_infer_text((SmithLangSpan){at, at + 1}, cx->instance, cx->location, piece,
                                          sizeof piece)) {
            return false;
        }
        size_t length = strlen(piece);
        if (used + length + 2 >= capacity) return false;
        if (used > 0) text[used++] = '\x1f';
        memcpy(text + used, piece, length + 1);
        used += length;
    }
    return true;
}

// The end of a call, a field or an element chain that begins at `at` (`table_size(sizeof(T))`, `config.count`,
// `names[2].length`), or `at` when the name is followed by none of them.
static uint32_t smith_lang_cx_primary_end(uint32_t at, uint32_t last) {
    uint32_t end = at + 1;
    for (size_t guard = 0; end < last && guard < smith_lang_gt_max; guard++) { // bounded by the condition
        const SmithLangGenToken *token = smith_lang_cx_token(end);
        if (smith_lang_gt_punct(token, "(") || smith_lang_gt_punct(token, "[")) {
            uint32_t after = smith_lang_cx_skip_group(end, last);
            if (after == UINT32_MAX) return at;
            end = after;
        } else if ((smith_lang_gt_punct(token, ".") || smith_lang_gt_punct(token, "->")) && end + 1 < last &&
                   smith_lang_cx_token(end + 1)->kind == SmithTokenKind_Identifier) {
            end += 2;
        } else {
            break;
        }
    }
    return end == at + 1 ? at : end;
}

// The operand at `*at` is a call or a field the small evaluator does not know: the compiler evaluates it, in the scope
// the parser is in (`value_query`), and it must be an integer. False with cx->message otherwise.
static bool smith_lang_cx_ask(SmithLangCx *cx, uint32_t *at, uint32_t end, SmithLangCxValue *value) {
    static SmithValue nodes[smith_lang_cx_value_nodes];
    static SmithValueQuery query;
    char tokens[1024];
    if (!smith_lang_cx_text(cx, *at, end, tokens, sizeof tokens)) {
        return smith_lang_cx_fail(cx, "the expression is too long");
    }
    query = (SmithValueQuery){.nodes = nodes, .capacity = smith_lang_cx_value_nodes};
    if (!cx->host->value_query(cx->host->context, tokens, (uint32_t)strlen(tokens), &query)) {
        return smith_lang_cx_fail(cx, "%s", query.why != nullptr ? query.why : "the compiler cannot evaluate it");
    }
    const SmithValue *root = &query.root;
    if (root->kind != SmithValue_Int) return smith_lang_cx_fail(cx, "the value is not an integer");
    if (root->magnitude > (uint64_t)INT64_MAX) return smith_lang_cx_fail(cx, "the number is larger than 63 bits");
    value->number = root->negative ? -(int64_t)root->magnitude : (int64_t)root->magnitude;
    *at = end;
    return true;
}

// The value of the condition made of the tokens [first, last). False with cx->message when it is not constant.
static bool smith_lang_cx_eval(SmithLangCx *cx, uint32_t first, uint32_t last, int64_t *result) {
    bool want_operand = true;
    uint32_t at = first;
    for (size_t guard = 0; at < last && guard < smith_lang_gt_max; guard++) { // bounded by the condition
        if (!smith_lang_cx_step(cx, &at, last, &want_operand)) return false;
    }
    if (want_operand) return smith_lang_cx_fail(cx, "the condition ends where an operand is expected");
    if (!smith_lang_cx_reduce(cx, 0)) return false;
    if (cx->op_count != 0) return smith_lang_cx_fail(cx, "a `(` is never closed");
    if (cx->value_count != 1 || cx->values[0].is_type) return smith_lang_cx_fail(cx, "the condition is not a number");
    *result = cx->values[0].number;
    return true;
}

// ---- Choosing a branch
// -------------------------------------------------------------------------------------------------

// At an `if constexpr` of the replay: evaluates the condition for the instance, and the branch it chooses replaces the
// whole statement. The other branch is skipped, never written.
static void smith_lang_replay_if(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                 SmithLangFrame *frame) {
    uint32_t at = frame->pos;
    uint32_t location = smith_lang_gen_place(host, replay->generic, smith_lang_cx_token(at), replay->location);
    frame->pos = frame->end; // after an error nothing more of this body is written
    uint32_t open = at + 1;
    if (open >= frame->end || !smith_lang_gt_punct(smith_lang_cx_token(open), "(")) {
        smith_lang_error(lang, host, location, "`if constexpr` is followed by a condition in parentheses");
        return;
    }
    uint32_t then_start = smith_lang_cx_skip_group(open, frame->end);
    uint32_t then_end = then_start == UINT32_MAX ? UINT32_MAX : smith_lang_cx_statement_end(then_start, frame->end);
    if (then_end == UINT32_MAX) {
        smith_lang_error(lang, host, location, "this `if constexpr` has no statement that ends");
        return;
    }
    uint32_t whole_end = then_end;
    uint32_t else_start = 0;
    bool has_else = then_end < frame->end && smith_lang_cx_word(smith_lang_cx_token(then_end), "else");
    if (has_else) {
        else_start = then_end + 1;
        whole_end = smith_lang_cx_statement_end(else_start, frame->end);
        if (whole_end == UINT32_MAX) {
            smith_lang_error(lang, host, location, "this `else` has no statement that ends");
            return;
        }
    }
    static SmithLangCx evaluation;
    SmithLangCx *cx = &evaluation;
    *cx =
        (SmithLangCx){.lang = lang, .host = host, .instance = replay->instance, .replay = replay, .location = location};
    int64_t value = 0;
    frame->pos = whole_end; // the statement is replaced by one of its branches, or by nothing after an error
    if (!smith_lang_cx_eval(cx, open + 1, then_start - 1, &value)) {
        smith_lang_error(lang, host, location, "in `if constexpr`: %s", cx->message);
        return;
    }
    uint32_t from = value != 0 ? then_start : else_start;
    uint32_t to = value != 0 ? then_end : whole_end;
    if (from < to && smith_lang_gt_punct(smith_lang_cx_token(from), "{") && smith_lang_cx_skip_group(from, to) == to &&
        smith_lang_assigns_list(replay, from + 1, to - 1)) {
        from += 1; // a block that assigns a list is written without its braces: the new variables outlive it
        to -= 1;
    }
    if ((value != 0 || has_else) && from < to) smith_lang_replay_push(replay, from, to, 0);
}

// In a generic's body, `if` followed by `constexpr`: one token of its own in the record. False when it is not that.
static bool smith_lang_generic_if_constexpr(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                            SmithLangWord word) {
    if (!lang->recording) return false;
    const SmithToken *next = smith_lang_peek(lang, host, 0, false);
    if (!smith_lang_ident(next, "constexpr")) return false;
    smith_lang_take(lang, host);
    smith_lang_word_state(lang, token, word);
    smith_lang_gen_mark(lang, SmithLangGen_IfConstexpr, "if", 2, token->location);
    smith_lang_track(lang, token, false);
    return true;
}

// `for` followed by `constexpr` outside a generic's body: an error (the loop is run when an instance is written, and
// only a generic has instances). The `constexpr` is taken, and the loop goes on to the parser as a `for`.
static void smith_lang_for_constexpr_outside(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!smith_lang_ident(smith_lang_peek(lang, host, 0, false), "constexpr")) return;
    smith_lang_take(lang, host);
    smith_lang_error(lang, host, token->location,
                     "`for constexpr` is the loop of a generic function's body, run once for each instance; a plain "
                     "function's loop is `for`");
}

// `for` followed by `constexpr` in a generic's body: one token of its own in the record. False when it is not that.
static bool smith_lang_generic_for_constexpr(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                             SmithLangWord word) {
    const SmithToken *next = smith_lang_peek(lang, host, 0, false);
    if (!smith_lang_ident(next, "constexpr")) return false;
    smith_lang_take(lang, host);
    smith_lang_word_state(lang, token, word);
    smith_lang_gen_mark(lang, SmithLangGen_ForConstexpr, "for", 3, token->location);
    smith_lang_track(lang, token, false);
    return true;
}
