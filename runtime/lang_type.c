// Types are values (issue #99). `type` is a compile-time type: `constexpr type t = i32;` is a value, a struct may hold
// one (`typedef struct { constexpr type t; } Foo;`), an array may, and an ordinary function may take and return one.
// Included by runtime/lang.c, after runtime/lang_tuple.c.
//
// A type is a handle, a 64-bit number the engine (clang's constant evaluator) sees as plain data: in the text given to
// clang a `type` is an `unsigned long long` and a type written where a value is expected (`i32`, `struct S *`) is its
// handle as an integer literal. The handle is made of an index into this file's table of the types it has met (two
// handles are equal exactly when the compiler's canonical spellings are: `u32 == uint32_t`), the questions a type is
// asked (integer, float, signed, pointer, bool, enum, record, array), its alignment and its size, so `sizeof(t)` is a
// shift of the handle and needs no callback into the lowering:
//
//   bits 0-15 the index   16-23 the kinds   24-28 log2 of the alignment   29 the type is incomplete   32-63 the size
//
// The lowering turns a handle back into the type it names, wherever a type is written (`t x;`, `(t)y`, the argument
// of a generic call), by asking the compiler for the handle's value and reading its index.
//
// The tokens are rewritten as they are lowered, by `smith_lang_ty_token`, called for each token of a file the host
// offered whole (the host sets `maybe_type_value` when the code has a `type` specifier). It follows a little of the
// file's shape (parentheses, braces, a declaration's specifiers) and knows four places a type is a value, the
// "regions": the initializer of a declaration whose type holds a type, the arguments of a call to a function that takes
// a type, the `return` of a function that returns one, and either side of `==` and `!=`.

constexpr size_t smith_lang_ty_types_max = 4096;
constexpr size_t smith_lang_ty_spelling_max = 256;
constexpr size_t smith_lang_ty_names_max = 512;
constexpr size_t smith_lang_ty_name_max = 64;
constexpr size_t smith_lang_ty_frames_max = 64;
constexpr size_t smith_lang_ty_chain_max = 12;
constexpr size_t smith_lang_ty_run_max = 12;

typedef enum : uint8_t {
    SmithLangTy_Var,       // a variable, parameter or member of a type that is, or holds, a type
    SmithLangTy_Holder,    // a typedef name of a struct that holds a type
    SmithLangTy_HolderTag, // the tag of such a struct
    SmithLangTy_Func,      // a function that takes or returns a type
    SmithLangTy_Plain,     // a variable, parameter or member named `type` of an ordinary type: C's identifier
} SmithLangTyKind;

typedef enum : uint8_t {
    SmithLangRegion_None,
    SmithLangRegion_Init,   // after the `=` of a declaration of something that holds a type
    SmithLangRegion_Call,   // the parentheses of a call to a function that takes a type
    SmithLangRegion_Return, // the expression of a `return` in a function that returns a type
} SmithLangRegion;

typedef struct {
    char name[smith_lang_ty_name_max];
    char field[smith_lang_ty_name_max]; // a holder: the member that holds the type
    SmithLangTyKind kind;
    uint32_t depth; // a variable: the number of open braces where it is declared (a parameter's body is one more)
} SmithLangTyName;

typedef struct {
    bool is_struct;
    bool holds;
    bool is_typedef;
    bool returns_type; // the body of a function that returns a type
    char tag[smith_lang_ty_name_max];
    char field[smith_lang_ty_name_max];
} SmithLangTyFrame;

typedef struct {
    char text[smith_lang_ty_name_max];
    SmithTokenKind kind;
} SmithLangTyLast;

// The types met so far, by handle index.
typedef struct {
    char spelling[smith_lang_ty_types_max][smith_lang_ty_spelling_max]; // as the compiler reads it
    char canonical[smith_lang_ty_types_max][smith_lang_ty_spelling_max];
    uint64_t handle[smith_lang_ty_types_max];
    size_t count;
} SmithLangTyTable;

typedef struct {
    SmithLangTyName names[smith_lang_ty_names_max];
    size_t name_count;
    SmithLangTyFrame frames[smith_lang_ty_frames_max];
    size_t frame_count;
    uint32_t paren; // open ( and [
    SmithLangTyLast last[2];
    bool decl_constexpr;
    bool decl_typedef;
    bool decl_holds; // a variable of a type that holds a type is being declared (outside parentheses)
    bool decl_name;  // the next identifier is a declarator's name
    bool decl_spec_type;
    char decl_field[smith_lang_ty_name_max]; // the member that holds the type being declared
    bool specifier;                          // the `type` just rewritten: the next identifier is a declarator
    bool struct_pending;
    char struct_tag[smith_lang_ty_name_max];
    bool typedef_holder; // after the `}` of a typedef'd struct that holds a type: the next name is the typedef's
    char typedef_field[smith_lang_ty_name_max];
    SmithLangRegion region;
    uint32_t region_depth; // open ( [ { inside the region
    uint32_t protect;      // 1 + the paren depth inside `sizeof(` and the like, where a type is a type; 0: none
    bool protect_next;
    bool call_pending;
    bool return_pending; // a function returning a type was declared: its body's `{` is next
    char owner[smith_lang_ty_name_max];
    char ident0[smith_lang_ty_name_max]; // the last identifier outside parentheses
    size_t passthrough;                  // tokens copied into a rewrite, which are not looked at again
} SmithLangTy;

static SmithLangTyTable smith_lang_ty_table;
static SmithLangTy smith_lang_ty;

static void smith_lang_ty_reset(void) {
    smith_lang_ty_table.count = 0;
    memset(&smith_lang_ty, 0, sizeof smith_lang_ty);
}

// ---- Handles

static const char *const smith_lang_ty_basic[] = {"void",     "char",     "short",    "int",    "long",  "float",
                                                  "double",   "signed",   "unsigned", "_Bool",  "bool",  "const",
                                                  "volatile", "restrict", "_Complex", "struct", "union", "enum"};

static const char *smith_lang_ty_standard(const char *name, size_t length) {
    for (size_t i = 0; i < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; i++) { // 13 entries
        if (smith_lang_equals(name, length, smith_lang_sized[i].name)) return smith_lang_sized[i].standard;
    }
    return nullptr;
}

static bool smith_lang_ty_is_basic(const char *name, size_t length) {
    for (size_t i = 0; i < sizeof smith_lang_ty_basic / sizeof smith_lang_ty_basic[0]; i++) { // 18 words
        if (smith_lang_equals(name, length, smith_lang_ty_basic[i])) return true;
    }
    return false;
}

static unsigned smith_lang_ty_log2(uint64_t value) {
    unsigned bits = 0;
    while (value > 1 && bits < 31) { // a power of two
        value >>= 1;
        bits += 1;
    }
    return bits;
}

static uint64_t smith_lang_ty_pack(size_t index, const SmithTypeQuery *query) {
    uint64_t kinds = (query->is_integer ? 1U : 0U) | (query->is_float ? 2U : 0U) | (query->is_signed ? 4U : 0U) |
                     (query->is_pointer ? 8U : 0U) | (query->is_bool ? 16U : 0U) | (query->is_enum ? 32U : 0U) |
                     (query->is_record ? 64U : 0U) | (query->is_array ? 128U : 0U);
    uint64_t size = query->complete ? (query->size > UINT32_MAX ? UINT32_MAX : query->size) : 0;
    uint64_t incomplete = query->complete ? 0 : 1;
    return (uint64_t)index | (kinds << 16) | ((uint64_t)smith_lang_ty_log2(query->alignment) << 24) |
           (incomplete << 29) | (size << 32);
}

// The handle of the type written `spelling` (as the compiler reads it), added to the table when it is new. False after
// an error.
static bool smith_lang_ty_handle(SmithLang *lang, const SmithHost *host, const char *spelling, uint32_t location,
                                 uint64_t *handle) {
    if (host->type_query == nullptr) {
        smith_lang_error(lang, host, location,
                         "a type as a value needs smith's compiler to read it; the raw lowering has none");
        return false;
    }
    SmithTypeQuery query = {};
    if (!host->type_query(host->context, spelling, (uint32_t)strlen(spelling), &query)) {
        smith_lang_error(lang, host, location, "the compiler cannot read the type `%s`", spelling);
        return false;
    }
    char canonical[smith_lang_ty_spelling_max];
    snprintf(canonical, sizeof canonical, "%.*s", (int)query.canonical_length, query.canonical);
    SmithLangTyTable *table = &smith_lang_ty_table;
    for (size_t i = 0; i < table->count; i++) { // the types met
        if (strcmp(table->canonical[i], canonical) == 0) {
            *handle = table->handle[i];
            return true;
        }
    }
    if (table->count >= smith_lang_ty_types_max) {
        smith_lang_error(lang, host, location, "this file uses more than %zu different types as values",
                         smith_lang_ty_types_max);
        return false;
    }
    size_t index = table->count++;
    snprintf(table->spelling[index], sizeof table->spelling[index], "%s", spelling);
    snprintf(table->canonical[index], sizeof table->canonical[index], "%s", canonical);
    table->handle[index] = smith_lang_ty_pack(index, &query);
    *handle = table->handle[index];
    return true;
}

// The spelling of the type a handle names. Null when the number is no handle of this file.
static const char *smith_lang_ty_spelling_of(uint64_t handle) {
    size_t index = (size_t)(handle & 0xFFFF);
    if (index >= smith_lang_ty_table.count || smith_lang_ty_table.handle[index] != handle) return nullptr;
    return smith_lang_ty_table.spelling[index];
}

// ---- Names

static SmithLangTyName *smith_lang_ty_find(const char *name, size_t length, SmithLangTyKind kind) {
    for (size_t i = smith_lang_ty.name_count; i > 0; i--) { // the newest first
        SmithLangTyName *entry = &smith_lang_ty.names[i - 1];
        if (entry->kind == kind && smith_lang_equals(name, length, entry->name)) return entry;
    }
    return nullptr;
}

static SmithLangTyName *smith_lang_ty_add(const char *name, size_t length, SmithLangTyKind kind, uint32_t depth) {
    if (smith_lang_ty.name_count >= smith_lang_ty_names_max || length >= smith_lang_ty_name_max) return nullptr;
    SmithLangTyName *entry = &smith_lang_ty.names[smith_lang_ty.name_count++];
    *entry = (SmithLangTyName){.kind = kind, .depth = depth};
    memcpy(entry->name, name, length);
    return entry;
}

// The variables declared in a scope that has closed (or parameters of a function that had no body) are forgotten.
static void smith_lang_ty_forget(uint32_t depth) {
    size_t kept = 0;
    for (size_t i = 0; i < smith_lang_ty.name_count; i++) { // the names
        const SmithLangTyName *entry = &smith_lang_ty.names[i];
        if ((entry->kind == SmithLangTy_Var || entry->kind == SmithLangTy_Plain) && entry->depth > depth) continue;
        smith_lang_ty.names[kept++] = *entry;
    }
    smith_lang_ty.name_count = kept;
}

static SmithLangTyFrame *smith_lang_ty_top(void) {
    return smith_lang_ty.frame_count > 0 ? &smith_lang_ty.frames[smith_lang_ty.frame_count - 1] : nullptr;
}

static bool smith_lang_ty_in_struct(void) {
    const SmithLangTyFrame *top = smith_lang_ty_top();
    return top != nullptr && top->is_struct;
}

static bool smith_lang_ty_in_returning(void) {
    for (size_t i = 0; i < smith_lang_ty.frame_count; i++) { // the open braces
        if (smith_lang_ty.frames[i].returns_type) return true;
    }
    return false;
}

// ---- Reading the tokens around one

static bool smith_lang_ty_last_is(size_t back, const char *text) {
    return smith_lang_equals(smith_lang_ty.last[back].text, strlen(smith_lang_ty.last[back].text), text);
}

static bool smith_lang_ty_is_name_token(const SmithToken *token) {
    return token->kind == SmithTokenKind_Identifier &&
           smith_lang_word(token->text, token->length) == SmithLangWord_Other;
}

// Whether a token before a declaration's type: the end of the one before, or a specifier.
static bool smith_lang_ty_at_start(void) {
    const SmithLangTyLast *last = &smith_lang_ty.last[0];
    if (last->text[0] == 0) return true;
    static const char *const starts[] = {";", "{", "}", "(", ",", "constexpr", "static", "const", "extern", "typedef"};
    for (size_t i = 0; i < sizeof starts / sizeof starts[0]; i++) { // 10 starts
        if (strcmp(last->text, starts[i]) == 0) return true;
    }
    return false;
}

static SmithToken smith_lang_ty_made(SmithTokenKind kind, const char *text, const SmithToken *like, bool space) {
    return smith_lang_tuple_made(kind, text, strlen(text), like, space);
}

// The rest of the queue after the first `consumed` tokens is kept; the tokens made go in front of it.
static bool smith_lang_ty_put(SmithLang *lang, size_t consumed, const SmithToken *made, size_t count) {
    return smith_lang_tuple_replace(lang, consumed, made, count);
}

// ---- Type leaves: a type written where a value is expected

static bool smith_lang_ty_type_start(const SmithHost *host, const SmithToken *token) {
    if (token->kind != SmithTokenKind_Identifier) return false;
    if (smith_lang_ty_is_basic(token->text, token->length) || smith_lang_ty_standard(token->text, token->length)) {
        return true;
    }
    if (smith_lang_ty_find(token->text, token->length, SmithLangTy_Var) != nullptr) return false;
    return smith_lang_word(token->text, token->length) == SmithLangWord_Other &&
           host->lookup(host->context, token->text, token->length, SmithLookup_Type).kind == SmithLookup_Type;
}

// The length of the type that begins with the token being lowered: words, a tag after struct/union/enum, one typedef
// name, and `*`s. The tokens after the first are peeked. 0 when it is no type.
static size_t smith_lang_ty_run(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!smith_lang_ty_type_start(host, token)) return 0;
    bool named = !smith_lang_ty_is_basic(token->text, token->length);
    bool tag_next =
        smith_lang_ident(token, "struct") || smith_lang_ident(token, "union") || smith_lang_ident(token, "enum");
    size_t length = 1;
    for (; length < smith_lang_ty_run_max; length++) { // a type is a few tokens
        const SmithToken *next = smith_lang_peek(lang, host, length - 1, false);
        bool part = false;
        if (next->kind == SmithTokenKind_Punctuator) {
            part = smith_lang_punct(next, "*");
        } else if (next->kind == SmithTokenKind_Identifier) {
            if (tag_next) {
                part = true;
                tag_next = false;
            } else if (smith_lang_ty_is_basic(next->text, next->length)) {
                part = true;
                tag_next = smith_lang_ident(next, "struct") || smith_lang_ident(next, "union") ||
                           smith_lang_ident(next, "enum");
            } else if (!named && smith_lang_ty_type_start(host, next)) {
                part = true;
                named = true;
            }
        }
        if (!part) break;
    }
    return length;
}

// Appends a token's text to a spelling, mapping a sized name to the compiler's own.
static void smith_lang_ty_spell_add(char *out, size_t capacity, const char *text, size_t length) {
    const char *standard = smith_lang_ty_standard(text, length);
    size_t used = strlen(out);
    if (standard != nullptr) {
        text = standard;
        length = strlen(standard);
    }
    if (used + length + 2 >= capacity) return;
    if (used > 0) out[used++] = ' ';
    memcpy(out + used, text, length);
    out[used + length] = 0;
}

static bool smith_lang_ty_operand_start(const SmithToken *token) {
    return token->kind == SmithTokenKind_Identifier || token->kind == SmithTokenKind_Number ||
           token->kind == SmithTokenKind_String || token->kind == SmithTokenKind_Character ||
           smith_lang_punct(token, "(") || smith_lang_punct(token, "{");
}

// Whether the run of `length` tokens, which begins with the token being lowered, is a type value here.
static bool smith_lang_ty_leaf_here(SmithLang *lang, const SmithHost *host, size_t length) {
    SmithLangTy *ty = &smith_lang_ty;
    if (ty->protect != 0 && ty->paren >= ty->protect - 1) return false; // `sizeof(i32)`
    if (smith_lang_ty_last_is(0, ".") || smith_lang_ty_last_is(0, "->")) return false;
    const SmithToken *next = smith_lang_peek(lang, host, length - 1, false);
    if (next->kind == SmithTokenKind_Identifier && smith_lang_word(next->text, next->length) == SmithLangWord_Other) {
        return false; // a declaration: `i32 x`
    }
    bool beside_equal = smith_lang_ty_last_is(0, "==") || smith_lang_ty_last_is(0, "!=") ||
                        smith_lang_punct(next, "==") || smith_lang_punct(next, "!=");
    if (ty->region == SmithLangRegion_None && !beside_equal) return false;
    if (smith_lang_ty_last_is(0, "(") && smith_lang_punct(next, ")")) {
        bool own = ty->region == SmithLangRegion_Call && ty->region_depth == 1;
        if (!own && smith_lang_ty_operand_start(smith_lang_peek(lang, host, length, false))) return false; // a cast
    }
    return true;
}

// The type value at the token being lowered, if it is one: the run's tokens become the handle's number.
static bool smith_lang_ty_leaf(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    size_t length = smith_lang_ty_run(lang, host, token);
    if (length == 0 || !smith_lang_ty_leaf_here(lang, host, length)) return false;
    char spelling[smith_lang_ty_spelling_max] = "";
    smith_lang_ty_spell_add(spelling, sizeof spelling, token->text, token->length);
    for (size_t at = 1; at < length; at++) { // the rest of the run
        const SmithToken *part = smith_lang_peek(lang, host, at - 1, false);
        smith_lang_ty_spell_add(spelling, sizeof spelling, part->text, part->length);
    }
    uint64_t handle = 0;
    if (!smith_lang_ty_handle(lang, host, spelling, token->location, &handle)) handle = 0;
    char number[32];
    snprintf(number, sizeof number, "%lluULL", (unsigned long long)handle);
    SmithToken made = smith_lang_ty_made(SmithTokenKind_Number, number, token, token->space_before);
    smith_lang_ty_put(lang, length - 1, &made, 1);
    return true;
}

// ---- A type held in a variable: the value of a chain `t`, `foo.t`, `types[1]`

typedef struct {
    SmithToken tokens[smith_lang_ty_chain_max]; // the first is the token being lowered
    size_t count;
    char text[256]; // the tokens joined by \x1f, for the compiler
} SmithLangTyChain;

// The chain that begins with the token being lowered, a variable that holds a type, then `.name` and `[index]`s. False
// when the token begins none.
static bool smith_lang_ty_chain(SmithLang *lang, const SmithHost *host, const SmithToken *token, size_t first,
                                SmithLangTyChain *chain) {
    const SmithToken *root = first == 0 ? token : smith_lang_peek(lang, host, first - 1, false);
    if (root->kind != SmithTokenKind_Identifier ||
        smith_lang_ty_find(root->text, root->length, SmithLangTy_Var) == nullptr) {
        return false;
    }
    chain->count = 0;
    chain->tokens[chain->count++] = *root;
    for (size_t guard = 0; chain->count + 3 < smith_lang_ty_chain_max && guard < smith_lang_ty_chain_max; guard++) {
        size_t at = first + chain->count; // the queue index of the token after the chain
        const SmithToken *next = smith_lang_peek(lang, host, at - 1, false);
        const SmithToken *after = smith_lang_peek(lang, host, at, false);
        if (smith_lang_punct(next, ".") && after->kind == SmithTokenKind_Identifier) {
            chain->tokens[chain->count++] = *next;
            chain->tokens[chain->count++] = *after;
        } else if (smith_lang_punct(next, "[") && after->kind == SmithTokenKind_Number &&
                   smith_lang_punct(smith_lang_peek(lang, host, at + 1, false), "]")) {
            chain->tokens[chain->count++] = *next;
            chain->tokens[chain->count++] = *after;
            chain->tokens[chain->count++] = *smith_lang_peek(lang, host, at + 1, false);
        } else {
            break;
        }
    }
    size_t used = 0;
    for (size_t i = 0; i < chain->count && used + chain->tokens[i].length + 2 < sizeof chain->text; i++) {
        if (i > 0) chain->text[used++] = '\x1f';
        memcpy(chain->text + used, chain->tokens[i].text, chain->tokens[i].length);
        used += chain->tokens[i].length;
    }
    chain->text[used] = 0;
    return true;
}

// The handle a chain holds, asked of the compiler. False after an error.
static bool smith_lang_ty_chain_value(SmithLang *lang, const SmithHost *host, const SmithLangTyChain *chain,
                                      uint64_t *handle) {
    uint32_t location = chain->tokens[0].location;
    if (host->value_query == nullptr) {
        smith_lang_error(lang, host, location, "the value of a type needs smith's compiler; the raw lowering has none");
        return false;
    }
    SmithValue nodes[4];
    SmithValueQuery query = {.nodes = nodes, .capacity = 4};
    if (!host->value_query(host->context, chain->text, (uint32_t)strlen(chain->text), &query)) {
        smith_lang_error(lang, host, location,
                         "the type `%.*s` has no value here: %s (a `constexpr type` initialized from an array or a "
                         "member is known to the compiler only once the next statement has begun: use it from the "
                         "statement after)",
                         (int)chain->tokens[0].length, chain->tokens[0].text,
                         query.why != nullptr ? query.why : "the compiler cannot evaluate it");
        return false;
    }
    *handle = query.root.kind == SmithValue_Int ? query.root.magnitude : 0;
    if (smith_lang_ty_spelling_of(*handle) == nullptr) {
        smith_lang_error(lang, host, location, "`%.*s` is not the value of a type", (int)chain->tokens[0].length,
                         chain->tokens[0].text);
        return false;
    }
    return true;
}

// The tokens of a spelling (words and `*`), made after `like`.
static size_t smith_lang_ty_spelling_tokens(const char *spelling, const SmithToken *like, SmithToken *out,
                                            size_t capacity) {
    size_t count = 0;
    bool space = like->space_before;
    for (size_t at = 0; spelling[at] != 0 && count < capacity;) { // words and stars
        if (spelling[at] == ' ') {
            at += 1;
            space = true;
            continue;
        }
        size_t end = at + 1;
        if (spelling[at] != '*') {
            while (spelling[end] != 0 && spelling[end] != ' ' && spelling[end] != '*')
                end += 1;
        }
        char piece[smith_lang_ty_spelling_max];
        snprintf(piece, sizeof piece, "%.*s", (int)(end - at), spelling + at);
        for (size_t k = 0; k < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; k++) { // 13 entries
            if (strcmp(piece, smith_lang_sized[k].standard) == 0) { // the lowering reads the sized name, not the macro
                snprintf(piece, sizeof piece, "%s", smith_lang_sized[k].name);
                break;
            }
        }
        out[count++] =
            smith_lang_tuple_made(spelling[at] == '*' ? SmithTokenKind_Punctuator : SmithTokenKind_Identifier, piece,
                                  strlen(piece), like, space);
        space = true;
        at = end;
    }
    return count;
}

// `t x;`, `(t)y` or the argument of a generic call, where `t` holds a type: the chain becomes the type it names.
static bool smith_lang_ty_use(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTyChain chain;
    if (smith_lang_ty_last_is(0, ".") || smith_lang_ty_last_is(0, "->")) return false;
    if (!smith_lang_ty_chain(lang, host, token, 0, &chain)) return false;
    const SmithToken *next = smith_lang_peek(lang, host, chain.count - 1, false);
    bool declaration =
        smith_lang_ty_at_start() &&
        (smith_lang_ty_is_name_token(next) ||
         (smith_lang_punct(next, "*") && smith_lang_ty_is_name_token(smith_lang_peek(lang, host, chain.count, false))));
    bool argument = smith_lang_capturing(lang) && (smith_lang_ty_last_is(0, "(") || smith_lang_ty_last_is(0, ",")) &&
                    (smith_lang_punct(next, ")") || smith_lang_punct(next, ","));
    bool cast = smith_lang_ty_last_is(0, "(") && smith_lang_punct(next, ")") &&
                smith_lang_ty_operand_start(smith_lang_peek(lang, host, chain.count, false)) &&
                (smith_lang_ty.last[1].kind != SmithTokenKind_Identifier || smith_lang_ty_last_is(1, "return"));
    if (!declaration && !argument && !cast) return false;
    uint64_t handle = 0;
    static SmithToken made[smith_lang_ty_spelling_max];
    size_t count = 0;
    if (smith_lang_ty_chain_value(lang, host, &chain, &handle)) {
        count =
            smith_lang_ty_spelling_tokens(smith_lang_ty_spelling_of(handle), token, made, smith_lang_ty_spelling_max);
    }
    if (count == 0) {
        made[0] = smith_lang_ty_made(SmithTokenKind_Identifier, "int", token, token->space_before);
        count = 1;
    }
    smith_lang_ty_put(lang, chain.count - 1, made, count);
    return true;
}

// ---- Questions of a type: sizeof(t), alignof(t), is_integer(t)...

typedef struct {
    const char *name;
    unsigned shift;
} SmithLangTyQuestion;

static const SmithLangTyQuestion smith_lang_ty_questions[] = {
    {"is_integer", 16}, {"is_float", 17}, {"is_signed", 18}, {"is_pointer", 19},
    {"is_bool", 20},    {"is_enum", 21},  {"is_record", 22}, {"is_array", 23},
};

// `sizeof(t)` and its kin, where `t` holds a type: an expression over the handle, written in the C given to the
// compiler (`((t) >> 32)`), and exact for the target because the handle was made from the compiler's answer.
static bool smith_lang_ty_question(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    bool size = smith_lang_ident(token, "sizeof");
    bool align = smith_lang_ident(token, "alignof") || smith_lang_ident(token, "_Alignof");
    const SmithLangTyQuestion *question = nullptr;
    for (size_t i = 0; i < sizeof smith_lang_ty_questions / sizeof smith_lang_ty_questions[0]; i++) { // 8 questions
        if (smith_lang_ident(token, smith_lang_ty_questions[i].name)) question = &smith_lang_ty_questions[i];
    }
    if ((!size && !align && question == nullptr) || !smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(")) {
        return false;
    }
    SmithLangTyChain chain;
    if (!smith_lang_ty_chain(lang, host, token, 2, &chain)) return false;
    if (!smith_lang_punct(smith_lang_peek(lang, host, chain.count + 1, false), ")")) return false;
    static SmithToken made[smith_lang_ty_chain_max + 16];
    size_t used = 0;
    char number[16];
    if (align) {
        made[used++] = smith_lang_ty_made(SmithTokenKind_Punctuator, "(", token, token->space_before);
        made[used++] = smith_lang_ty_made(SmithTokenKind_Number, "1ULL", token, false);
        made[used++] = smith_lang_ty_made(SmithTokenKind_Punctuator, "<<", token, true);
    }
    made[used++] = smith_lang_ty_made(SmithTokenKind_Punctuator, "(", token, align ? true : token->space_before);
    if (!size) made[used++] = smith_lang_ty_made(SmithTokenKind_Punctuator, "(", token, false);
    made[used++] = smith_lang_ty_made(SmithTokenKind_Punctuator, "(", token, false);
    for (size_t i = 0; i < chain.count; i++) // the chain, as written
        made[used++] = chain.tokens[i];
    made[used++] = smith_lang_ty_made(SmithTokenKind_Punctuator, ")", token, false);
    made[used++] = smith_lang_ty_made(SmithTokenKind_Punctuator, ">>", token, true);
    snprintf(number, sizeof number, "%u", size ? 32U : align ? 24U : question->shift);
    made[used++] = smith_lang_ty_made(SmithTokenKind_Number, number, token, true);
    if (!size) {
        made[used++] = smith_lang_ty_made(SmithTokenKind_Punctuator, ")", token, false);
        made[used++] = smith_lang_ty_made(SmithTokenKind_Punctuator, "&", token, true);
        made[used++] = smith_lang_ty_made(SmithTokenKind_Number, align ? "31" : "1", token, true);
    }
    made[used++] = smith_lang_ty_made(SmithTokenKind_Punctuator, ")", token, false);
    if (align) made[used++] = smith_lang_ty_made(SmithTokenKind_Punctuator, ")", token, false);
    if (smith_lang_ty_put(lang, 2 + chain.count, made, used)) smith_lang_ty.passthrough = chain.count;
    return true;
}

// ---- Following the shape of the file

static void smith_lang_ty_copy(char *out, const char *text, size_t length) {
    size_t take = length < smith_lang_ty_name_max - 1 ? length : smith_lang_ty_name_max - 1;
    memcpy(out, text, take);
    out[take] = 0;
}

static void smith_lang_ty_end_declaration(void) {
    SmithLangTy *ty = &smith_lang_ty;
    ty->decl_constexpr = false;
    ty->decl_typedef = false;
    ty->decl_holds = false;
    ty->decl_name = false;
    ty->decl_spec_type = false;
    ty->decl_field[0] = 0;
}

// A variable that is, or holds, a type was declared without `constexpr`: it would exist at run time.
static void smith_lang_ty_runtime_error(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTy *ty = &smith_lang_ty;
    if (ty->decl_spec_type) {
        smith_lang_error(lang, host, token->location,
                         "`%.*s` is a `type`, which exists only at compile time: declare it `constexpr`",
                         (int)token->length, token->text);
        return;
    }
    smith_lang_error(lang, host, token->location,
                     "`%.*s` holds a type (`%.*s.%s`), which exists only at compile time: declare it `constexpr`",
                     (int)token->length, token->text, (int)token->length, token->text, ty->decl_field);
}

// The identifier that names what a declaration declares: a function that takes or returns a type, a member that holds
// one, or a variable or parameter that is or holds one.
static void smith_lang_ty_declarator(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTy *ty = &smith_lang_ty;
    bool param = ty->paren > 0;
    ty->decl_name = false;
    if (ty->decl_typedef && !param) return;
    if (param && ty->paren == 1 && ty->owner[0] != 0 &&
        smith_lang_ty_find(ty->owner, strlen(ty->owner), SmithLangTy_Func) == nullptr) {
        smith_lang_ty_add(ty->owner, strlen(ty->owner), SmithLangTy_Func, 0);
    }
    if (!param && smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(")) {
        smith_lang_ty_add(token->text, token->length, SmithLangTy_Func, 0);
        ty->return_pending = ty->decl_spec_type;
        ty->decl_holds = false;
        return;
    }
    SmithLangTyFrame *top = smith_lang_ty_top();
    if (!param && top != nullptr && top->is_struct) { // a member that holds a type
        top->holds = true;
        char field[smith_lang_ty_name_max];
        const char *inner = ty->decl_spec_type ? "" : ty->decl_field;
        snprintf(field, sizeof field, "%.*s%s%s", (int)token->length, token->text, inner[0] != 0 ? "." : "", inner);
        smith_lang_ty_copy(top->field, field, strlen(field));
        return;
    }
    if (ty->decl_field[0] == 0) smith_lang_ty_copy(ty->decl_field, "type", 4);
    if (smith_lang_ty_add(token->text, token->length, SmithLangTy_Var, (uint32_t)ty->frame_count + (param ? 1U : 0U)) ==
        nullptr) {
        smith_lang_error(lang, host, token->location, "this file has too many names that hold types for the lowering");
    }
    if (!param && !ty->decl_constexpr) smith_lang_ty_runtime_error(lang, host, token);
}

// A struct that holds a type is a name for a declaration's type.
static void smith_lang_ty_holder_use(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTy *ty = &smith_lang_ty;
    if (!smith_lang_ty_is_name_token(token) || smith_lang_ty_last_is(0, ".") || smith_lang_ty_last_is(0, "->")) return;
    bool tag = smith_lang_ty_last_is(0, "struct");
    const SmithLangTyName *holder =
        smith_lang_ty_find(token->text, token->length, tag ? SmithLangTy_HolderTag : SmithLangTy_Holder);
    if (holder == nullptr || !smith_lang_ty_is_name_token(smith_lang_peek(lang, host, 0, false))) return;
    ty->decl_name = true;
    ty->decl_holds = ty->paren == 0;
    ty->decl_spec_type = false;
    smith_lang_ty_copy(ty->decl_field, holder->field, strlen(holder->field));
}

static bool smith_lang_ty_declaration_before(void) {
    const SmithLangTyLast *last = &smith_lang_ty.last[0];
    if (strcmp(last->text, "*") == 0) return true;
    static const char *const calls[] = {"return", "else", "case", "do", "sizeof", "alignof", "_Alignof"};
    for (size_t i = 0; i < sizeof calls / sizeof calls[0]; i++) { // 7 words
        if (strcmp(last->text, calls[i]) == 0) return false;
    }
    return last->kind == SmithTokenKind_Identifier;
}

static void smith_lang_ty_identifier(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTy *ty = &smith_lang_ty;
    if (ty->specifier) { // the `type` just rewritten
        ty->specifier = false;
        ty->decl_name = true;
        ty->decl_spec_type = true;
        ty->decl_holds = ty->paren == 0;
        smith_lang_ty_copy(ty->decl_field, "type", 4);
        return;
    }
    if (ty->decl_name && smith_lang_ty_is_name_token(token)) {
        smith_lang_ty_declarator(lang, host, token);
        return;
    }
    if (smith_lang_ident(token, "struct") || smith_lang_ident(token, "union")) {
        ty->struct_pending = true;
        ty->struct_tag[0] = 0;
    } else if (ty->struct_pending && ty->struct_tag[0] == 0 && smith_lang_ty_is_name_token(token) &&
               (smith_lang_ty_last_is(0, "struct") || smith_lang_ty_last_is(0, "union"))) {
        smith_lang_ty_copy(ty->struct_tag, token->text, token->length);
    }
    if (smith_lang_ident(token, "typedef")) ty->decl_typedef = true;
    if (smith_lang_ident(token, "constexpr")) ty->decl_constexpr = true;
    if (smith_lang_ident(token, "sizeof") || smith_lang_ident(token, "alignof") ||
        smith_lang_ident(token, "_Alignof") || smith_lang_ident(token, "typeof") ||
        smith_lang_ident(token, "__typeof__") || smith_lang_ident(token, "offsetof")) {
        ty->protect_next = true;
    }
    if (ty->typedef_holder && ty->paren == 0 && smith_lang_ty_is_name_token(token)) {
        SmithLangTyName *holder = smith_lang_ty_add(token->text, token->length, SmithLangTy_Holder, 0);
        if (holder != nullptr) smith_lang_ty_copy(holder->field, ty->typedef_field, strlen(ty->typedef_field));
        ty->typedef_holder = false;
    }
    smith_lang_ty_holder_use(lang, host, token);
    if (smith_lang_ty_is_name_token(token) &&
        smith_lang_ty_find(token->text, token->length, SmithLangTy_Func) != nullptr &&
        ty->region == SmithLangRegion_None && !ty->decl_name && !smith_lang_ty_declaration_before() &&
        smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(")) {
        ty->call_pending = true;
    }
    if (smith_lang_ident(token, "return") && ty->region == SmithLangRegion_None && smith_lang_ty_in_returning()) {
        ty->region = SmithLangRegion_Return;
        ty->region_depth = 0;
    }
    if (ty->paren == 0 && smith_lang_ty_is_name_token(token))
        smith_lang_ty_copy(ty->ident0, token->text, token->length);
}

static void smith_lang_ty_open_brace(void) {
    SmithLangTy *ty = &smith_lang_ty;
    if (ty->frame_count < smith_lang_ty_frames_max) {
        ty->frames[ty->frame_count++] = (SmithLangTyFrame){.is_struct = ty->struct_pending,
                                                           .is_typedef = ty->decl_typedef,
                                                           .returns_type = ty->return_pending && ty->paren == 0};
        smith_lang_ty_copy(ty->frames[ty->frame_count - 1].tag, ty->struct_tag, strlen(ty->struct_tag));
    }
    ty->struct_pending = false;
    ty->return_pending = false;
    if (ty->region == SmithLangRegion_None) {
        smith_lang_ty_end_declaration(); // a typedef's flag is in the frame now
    } else {
        ty->region_depth += 1;
    }
}

static void smith_lang_ty_close_brace(void) {
    SmithLangTy *ty = &smith_lang_ty;
    if (ty->region != SmithLangRegion_None) {
        if (ty->region_depth > 0)
            ty->region_depth -= 1;
        else
            ty->region = SmithLangRegion_None;
    }
    if (ty->frame_count == 0) return;
    SmithLangTyFrame done = ty->frames[--ty->frame_count];
    smith_lang_ty_forget((uint32_t)ty->frame_count);
    if (!done.is_struct) return;
    SmithLangTyFrame *parent = smith_lang_ty_top();
    if (done.holds && parent != nullptr && parent->is_struct && !parent->holds) {
        parent->holds = true;
        smith_lang_ty_copy(parent->field, done.field, strlen(done.field));
    }
    if (!done.holds) return;
    if (done.tag[0] != 0) {
        SmithLangTyName *holder = smith_lang_ty_add(done.tag, strlen(done.tag), SmithLangTy_HolderTag, 0);
        if (holder != nullptr) smith_lang_ty_copy(holder->field, done.field, strlen(done.field));
    }
    if (done.is_typedef) {
        ty->typedef_holder = true;
        smith_lang_ty_copy(ty->typedef_field, done.field, strlen(done.field));
    }
}

static void smith_lang_ty_punctuator(const SmithToken *token) {
    SmithLangTy *ty = &smith_lang_ty;
    if (smith_lang_punct(token, "(") || smith_lang_punct(token, "[")) {
        if (smith_lang_punct(token, "(") && ty->paren == 0)
            smith_lang_ty_copy(ty->owner, ty->ident0, strlen(ty->ident0));
        ty->paren += 1;
        if (ty->protect_next && smith_lang_punct(token, "(")) ty->protect = ty->paren + 1;
        ty->protect_next = false;
        if (ty->call_pending && smith_lang_punct(token, "(")) {
            ty->region = SmithLangRegion_Call;
            ty->region_depth = 0;
        }
        ty->call_pending = false;
        if (ty->region != SmithLangRegion_None) ty->region_depth += 1;
    } else if (smith_lang_punct(token, ")") || smith_lang_punct(token, "]")) {
        if (ty->paren > 0) ty->paren -= 1;
        if (ty->protect != 0 && ty->paren + 1 < ty->protect) ty->protect = 0;
        ty->decl_name = ty->decl_name && ty->paren == 0;
        if (ty->region != SmithLangRegion_None && ty->region_depth > 0) ty->region_depth -= 1;
        if (ty->region == SmithLangRegion_Call && ty->region_depth == 0) ty->region = SmithLangRegion_None;
    } else if (smith_lang_punct(token, "{")) {
        smith_lang_ty_open_brace();
    } else if (smith_lang_punct(token, "}")) {
        smith_lang_ty_close_brace();
    } else if (smith_lang_punct(token, ";") && ty->paren == 0) {
        if (ty->region != SmithLangRegion_Call && ty->region_depth == 0) ty->region = SmithLangRegion_None;
        smith_lang_ty_end_declaration();
        smith_lang_ty_forget((uint32_t)ty->frame_count);
        ty->struct_pending = false;
        ty->typedef_holder = false;
        ty->return_pending = false;
        ty->protect_next = false;
    } else if (smith_lang_punct(token, ",")) {
        if (ty->paren == 0 && ty->region == SmithLangRegion_Init && ty->region_depth == 0)
            ty->region = SmithLangRegion_None;
        if (ty->paren == 0 && ty->region == SmithLangRegion_None)
            ty->decl_name = ty->decl_holds;
        else if (ty->paren > 0)
            ty->decl_name = false;
    } else if (smith_lang_punct(token, "=") && ty->paren == 0 && ty->decl_holds && ty->region == SmithLangRegion_None) {
        ty->region = SmithLangRegion_Init;
        ty->region_depth = 0;
    }
}

// The token has been lowered: the state follows it. `as_value` when it stands for a type value written as a number.
static void smith_lang_ty_track(SmithLang *lang, const SmithHost *host, const SmithToken *token, bool as_value) {
    SmithLangTy *ty = &smith_lang_ty;
    if (token->kind == SmithTokenKind_Identifier) smith_lang_ty_identifier(lang, host, token);
    if (token->kind == SmithTokenKind_Punctuator) smith_lang_ty_punctuator(token);
    ty->last[1] = ty->last[0];
    if (as_value) {
        ty->last[0] = (SmithLangTyLast){.text = "0", .kind = SmithTokenKind_Number};
    } else {
        smith_lang_ty_copy(ty->last[0].text, token->text, token->length);
        ty->last[0].kind = token->kind;
    }
}

// ---- The tokens

// `constexpr type t;` as a member: `constexpr` is not valid on a member of C, so the member is a plain integer.
static bool smith_lang_ty_member_constexpr(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    return smith_lang_ident(token, "constexpr") && smith_lang_ty.paren == 0 && smith_lang_ty_in_struct() &&
           smith_lang_ident(smith_lang_peek(lang, host, 0, false), "type");
}

// `type` where a type is written (a declaration's specifier): an `unsigned long long`, the handle's type.
static bool smith_lang_ty_specifier(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTy *ty = &smith_lang_ty;
    if (!smith_lang_ident(token, "type") || ty->decl_name || !smith_lang_ty_at_start()) return false;
    if (ty->paren > 0 && smith_lang_ty_last_is(0, "constexpr")) return false; // a generic's `constexpr type T`
    const SmithToken *next = smith_lang_peek(lang, host, 0, false);
    // `type * x` where `type` is a variable declared here: the product, as in C. (`type x` is no C at all: the keyword.)
    if (smith_lang_punct(next, "*") && smith_lang_ty_find("type", 4, SmithLangTy_Plain) != nullptr) return false;
    if (!smith_lang_ty_is_name_token(next) && !smith_lang_punct(next, "*")) return false;
    SmithToken made[3];
    made[0] = smith_lang_ty_made(SmithTokenKind_Identifier, "unsigned", token, token->space_before);
    made[1] = smith_lang_ty_made(SmithTokenKind_Identifier, "long", token, true);
    made[2] = smith_lang_ty_made(SmithTokenKind_Identifier, "long", token, true);
    made[0].line_start = token->line_start;
    smith_lang_ty_put(lang, 0, made, 3);
    ty->specifier = true;
    return true;
}

// `type` as the name of a variable, parameter or member of another type (`c::Kind type`, `int type;`): C's identifier,
// which `type * n` after it must not read as the keyword. Remembered for the scope it is declared in.
static void smith_lang_ty_plain(const SmithToken *token) {
    SmithLangTy *ty = &smith_lang_ty;
    if (!smith_lang_ident(token, "type") || ty->decl_name || smith_lang_ty_at_start() ||
        !smith_lang_ty_declaration_before() || smith_lang_ty_last_is(0, ".") || smith_lang_ty_last_is(0, "->")) {
        return;
    }
    smith_lang_ty_add("type", 4, SmithLangTy_Plain, (uint32_t)ty->frame_count + (ty->paren == 1 ? 1U : 0U));
}

// Called for each token of a file with type values, before the lowering looks at it. True when the token was replaced:
// the tokens that stand for it are at the front of the queue.
static bool smith_lang_ty_token(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangTy *ty = &smith_lang_ty;
    if (token->synthetic || token->kind == SmithTokenKind_Eof) return false;
    bool replaced = false;
    bool as_value = false;
    if (ty->passthrough > 0) {
        ty->passthrough -= 1;
    } else if (token->kind == SmithTokenKind_Identifier) {
        replaced = smith_lang_ty_member_constexpr(lang, host, token) || smith_lang_ty_specifier(lang, host, token);
        if (!replaced) replaced = as_value = smith_lang_ty_question(lang, host, token);
        if (!replaced) replaced = as_value = smith_lang_ty_use(lang, host, token);
        if (!replaced) replaced = as_value = smith_lang_ty_leaf(lang, host, token);
        if (!replaced) smith_lang_ty_plain(token);
    }
    smith_lang_ty_track(lang, host, token, as_value);
    return replaced;
}

// ---- `Field`, the element of `fields(T)` (runtime/lang_fields.c)

// After a `module` or `import` line, when no other `import` follows: defines `Field` and `smith_field_make` in a file
// that uses `fields(` or the name, or imports a generic that reflects, so that the first declaration may use them and
// the compiler has read them before an instance's condition asks about a `Field`. Written as the lowering leaves them.
// Only the compile of the file defines them: the raw passes (headers, interfaces) have no parser.
static void smith_lang_field_after_imports(SmithLang *lang, const SmithHost *host) {
    if (lang->field_defined || host->type_field == nullptr || !(host->maybe_field || lang->field_import) ||
        lang->recording || !lang->is_module) {
        return;
    }
    if (smith_lang_ident(smith_lang_peek(lang, host, 0, false), "import")) return;
    smith_lang_fields_define(lang, host, nullptr, smith_lang_peek(lang, host, 0, false)->location, 0);
    SmithLangTyName *holder = smith_lang_ty_add("Field", 5, SmithLangTy_Holder, 0);
    if (holder != nullptr) snprintf(holder->field, sizeof holder->field, "type");
    holder = smith_lang_ty_add("smith_field", 11, SmithLangTy_HolderTag, 0);
    if (holder != nullptr) snprintf(holder->field, sizeof holder->field, "type");
}
