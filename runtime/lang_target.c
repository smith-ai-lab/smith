// The module `target`, and `if constexpr` outside generics (issue #46). Included by runtime/lang.c, after
// runtime/lang_generic.c, whose pool of kept tokens and constant-expression evaluator they use.
//
//   import target;
//   constexpr bool sanitized = target::address_sanitizer;
//   if constexpr (target::arch == target::aarch64) { ... } else { ... }
//
// `target::x` is a constant of the target the file is compiled for. A host with a preprocessor (the clang hook) answers
// each fact from it (`SmithHost.target_fact`) and the lowering writes the value as a literal, so it works in a
// `static_assert`, an array size or a `constexpr` initializer. The raw lowering (`smith cc --smith-lower`, which
// `smith build headers` uses for the headers) has no preprocessor and writes the preprocessor's own question into its
// output: a macro for the fact, defined at the top of the output by an `#if` chain (smith_lang_target_prelude), and for
// an `if constexpr` the branches between `#if`, `#else` and `#endif`. The preprocessor lives only in generated output.
//
// `if constexpr (condition) { then } else { else }` (the branches are blocks, and an `else if constexpr` continues the
// chain): the condition is a constant expression over `target::` facts, `constexpr` constants and literals. The branch
// not taken is read and dropped, never given to the parser, so it may not compile on this target. The branch taken is
// an ordinary block that flows through the lowering as it comes.

// ---- The facts -----------------------------------------------------------------------------------------------------

typedef struct {
    const char *name;
    int fact; // a SmithTarget, or -1 for a constant of this table
    int64_t constant;
    const char *macro; // what the raw lowering writes for the fact
} SmithLangTargetEntry;

static const SmithLangTargetEntry smith_lang_target_table[] = {
    {"arch", SmithTarget_Arch, 0, "SMITH_TARGET_ARCH"},
    {"aarch64", -1, 1, nullptr},
    {"x86_64", -1, 2, nullptr},
    {"os", SmithTarget_Os, 0, "SMITH_TARGET_OS"},
    {"macos", -1, 1, nullptr},
    {"linux", -1, 2, nullptr},
    {"address_sanitizer", SmithTarget_AddressSanitizer, 0, "SMITH_TARGET_ADDRESS_SANITIZER"},
    {"undefined_sanitizer", SmithTarget_UndefinedSanitizer, 0, "SMITH_TARGET_UNDEFINED_SANITIZER"},
    {"thread_sanitizer", SmithTarget_ThreadSanitizer, 0, "SMITH_TARGET_THREAD_SANITIZER"},
    {"assertions", SmithTarget_Assertions, 0, "SMITH_TARGET_ASSERTIONS"},
    {"float_radix", SmithTarget_FloatRadix, 0, "SMITH_TARGET_FLOAT_RADIX"},
    {"f32_mantissa_bits", SmithTarget_F32MantissaBits, 0, "SMITH_TARGET_F32_MANTISSA_BITS"},
    {"f64_mantissa_bits", SmithTarget_F64MantissaBits, 0, "SMITH_TARGET_F64_MANTISSA_BITS"},
    {"float_eval_method", SmithTarget_FloatEvalMethod, 0, "SMITH_TARGET_FLOAT_EVAL_METHOD"},
    {"static_analyzer", SmithTarget_StaticAnalyzer, 0, "SMITH_TARGET_STATIC_ANALYZER"},
};

// The facts that are true or false, written `true` and `false` in compiled code.
static bool smith_lang_target_boolean(int fact) {
    return fact == SmithTarget_AddressSanitizer || fact == SmithTarget_UndefinedSanitizer ||
           fact == SmithTarget_ThreadSanitizer || fact == SmithTarget_Assertions || fact == SmithTarget_StaticAnalyzer;
}

// What the raw lowering defines for each fact at the top of its output: the preprocessor's own question.
static const char *const smith_lang_target_prelude_text[] = {
    [SmithTarget_Arch] = ("#ifndef SMITH_TARGET_ARCH\n"
                          "#if defined(__aarch64__)\n#define SMITH_TARGET_ARCH 1\n"
                          "#elif defined(__x86_64__)\n#define SMITH_TARGET_ARCH 2\n"
                          "#else\n#define SMITH_TARGET_ARCH 0\n#endif\n#endif\n"),
    [SmithTarget_Os] = ("#ifndef SMITH_TARGET_OS\n"
                        "#if defined(__APPLE__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)\n"
                        "#define SMITH_TARGET_OS 1\n"
                        "#elif defined(__linux__)\n#define SMITH_TARGET_OS 2\n"
                        "#else\n#define SMITH_TARGET_OS 0\n#endif\n#endif\n"),
    [SmithTarget_AddressSanitizer] = ("#ifndef SMITH_TARGET_ADDRESS_SANITIZER\n"
                                      "#if __has_feature(address_sanitizer)\n#define SMITH_TARGET_ADDRESS_SANITIZER 1\n"
                                      "#else\n#define SMITH_TARGET_ADDRESS_SANITIZER 0\n#endif\n#endif\n"),
    [SmithTarget_UndefinedSanitizer] = ("#ifndef SMITH_TARGET_UNDEFINED_SANITIZER\n"
                                        "#if __has_feature(undefined_behavior_sanitizer)\n"
                                        "#define SMITH_TARGET_UNDEFINED_SANITIZER 1\n"
                                        "#else\n#define SMITH_TARGET_UNDEFINED_SANITIZER 0\n#endif\n#endif\n"),
    [SmithTarget_ThreadSanitizer] = ("#ifndef SMITH_TARGET_THREAD_SANITIZER\n"
                                     "#if __has_feature(thread_sanitizer)\n#define SMITH_TARGET_THREAD_SANITIZER 1\n"
                                     "#else\n#define SMITH_TARGET_THREAD_SANITIZER 0\n#endif\n#endif\n"),
    [SmithTarget_Assertions] = ("#ifndef SMITH_TARGET_ASSERTIONS\n"
                                "#ifdef NDEBUG\n#define SMITH_TARGET_ASSERTIONS 0\n"
                                "#else\n#define SMITH_TARGET_ASSERTIONS 1\n#endif\n#endif\n"),
    [SmithTarget_FloatRadix] =
        ("#ifndef SMITH_TARGET_FLOAT_RADIX\n#define SMITH_TARGET_FLOAT_RADIX __FLT_RADIX__\n#endif\n"),
    [SmithTarget_F32MantissaBits] =
        ("#ifndef SMITH_TARGET_F32_MANTISSA_BITS\n#define SMITH_TARGET_F32_MANTISSA_BITS __FLT_MANT_DIG__\n#endif\n"),
    [SmithTarget_F64MantissaBits] =
        ("#ifndef SMITH_TARGET_F64_MANTISSA_BITS\n#define SMITH_TARGET_F64_MANTISSA_BITS __DBL_MANT_DIG__\n#endif\n"),
    [SmithTarget_FloatEvalMethod] = ("#ifndef SMITH_TARGET_FLOAT_EVAL_METHOD\n"
                                     "#define SMITH_TARGET_FLOAT_EVAL_METHOD __FLT_EVAL_METHOD__\n#endif\n"),
    [SmithTarget_StaticAnalyzer] = ("#ifndef SMITH_TARGET_STATIC_ANALYZER\n"
                                    "#ifdef __clang_analyzer__\n#define SMITH_TARGET_STATIC_ANALYZER 1\n"
                                    "#else\n#define SMITH_TARGET_STATIC_ANALYZER 0\n#endif\n#endif\n"),
};

// The definitions a header needs, for the facts the `if constexpr` of its inline functions use (the header is
// written without the compiler, so the includer's preprocessor answers).
static size_t smith_lang_target_header_prelude(const SmithLang *lang, char *out, size_t capacity, size_t used) {
    for (size_t i = 0; i < sizeof smith_lang_target_prelude_text / sizeof smith_lang_target_prelude_text[0]; i++) {
        if ((lang->header_target_used & (1U << i)) != 0)
            used = smith_lang_append(out, capacity, used, "%s", smith_lang_target_prelude_text[i]);
    }
    return used;
}

// The definitions the raw lowering's output needs, for the facts it used.
static void smith_lang_target_prelude(const SmithLang *lang, FILE *out) {
    for (size_t i = 0; i < sizeof smith_lang_target_prelude_text / sizeof smith_lang_target_prelude_text[0]; i++) {
        if ((lang->target_used & (1U << i)) != 0) fputs(smith_lang_target_prelude_text[i], out);
    }
}

static const SmithLangTargetEntry *smith_lang_target_find(const char *name, size_t length) {
    for (size_t i = 0; i < sizeof smith_lang_target_table / sizeof smith_lang_target_table[0]; i++) { // 14 facts
        if (smith_lang_equals(name, length, smith_lang_target_table[i].name)) return &smith_lang_target_table[i];
    }
    return nullptr;
}

static void smith_lang_target_unknown(SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                      uint32_t location) {
    smith_lang_error(lang, host, location,
                     "`target::%.*s` is not a fact of the target; they are arch (aarch64, x86_64), os (macos, linux), "
                     "address_sanitizer, undefined_sanitizer, thread_sanitizer, assertions, float_radix, "
                     "f32_mantissa_bits, f64_mantissa_bits, float_eval_method and static_analyzer",
                     (int)length, name);
}

// The value of a fact: from the table, or from the host's preprocessor. False when the host has none (raw lowering) or
// cannot answer.
static bool smith_lang_target_value(const SmithLang *lang, const SmithHost *host, const SmithLangTargetEntry *entry,
                                    int64_t *value) {
    (void)lang;
    if (entry->fact < 0) {
        *value = entry->constant;
        return true;
    }
    return host->target_fact != nullptr && host->target_fact(host->context, (SmithTarget)entry->fact, value);
}

static int smith_lang_target_macro_value(const SmithLang *lang, const SmithHost *host, const char *name, size_t length,
                                         int64_t *value) {
    for (size_t i = 0; i < sizeof smith_lang_target_table / sizeof smith_lang_target_table[0]; i++) { // 15 facts
        const SmithLangTargetEntry *entry = &smith_lang_target_table[i];
        if (entry->macro != nullptr && smith_lang_equals(name, length, entry->macro)) {
            return smith_lang_target_value(lang, host, entry, value) ? 1 : -1;
        }
    }
    return 0;
}

// ---- `target::x` in code
// ---------------------------------------------------------------------------------------------

static SmithToken smith_lang_target_token(SmithTokenKind kind, const char *text, size_t length, const SmithToken *like,
                                          uint32_t location, bool first) {
    SmithToken made = smith_lang_synth(kind, text, length, like);
    made.location = location;
    if (!first) made.line_start = made.space_before = false;
    return made;
}

// The token a fact becomes: a number, or `true` or `false`, or (the raw lowering) the macro its output defines. It
// stands where `target::name` stood, as one token, so a declaration's head holds it like any name.
static SmithToken smith_lang_target_value_token(const SmithToken *like, uint32_t location, int64_t value, bool boolean,
                                                char *digits, size_t capacity) {
    if (boolean) {
        const char *word = value != 0 ? "true" : "false";
        return smith_lang_target_token(SmithTokenKind_Identifier, word, strlen(word), like, location, true);
    }
    int length = snprintf(digits, capacity, "%llu", (unsigned long long)value);
    SMITH_ASSERT(length > 0);
    return smith_lang_target_token(SmithTokenKind_Number, digits, (size_t)length, like, location, true);
}

// `target::member`, read by smith_lang_namespace: the token that stands where the access stood. `digits` holds its
// text, and lives as long as the caller's use of it.
static void smith_lang_target_use(SmithLang *lang, const SmithHost *host, const SmithToken *name,
                                  const SmithToken *member, SmithToken *result, char *digits, size_t capacity) {
    const SmithLangTargetEntry *entry = smith_lang_target_find(member->text, member->length);
    *result = smith_lang_target_value_token(name, member->location, 0, false, digits, capacity);
    if (entry == nullptr) {
        smith_lang_target_unknown(lang, host, member->text, member->length, member->location);
        return;
    }
    if (lang->export_active) {
        smith_lang_error(lang, host, name->location,
                         "`target::%.*s` cannot be used in an exported declaration: its header is written without the "
                         "compiler, for every target",
                         (int)member->length, member->text);
    }
    int64_t value = 0;
    if (smith_lang_target_value(lang, host, entry, &value)) {
        if (value < 0) {
            smith_lang_error(lang, host, member->location, "`target::%.*s` is %lld here: it has no value to use",
                             (int)member->length, member->text, (long long)value);
            return;
        }
        *result =
            smith_lang_target_value_token(name, member->location, value,
                                          entry->fact >= 0 && smith_lang_target_boolean(entry->fact), digits, capacity);
    } else if (host->target_fact == nullptr) { // the raw lowering: the macro its output defines
        lang->target_used |= 1U << (unsigned)entry->fact;
        *result = smith_lang_target_token(SmithTokenKind_Identifier, entry->macro, strlen(entry->macro), name,
                                          member->location, true);
    } else {
        smith_lang_error(lang, host, member->location, "the compiler cannot say `target::%.*s`", (int)member->length,
                         member->text);
    }
}

// ---- Conditions
// ------------------------------------------------------------------------------------------------------

typedef struct {
    uint32_t location;
    char message[smith_lang_message_max];
} SmithLangIfcError;

constexpr uint8_t smith_lang_ifc_pseudo = 255; // a staged token that stands for text of the raw lowering's `#if`

static bool smith_lang_ifc_stage(const SmithToken *token) {
    return smith_lang_pool_add(&smith_lang_gt, smith_lang_gt_from(token), token->text, token->length) != UINT32_MAX;
}

static bool smith_lang_ifc_add(const SmithLangGenToken *like, SmithTokenKind kind, const char *text, size_t length,
                               uint8_t param) {
    SmithLangGenToken made = *like;
    made.kind = (uint8_t)kind;
    made.special = SmithLangGen_Plain;
    made.param = param;
    return smith_lang_pool_add(&smith_lang_gt, made, text, length) != UINT32_MAX;
}

static bool smith_lang_ifc_add_number(const SmithLangGenToken *like, int64_t value) {
    char digits[32];
    int length = snprintf(digits, sizeof digits, "%llu",
                          (unsigned long long)(value < 0 ? 0 - (uint64_t)value : (uint64_t)value));
    if (value < 0 && !smith_lang_ifc_add(like, SmithTokenKind_Punctuator, "-", 1, 0)) return false;
    return smith_lang_ifc_add(like, SmithTokenKind_Number, digits, (size_t)length, 0);
}

static const SmithLangConstant *smith_lang_ifc_constant_find(const SmithLang *lang, const char *name, size_t length) {
    for (size_t i = lang->constants_count; i > 0; i--) { // the latest declaration of the name
        const SmithLangConstant *constant = &lang->constants[i - 1];
        if (smith_lang_equals(name, length, smith_lang_str(constant->name))) return constant;
    }
    return nullptr;
}

[[gnu::format(printf, 3, 4)]] static bool smith_lang_ifc_fail(SmithLangIfcError *error, uint32_t location,
                                                              const char *format, ...) {
    error->location = location;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error->message, sizeof error->message, format, arguments);
    va_end(arguments);
    return false;
}

// One staged token of a condition, written to the pool again: a `target::` fact, a constant or a word, as the
// evaluation (or the `#if` text) wants it. `*at` moves past what it used.
static bool smith_lang_ifc_substitute_one(SmithLang *lang, const SmithHost *host, uint32_t *at, uint32_t last, bool raw,
                                          SmithLangIfcError *error) {
    const SmithLangGenToken token = smith_lang_gt.tokens[*at];
    const char *text = smith_lang_gt_str(&token);
    if (token.kind == SmithTokenKind_Identifier && lang->bindings_count > 0) {
        const SmithLangBinding *binding = smith_lang_binding_find(lang, text, token.length);
        if (binding != nullptr && binding->is_target) {
            bool member = *at + 2 < last && smith_lang_gt_punct(&smith_lang_gt.tokens[*at + 1], "::") &&
                          smith_lang_gt.tokens[*at + 2].kind == SmithTokenKind_Identifier;
            if (!member)
                return smith_lang_ifc_fail(error, token.location,
                                           "`%s` is the module of the target: write `%s::` and a fact", text, text);
            const SmithLangGenToken *name = &smith_lang_gt.tokens[*at + 2];
            const SmithLangTargetEntry *entry = smith_lang_target_find(smith_lang_gt_str(name), name->length);
            if (entry == nullptr) {
                return smith_lang_ifc_fail(error, name->location, "`target::%s` is not a fact of the target",
                                           smith_lang_gt_str(name));
            }
            *at += 2;
            int64_t value = 0;
            if (smith_lang_target_value(lang, host, entry, &value)) return smith_lang_ifc_add_number(&token, value);
            if (!raw)
                return smith_lang_ifc_fail(error, name->location, "the compiler cannot say `target::%s`", entry->name);
            lang->target_used |= 1U << (unsigned)entry->fact;
            if (lang->export_active) lang->header_target_used |= 1U << (unsigned)entry->fact;
            return smith_lang_ifc_add(&token, SmithTokenKind_Identifier, entry->macro, strlen(entry->macro),
                                      smith_lang_ifc_pseudo);
        }
    }
    bool after_member = *at > 0 && (smith_lang_gt_punct(&smith_lang_gt.tokens[*at - 1], ".") ||
                                    smith_lang_gt_punct(&smith_lang_gt.tokens[*at - 1], "->") ||
                                    smith_lang_gt_punct(&smith_lang_gt.tokens[*at - 1], "::"));
    if (token.kind == SmithTokenKind_Identifier && raw &&
        (smith_lang_equals(text, token.length, "true") || smith_lang_equals(text, token.length, "false"))) {
        bool truth = smith_lang_equals(text, token.length, "true");
        return smith_lang_ifc_add(&token, SmithTokenKind_Number, truth ? "1" : "0", 1, smith_lang_ifc_pseudo);
    }
    if (token.kind == SmithTokenKind_Identifier && !after_member) {
        const SmithLangConstant *constant = smith_lang_ifc_constant_find(lang, text, token.length);
        if (constant != nullptr && raw && constant->text.length > 0) {
            return smith_lang_ifc_add(&token, SmithTokenKind_Identifier, smith_lang_str(constant->text),
                                      constant->text.length, smith_lang_ifc_pseudo);
        }
        int64_t value = 0;
        if (!raw && host->constant != nullptr) { // the parser knows it by the name the compiler sees
            const char *seen = text;
            size_t seen_length = token.length;
            if (lang->is_module && smith_lang_name_find(lang, text, token.length) != nullptr) {
                seen = smith_lang_prefixed(lang, text, token.length);
                seen_length = lang->tmp_length;
            }
            if (host->constant(host->context, seen, (uint32_t)seen_length, &value)) {
                return smith_lang_ifc_add_number(&token, value);
            }
        }
    }
    return smith_lang_pool_add(&smith_lang_gt, token, text, token.length) != UINT32_MAX;
}

// The raw lowering's `#if` expression: the substituted tokens [first, last) joined by spaces. A name that is not a
// fact or a constant is an error.
static bool smith_lang_ifc_text(uint32_t first, uint32_t last, char *text, size_t capacity, SmithLangIfcError *error) {
    size_t used = 0;
    text[0] = 0;
    for (uint32_t i = first; i < last; i++) { // the tokens of the condition
        const SmithLangGenToken *token = &smith_lang_gt.tokens[i];
        if (token->param != smith_lang_ifc_pseudo &&
            (token->kind == SmithTokenKind_Identifier || token->kind == SmithTokenKind_String ||
             token->kind == SmithTokenKind_Character)) {
            const SmithLangGenToken *after = i + 1 < last ? token + 1 : nullptr;
            if (token->kind == SmithTokenKind_Identifier && after != nullptr &&
                (smith_lang_gt_punct(after, "(") || smith_lang_gt_punct(after, ".") ||
                 smith_lang_gt_punct(after, "[") || smith_lang_gt_punct(after, "->"))) {
                return smith_lang_ifc_fail(error, token->location,
                                           "`%s` is not a constant: a call or a field of a `constexpr` value in an "
                                           "`if constexpr` needs smith's compiler to evaluate it, and the raw "
                                           "lowering has none",
                                           smith_lang_gt_str(token));
            }
            return smith_lang_ifc_fail(
                error, token->location,
                "`%s` is not a constant: an `if constexpr` condition is made of `target::` facts, "
                "`constexpr` constants and literals",
                smith_lang_gt_str(token));
        }
        size_t length = token->length;
        if (used + length + 2 >= capacity)
            return smith_lang_ifc_fail(error, token->location, "this condition is too long");
        if (used > 0) text[used++] = ' ';
        memcpy(text + used, smith_lang_gt_str(token), length);
        used += length;
        text[used] = 0;
    }
    return true;
}

// The value of the staged condition [first, last).
static bool smith_lang_ifc_evaluate(SmithLang *lang, const SmithHost *host, uint32_t first, uint32_t last,
                                    uint32_t location, int64_t *value, SmithLangIfcError *error) {
    static SmithLangCx evaluation;
    SmithLangCx *cx = &evaluation;
    *cx = (SmithLangCx){.lang = lang, .host = host, .instance = nullptr, .location = location};
    if (smith_lang_cx_eval(cx, first, last, value)) return true;
    if (cx->unknown_name) {
        const SmithLangGenToken *token = &smith_lang_gt.tokens[cx->unknown_at];
        return smith_lang_ifc_fail(error, token->location,
                                   "`%s` is not a constant: an `if constexpr` condition is made of `target::` facts, "
                                   "`constexpr` constants and literals",
                                   smith_lang_gt_str(token));
    }
    return smith_lang_ifc_fail(error, location, "%s", cx->message);
}

// A condition staged at [first, last) of the pool: its value (compile), or the text of its `#if` (raw). The pool is
// as it was, above `first`, afterwards.
static bool smith_lang_ifc_resolve(SmithLang *lang, const SmithHost *host, uint32_t first, uint32_t location,
                                   int64_t *value, char *text, size_t capacity, SmithLangIfcError *error) {
    uint32_t last = smith_lang_gt.count;
    bool raw = host->target_fact == nullptr;
    for (uint32_t at = first; at < last; at++) { // each staged token, substituted after the staged ones
        if (!smith_lang_ifc_substitute_one(lang, host, &at, last, raw, error)) return false;
    }
    if (smith_lang_gt.count == last) return smith_lang_ifc_fail(error, location, "the condition is empty");
    if (raw) return smith_lang_ifc_text(last, smith_lang_gt.count, text, capacity, error);
    return smith_lang_ifc_evaluate(lang, host, last, smith_lang_gt.count, location, value, error);
}

// Takes the tokens up to the `)` that closes a group whose `(` was taken, and stages them. A name after `::` is read
// as written (`linux` may be a macro).
static bool smith_lang_ifc_read_group(SmithLang *lang, const SmithHost *host) {
    int depth = 1;
    bool after_colons = false;
    for (size_t i = 0; i < smith_lang_condition_max; i++) { // bounded by the condition
        if (smith_lang_peek(lang, host, 0, after_colons)->kind == SmithTokenKind_Eof) return false;
        SmithToken token = smith_lang_take(lang, host);
        if (smith_lang_punct(&token, "(")) depth += 1;
        if (smith_lang_punct(&token, ")") && --depth == 0) return true;
        if (!smith_lang_ifc_stage(&token)) return false;
        after_colons = smith_lang_punct(&token, "::");
    }
    return false;
}

// ---- Blocks
// --------------------------------------------------------------------------------------------------------------

// Takes the group (or block) that begins at the next token, whose opening bracket is `open`, up to its closing one. The
// end of the file is left for the caller. False when the file ends first.
static bool smith_lang_ifc_skip(SmithLang *lang, const SmithHost *host, const char *open, const char *close) {
    int depth = 0;
    for (size_t i = 0; i < smith_lang_gt_max; i++) { // bounded by the pool
        if (smith_lang_peek(lang, host, 0, false)->kind == SmithTokenKind_Eof) return false;
        SmithToken token = smith_lang_take(lang, host);
        if (smith_lang_punct(&token, open)) depth += 1;
        if (smith_lang_punct(&token, close) && --depth == 0) return true;
    }
    return false;
}

static void smith_lang_ifc_push(SmithLang *lang, const SmithHost *host, uint32_t location, bool raw) {
    if (lang->ifc_count >= smith_lang_if_depth_max) {
        smith_lang_error(lang, host, location, "`if constexpr` blocks nest at most %zu deep", smith_lang_if_depth_max);
        return;
    }
    if (lang->ifc_count == 0) lang->ifc_braces = 0;
    lang->ifc[lang->ifc_count++] = (SmithLangIfFrame){.target = lang->ifc_braces, .raw = raw};
}

static void smith_lang_ifc_pop(SmithLang *lang) {
    lang->ifc_count -= 1;
    if (lang->ifc_count == 0) lang->ifc_braces = 0;
}

static bool smith_lang_ifc_is_chain(SmithLang *lang, const SmithHost *host) { // `if constexpr` next
    return smith_lang_ident(smith_lang_peek(lang, host, 0, false), "if") &&
           smith_lang_ident(smith_lang_peek(lang, host, 1, false), "constexpr");
}

// After a then-block that was taken: the `else` that follows, a block or a chain of `else if constexpr`, is dropped.
static void smith_lang_ifc_discard_else(SmithLang *lang, const SmithHost *host) {
    for (size_t guard = 0; guard < smith_lang_if_depth_max * 4; guard++) { // the chain's links
        if (!smith_lang_ident(smith_lang_peek(lang, host, 0, false), "else")) return;
        smith_lang_take(lang, host);
        if (smith_lang_punct(smith_lang_peek(lang, host, 0, false), "{")) {
            smith_lang_ifc_skip(lang, host, "{", "}");
            return;
        }
        if (!smith_lang_ifc_is_chain(lang, host)) {
            smith_lang_error(lang, host, smith_lang_peek(lang, host, 0, false)->location,
                             "after `else` of an `if constexpr` write a block or another `if constexpr`");
            return;
        }
        smith_lang_take(lang, host);
        smith_lang_take(lang, host);
        if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(") ||
            !smith_lang_ifc_skip(lang, host, "(", ")") ||
            !smith_lang_punct(smith_lang_peek(lang, host, 0, false), "{") ||
            !smith_lang_ifc_skip(lang, host, "{", "}")) {
            return;
        }
    }
}

static void smith_lang_dump_line(SmithLang *lang, const char *text) {
    size_t length = strlen(text);
    if (lang->is_module && lang->export_active) { // an exported inline function's body is in the header too
        smith_lang_header_line(lang, text, length, 0);
        if (lang->aligned && smith_lang_header_append(lang, "\n", 1)) { // the interface read in memory: the next token
            lang->aligned_line += 1;                                    // is on a line after the directive
            lang->aligned_column = 1;
        }
    }
    if (!lang->dump_enabled || lang->dump_used + length + 2 > smith_lang_dump_max) return;
    if (lang->dump_used > 0 && smith_lang_dump[lang->dump_used - 1] != '\n') smith_lang_dump[lang->dump_used++] = '\n';
    smith_copy_bytes(smith_lang_dump + lang->dump_used, text, length);
    lang->dump_used += length;
    smith_lang_dump[lang->dump_used++] = '\n';
}

// The directive line `#if text` (or `#elif`), from a condition just staged.
static void smith_lang_ifc_directive(SmithLang *lang, const char *directive, const char *text) {
    char line[smith_lang_message_max + 16];
    snprintf(line, sizeof line, "%s %s", directive, text);
    smith_lang_dump_line(lang, line);
}

// `if constexpr` begins here (`if` is the token, `constexpr` is next). Returns false when the next token is no
// `constexpr`: an ordinary `if`.
static bool smith_lang_ifc_begin(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!smith_lang_ident(smith_lang_peek(lang, host, 0, false), "constexpr")) return false;
    smith_lang_take(lang, host);
    if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(")) {
        smith_lang_error(lang, host, token->location, "`if constexpr` is followed by a condition in parentheses");
        return true;
    }
    smith_lang_take(lang, host);
    uint32_t mark[2] = {smith_lang_gt.count, smith_lang_gt.text_used};
    uint32_t condition_location = smith_lang_peek(lang, host, 0, false)->location;
    bool raw = host->target_fact == nullptr;
    int64_t taken = 0;
    char text[smith_lang_message_max];
    SmithLangIfcError error = {};
    bool good = smith_lang_ifc_read_group(lang, host);
    if (!good) {
        smith_lang_error(lang, host, token->location, "this `if constexpr` has a condition that never closes");
    } else {
        good = smith_lang_ifc_resolve(lang, host, mark[0], condition_location, &taken, text, sizeof text, &error);
        if (!good) smith_lang_error(lang, host, error.location, "in `if constexpr`: %s", error.message);
    }
    smith_lang_gt.count = mark[0];
    smith_lang_gt.text_used = mark[1];
    if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), "{")) {
        smith_lang_error(lang, host, token->location,
                         "the branches of `if constexpr` are blocks: write `{` after the condition");
        return true;
    }
    if (good && raw) {
        smith_lang_ifc_directive(lang, "#if", text);
        smith_lang_ifc_push(lang, host, token->location, true);
    } else if (good && taken != 0) {
        smith_lang_ifc_push(lang, host, token->location, false);
    } else { // not taken: the block is dropped, and an `else` after it begins
        smith_lang_ifc_skip(lang, host, "{", "}");
        if (smith_lang_ident(smith_lang_peek(lang, host, 0, false), "else")) smith_lang_take(lang, host);
    }
    return true;
}

// The `}` that ended the then-block (or the else-block) of the innermost `if constexpr` was written.
static void smith_lang_ifc_close(SmithLang *lang, const SmithHost *host) {
    SmithLangIfFrame *frame = &lang->ifc[lang->ifc_count - 1];
    if (!frame->raw) {
        smith_lang_ifc_discard_else(lang, host);
        smith_lang_ifc_pop(lang);
        return;
    }
    if (!frame->in_else && smith_lang_ident(smith_lang_peek(lang, host, 0, false), "else")) {
        smith_lang_take(lang, host);
        if (!smith_lang_ifc_is_chain(lang, host)) {
            smith_lang_dump_line(lang, "#else");
            frame->in_else = true;
            return;
        }
        smith_lang_take(lang, host);
        smith_lang_take(lang, host);
        uint32_t mark[2] = {smith_lang_gt.count, smith_lang_gt.text_used};
        char text[smith_lang_message_max];
        SmithLangIfcError error = {};
        int64_t taken = 0;
        uint32_t location = smith_lang_peek(lang, host, 0, false)->location;
        bool good = smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(");
        if (good) smith_lang_take(lang, host);
        good = good && smith_lang_ifc_read_group(lang, host) &&
               smith_lang_ifc_resolve(lang, host, mark[0], location, &taken, text, sizeof text, &error);
        if (good) {
            smith_lang_ifc_directive(lang, "#elif", text);
        } else {
            smith_lang_error(lang, host, error.location != 0 ? error.location : location, "in `if constexpr`: %s",
                             error.message[0] != 0 ? error.message : "the condition is not in parentheses");
            smith_lang_dump_line(lang, "#else");
            frame->in_else = true;
        }
        smith_lang_gt.count = mark[0];
        smith_lang_gt.text_used = mark[1];
        return;
    }
    smith_lang_dump_line(lang, "#endif");
    smith_lang_ifc_pop(lang);
}

// ---- Constants a condition names
// -------------------------------------------------------------------------------------

// The raw lowering has no parser to ask for a constant (a compile does, `SmithHost.constant`), so at a `constexpr`
// (already taken) `constexpr <type> name = initializer;` is read ahead, without taking anything, and the name is
// recorded with the `#if` text of its initializer. An initializer that is no constant of the kind a condition has is
// not recorded: naming it in a condition is the error.
static void smith_lang_ifc_constant(SmithLang *lang, const SmithHost *host) {
    if (host->target_fact != nullptr) return;
    size_t equals = SIZE_MAX;
    size_t end = SIZE_MAX;
    int depth = 0;
    bool after_colons = false;
    for (size_t i = 0; i < smith_lang_condition_max && end == SIZE_MAX; i++) { // bounded by the declaration
        const SmithToken *token = smith_lang_peek(lang, host, i, after_colons);
        after_colons = smith_lang_punct(token, "::");
        if (token->kind == SmithTokenKind_Eof || smith_lang_punct(token, "{")) return;
        bool open = smith_lang_punct(token, "(") || smith_lang_punct(token, "[");
        bool close = smith_lang_punct(token, ")") || smith_lang_punct(token, "]");
        if (equals == SIZE_MAX && (open || close || smith_lang_punct(token, ","))) return; // a function, an array...
        if (open) depth += 1;
        if (close && --depth < 0) return;
        if (depth == 0 && smith_lang_punct(token, ";")) end = i;
        if (depth == 0 && smith_lang_punct(token, ",")) return; // several declarators
        if (depth == 0 && equals == SIZE_MAX && smith_lang_punct(token, "=")) equals = i;
    }
    if (end == SIZE_MAX || equals == SIZE_MAX || equals < 2) return;
    const SmithToken *name = smith_lang_peek(lang, host, equals - 1, false);
    if (name->kind != SmithTokenKind_Identifier || smith_lang_is_word(name) ||
        lang->constants_count >= smith_lang_constants_max) {
        return;
    }
    SmithLangConstant constant = {.name = smith_lang_store(lang, name->text, name->length)};
    uint32_t mark[2] = {smith_lang_gt.count, smith_lang_gt.text_used};
    bool staged = true;
    for (size_t i = equals + 1; i < end && staged; i++)
        staged = smith_lang_ifc_stage(smith_lang_peek(lang, host, i, false));
    int64_t value = 0;
    char text[smith_lang_message_max];
    SmithLangIfcError error = {};
    if (staged && smith_lang_ifc_resolve(lang, host, mark[0], name->location, &value, text, sizeof text, &error)) {
        char wrapped[smith_lang_message_max + 4];
        snprintf(wrapped, sizeof wrapped, "(%s)", text);
        constant.text = smith_lang_store(lang, wrapped, strlen(wrapped));
    }
    smith_lang_gt.count = mark[0];
    smith_lang_gt.text_used = mark[1];
    lang->constants[lang->constants_count++] = constant;
}
