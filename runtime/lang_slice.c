// Run-time arrays of one dimension (issue #82): `T xs[:]` is a slice, a pointer and the number of elements that follow
// it. It is a struct the lowering writes where it is used, `struct smith_array_i32_n { int32_t *data; size_t count[1];
// }` (C23 accepts the same definition again, in any scope, so two files and two headers agree), and everything else is
// plain C over it:
//
//   xs[i]       *({ auto smith_s = (xs); size_t smith_i = (i); check(smith_i < smith_s.count[0]);
//   &smith_s.data[smith_i]; }) xs[a:b]     ({ auto smith_s = (xs); ...; check(a <= b && b <= smith_s.count[0]); (struct
//   smith_array_i32_n){...}; }) p[a:b]      the same over a pointer, whose extent is the program's word (the slice's
//   `smith prove` obligation) countof(xs) (xs).count[0]
//
// Because a subscript is read after the expression it subscripts has been written, the lowering reads a whole postfix
// chain (`a.items[i].rows[b:c]`) at its first token, from the queue, types each prefix with the compiler (`expr_type`)
// and writes the chain again. Included by runtime/lang.c, after runtime/lang_tuple.c.

constexpr size_t smith_lang_slice_chain_max = 200;
constexpr size_t smith_lang_slice_cur_max = 600;
constexpr size_t smith_lang_slice_dims_max = 8;
constexpr size_t smith_lang_slice_elements_max = 24;
constexpr size_t smith_lang_slice_scan_max = 48;
constexpr size_t smith_lang_slice_text_max = 4096;
constexpr size_t smith_lang_slice_args_max = 16;
constexpr char smith_lang_slice_prefix[] = "smith_array_";

// The end of the comment, string or character literal that begins at `at` (`at` itself when the text there is code): a
// `[:]` in either is no slice (issue #121). A `'` after a letter or digit is a digit separator.
static size_t smith_lang_text_skip(const char *text, size_t length, size_t at) {
    char c = text[at];
    if (c == '/' && at + 1 < length && text[at + 1] == '/') {
        while (at < length && text[at] != '\n')
            at += 1;
        return at;
    }
    if (c == '/' && at + 1 < length && text[at + 1] == '*') {
        at += 2;
        while (at + 1 < length && !(text[at] == '*' && text[at + 1] == '/'))
            at += 1;
        return at + 2 < length ? at + 2 : length;
    }
    bool quote =
        c == '"' || (c == '\'' && !(at > 0 && (isalnum((unsigned char)text[at - 1]) != 0 || text[at - 1] == '_')));
    if (!quote) return at;
    for (at += 1; at < length && text[at] != c && text[at] != '\n'; at++) {
        if (text[at] == '\\') at += 1;
    }
    return at < length ? at + 1 : length;
}

// Whether the text has a `[ ]` with a `:` that is neither `::` nor the `:` of a `? :` (the hook's hasSlice), outside
// comments and strings.
static bool smith_lang_text_has_slice(const char *text, size_t length) {
    for (size_t at = 0; at < length; at++) { // each `[` of the code
        size_t after = smith_lang_text_skip(text, length, at);
        if (after != at) {
            at = after - 1;
            continue;
        }
        if (text[at] != '[') continue;
        int depth = 0;
        int conditions = 0;
        for (size_t scan = at; scan < length && text[scan] != ';'; scan++) { // to its `]`
            size_t next = smith_lang_text_skip(text, length, scan);
            if (next != scan) {
                scan = next - 1;
                continue;
            }
            char c = text[scan];
            depth += c == '[' || c == '(' || c == '{';
            depth -= c == ']' || c == ')' || c == '}';
            if (depth <= 0) break;
            conditions += depth == 1 && c == '?';
            if (depth != 1 || c != ':') continue;
            if (scan + 1 < length && text[scan + 1] == ':') {
                scan += 1;
            } else if (conditions > 0) {
                conditions -= 1;
            } else {
                return true;
            }
        }
    }
    return false;
}

bool smith_lang_slices_on(const SmithLang *lang) {
    return lang->slices_seen;
}

static bool smith_lang_slice_active(const SmithLang *lang, const SmithHost *host) {
    return host->maybe_slice || lang->slices_seen;
}

// ---- Tokens from a pattern

typedef struct {
    const SmithToken *tokens;
    size_t count;
} SmithLangSliceSpan;

static SmithTokenKind smith_lang_slice_kind(const char *word) {
    unsigned char c = (unsigned char)word[0];
    if (c == '"') return SmithTokenKind_String;
    if (c >= '0' && c <= '9') return SmithTokenKind_Number;
    if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return SmithTokenKind_Identifier;
    return SmithTokenKind_Punctuator;
}

// The tokens of a pattern: words separated by one space, `@0` to `@9` for the tokens of a span. A made token is
// synthetic; the span's are as they were.
static size_t smith_lang_slice_build(SmithToken *out, size_t used, size_t capacity, const char *pattern,
                                     const SmithLangSliceSpan *spans, const SmithToken *like) {
    const char *at = pattern;
    bool open = true; // the word before was an opening bracket or `.`: no space after it
    while (*at != 0 && used + 1 < capacity) {
        while (*at == ' ')
            at += 1;
        const char *end = at;
        while (*end != 0 && *end != ' ')
            end += 1;
        size_t length = (size_t)(end - at);
        if (length == 0) break;
        if (at[0] == '@' && length == 2) {
            const SmithLangSliceSpan *span = &spans[at[1] - '0'];
            for (size_t i = 0; i < span->count && used + 1 < capacity; i++) // the span's tokens
                out[used++] = span->tokens[i];
            open = false;
        } else {
            bool closing = length == 1 && strchr(")],;.", at[0]) != nullptr;
            out[used++] = smith_lang_tuple_made(smith_lang_slice_kind(at), at, length, like, !open && !closing);
            open = length == 1 && strchr("([.", at[0]) != nullptr;
        }
        at = end;
    }
    return used;
}

// The tokens of a pattern (as smith_lang_slice_build), the unsigned type of a size spelled as the compiler does.
static size_t smith_lang_slice_pattern(SmithToken *out, size_t used, size_t capacity, const char *pattern,
                                       const SmithLangSliceSpan *spans, const SmithToken *like) {
    static unsigned counter;
    counter += 1;
    size_t from = used;
    used = smith_lang_slice_build(out, used, capacity, pattern, spans, like);
    for (size_t i = from; i < used; i++) { // each token made
        static const char *const temporaries[] = {"smith_s", "smith_i", "smith_a", "smith_b", "smith_p",
                                                  "smith_r", "smith_j", "smith_k", "smith_l", "smith_m",
                                                  "smith_n", "smith_o", "smith_q"};
        for (size_t t = 0; t < sizeof temporaries / sizeof temporaries[0]; t++) { // the names of the temporaries
            if (!out[i].synthetic || !smith_lang_equals(out[i].text, out[i].length, temporaries[t])) continue;
            char numbered[24];
            int length = snprintf(numbered, sizeof numbered, "%s%u", temporaries[t], counter);
            out[i] = smith_lang_tuple_made(SmithTokenKind_Identifier, numbered, (size_t)length, &out[i],
                                           out[i].space_before);
        }
        bool type_macro = out[i].synthetic && out[i].length > 9 && memcmp(out[i].text, "__", 2) == 0 &&
                          memcmp(out[i].text + out[i].length - 7, "_TYPE__", 7) == 0;
        if (!type_macro) continue;
        out[i].expand = true; // the compiler's own macro for a sized type
        if (out[i].length == 13 && memcmp(out[i].text, "__SIZE_TYPE__", 13) == 0) {
            out[i].header_text = "size_t";
            out[i].header_length = 6;
        }
    }
    return used;
}

// ---- The dimensions of a slice (issue #105)

// `f32[:][2]` is two dimensions, the first of them run-time. The struct of a run-time array keeps its sizes in
// `count[k]`, k the number of `:`; the fixed ones are in its name (`smith_array_f32_n_2`), and everything else (the
// strides, the checks) is written from them. The first dimension is always a `:`: a row of fixed size is a `[:]`.
typedef struct {
    size_t count;
    bool runtime[smith_lang_slice_dims_max];
    uint64_t fixed[smith_lang_slice_dims_max];
    bool restrict_data; // `[restrict :]`: the data pointer is `__restrict` (C and C++ headers both read it; its own
                        // struct, `smith_array_restrict_...`)
} SmithLangSliceDims;

static SmithLangSliceDims smith_lang_slice_dims_one(void) {
    SmithLangSliceDims dims = {.count = 1};
    dims.runtime[0] = true;
    return dims;
}

static size_t smith_lang_slice_dims_runtime(const SmithLangSliceDims *dims) {
    size_t runtime = 0;
    for (size_t at = 0; at < dims->count; at++) // at most 8
        runtime += dims->runtime[at] ? 1 : 0;
    return runtime;
}

// `_n_2` for `[:][2]`.
static void smith_lang_slice_dims_suffix(const SmithLangSliceDims *dims, char *out, size_t capacity) {
    size_t used = 0;
    out[0] = 0;
    for (size_t at = 0; at < dims->count && used + 24 < capacity; at++) { // at most 8
        used += dims->runtime[at]
                    ? (size_t)snprintf(out + used, capacity - used, "_n")
                    : (size_t)snprintf(out + used, capacity - used, "_%llu", (unsigned long long)dims->fixed[at]);
    }
}

// The last `_n` or `_<digits>` of a name before `end`: its start (the `_`) and whether it is a run-time dimension.
static bool smith_lang_slice_dims_last(const char *text, size_t end, size_t *start, bool *runtime, uint64_t *fixed) {
    size_t at = end;
    while (at > 0 && text[at - 1] != '_')
        at -= 1;
    if (at == 0 || at == end) return false;
    size_t length = end - at;
    *start = at - 1;
    if (length == 1 && text[at] == 'n') {
        *runtime = true;
        return true;
    }
    uint64_t value = 0;
    for (size_t i = at; i < end; i++) { // digits only
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10 + (uint64_t)(text[i] - '0');
    }
    *runtime = false;
    *fixed = value;
    return true;
}

// The dimensions a slice's tag names, and the length of the tag before them (its element's part). A tag's element
// never ends like a dimension (`_e` is written between), so reading from the end is unambiguous.
static bool smith_lang_slice_dims_parse(const char *tag, SmithLangSliceDims *dims, size_t *stem) {
    bool runtime[smith_lang_slice_dims_max];
    uint64_t fixed[smith_lang_slice_dims_max] = {};
    size_t found = 0;
    size_t end = strlen(tag);
    size_t start = 0;
    while (found < smith_lang_slice_dims_max &&
           smith_lang_slice_dims_last(tag, end, &start, &runtime[found], &fixed[found])) {
        found += 1;
        end = start;
    }
    if (found == 0) return false;
    *dims = (SmithLangSliceDims){.count = found};
    dims->restrict_data = strncmp(tag, smith_lang_slice_prefix, sizeof smith_lang_slice_prefix - 1) == 0 &&
                          strncmp(tag + sizeof smith_lang_slice_prefix - 1, "restrict_", 9) == 0;
    for (size_t at = 0; at < found; at++) { // read from the end
        dims->runtime[at] = runtime[found - 1 - at];
        dims->fixed[at] = fixed[found - 1 - at];
    }
    *stem = end;
    return true;
}

// ---- The name of a slice's struct

static void smith_lang_slice_tag_dims(const char *spelled, const SmithLangSliceDims *dims, char *tag, size_t capacity) {
    size_t length = strlen(spelled);
    char suffix[64];
    smith_lang_slice_dims_suffix(dims, suffix, sizeof suffix);
    size_t start = 0;
    bool runtime = false;
    uint64_t fixed = 0;
    const char *guard = smith_lang_slice_dims_last(spelled, length, &start, &runtime, &fixed) ? "_e" : "";
    const char *qualified = dims->restrict_data ? "restrict_" : "";
    if (length <= smith_lang_tuple_long) {
        snprintf(tag, capacity, "%s%s%s%s%s", smith_lang_slice_prefix, qualified, spelled, guard, suffix);
        return;
    }
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < length; i++) { // FNV-1a
        hash = (hash ^ (uint8_t)spelled[i]) * 1099511628211ull;
    }
    snprintf(tag, capacity, "%s%s%.80s_h%016llx%s", smith_lang_slice_prefix, qualified, spelled,
             (unsigned long long)hash, suffix);
}

static void smith_lang_slice_tag(const char *spelled, char *tag, size_t capacity) {
    SmithLangSliceDims one = smith_lang_slice_dims_one();
    smith_lang_slice_tag_dims(spelled, &one, tag, capacity);
}

// The struct a slice of the element written as `elem` is, or false with the reason at `where`.
static bool smith_lang_slice_tag_of(SmithLang *lang, const SmithToken *elem, size_t count,
                                    const SmithLangSliceDims *dims, char *tag, size_t capacity, const char **problem,
                                    uint32_t *where) {
    char spelled[smith_lang_tuple_name_max];
    if (count == 0) {
        *problem = "a slice needs its element type: `T xs[:]`";
        return false;
    }
    if (!smith_lang_tuple_spell(lang, elem, 0, count, spelled, sizeof spelled, problem, where)) return false;
    smith_lang_slice_tag_dims(spelled, dims, tag, capacity);
    return true;
}

// Whether the struct of this tag is defined at file scope by an earlier declaration: a later use names the tag only.
// (A second definition inside a function's head, after one at file scope, is read by the compiler but the token after
// the head is lost from the hook's stream; and a bare tag is shorter.)
static bool smith_lang_slice_defined(const SmithLang *lang, const char *tag) {
    for (uint32_t i = 0; i < lang->slice.tag_count; i++) { // at most 32
        if (strcmp(lang->slice.tags[i], tag) == 0) return true;
    }
    return false;
}

// The definition as a C header spells it, for a bare tag: the header of an export that names the struct holds it.
static const char *smith_lang_slice_header_text(SmithLang *lang, const char *tag, const SmithToken *elem, size_t count,
                                                size_t sizes, bool restrict_data) {
    char text[smith_lang_tuple_name_max];
    size_t used = (size_t)snprintf(text, sizeof text, "%s {", tag);
    for (size_t at = 0; at < count && used + smith_lang_name_max + 8 < sizeof text; at++) { // the element's tokens
        const SmithToken *token = &elem[at];
        char word[smith_lang_name_max];
        size_t taken = 1;
        const SmithLangSized *sized =
            token->kind == SmithTokenKind_Identifier ? smith_lang_find_sized(token->text, token->length) : nullptr;
        if (sized != nullptr) {
            snprintf(word, sizeof word, "%s", sized->header);
        } else if (token->kind == SmithTokenKind_Identifier &&
                   smith_lang_word(token->text, token->length) == SmithLangWord_Other) {
            taken = smith_lang_tuple_resolve(lang, elem, at, count, word, sizeof word);
        } else {
            snprintf(word, sizeof word, "%.*s", (int)(token->length < sizeof word ? token->length : sizeof word - 1),
                     token->text);
        }
        used += (size_t)snprintf(text + used, sizeof text - used, " %s", word);
        at += taken - 1;
    }
    snprintf(text + used, sizeof text - used, " * %sdata; size_t count [ %zu ]; }", restrict_data ? "__restrict " : "",
             sizes);
    return smith_lang_tuple_keep(text, strlen(text));
}

// The struct a slice is, ready for the queue: `struct tag { T *data; usize count[1]; }`, or only `struct tag` when an
// earlier declaration defined it at file scope. The first use at file scope defines it.
static size_t smith_lang_slice_define(SmithLang *lang, const SmithHost *host, SmithToken *out, size_t used,
                                      size_t capacity, const char *tag, const SmithToken *elem, size_t count,
                                      const SmithLangSliceDims *dims, const SmithToken *like) {
    size_t sizes = smith_lang_slice_dims_runtime(dims);
    bool bare = smith_lang_slice_defined(lang, tag);
    char pattern[smith_lang_tuple_name_max + 80];
    if (bare) {
        snprintf(pattern, sizeof pattern, "struct %s", tag);
    } else {
        snprintf(pattern, sizeof pattern, "struct %s { @0 * %sdata ; __SIZE_TYPE__ count [ %zu ] ; }", tag,
                 dims->restrict_data ? "__restrict " : "", sizes);
    }
    SmithLangSliceSpan spans[1] = {{elem, count}};
    size_t first = used;
    used = smith_lang_slice_pattern(out, used, capacity, pattern, spans, like);
    if (first > 0) out[first].space_before = true; // after `typedef`, `static` and the like
    if (bare) {
        const char *text = smith_lang_slice_header_text(lang, tag, elem, count, sizes, dims->restrict_data);
        out[used - 1].header_text = text;
        out[used - 1].header_length = (uint32_t)strlen(text);
        return used;
    }
    for (size_t i = first; i < used; i++) { // the braces of the definition, not those the element might hold
        if (out[i].synthetic && (smith_lang_punct(&out[i], "{") || smith_lang_punct(&out[i], "}")))
            out[i].tuple_brace = true;
    }
    // A file with no `module` line does not track its functions: the compiler says whether a body is open (issue #119).
    char returned[8];
    bool in_body = lang->in_function || (host != nullptr && host->return_type != nullptr &&
                                         host->return_type(host->context, returned, sizeof returned));
    if (!in_body && lang->paren_depth == 0 && lang->slice.tag_count < 32) {
        snprintf(lang->slice.tags[lang->slice.tag_count++], sizeof lang->slice.tags[0], "%s", tag);
    }
    return used;
}

// ---- A declaration: `T xs[:]`

static bool smith_lang_slice_boundary(const SmithToken *token);

static bool smith_lang_slice_statement_word(const SmithToken *token) {
    static const char *const words[] = {"return",  "sizeof", "case",    "goto",       "else",
                                        "do",      "if",     "while",   "for",        "switch",
                                        "default", "typeof", "alignof", "__typeof__", "defer"};
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) { // 15 words
        if (smith_lang_ident(token, words[i])) return true;
    }
    return false;
}

// The token `at` places into a declaration that begins with the one being lowered (0), then the queue.
static const SmithToken *smith_lang_slice_at(SmithLang *lang, const SmithHost *host, const SmithToken *first,
                                             size_t at) {
    return at == 0 ? first : smith_lang_peek(lang, host, at - 1, false);
}

// The qualifiers inside a slice's brackets, after C99's `int a[const 10]`: `[const :]` makes the slice itself const,
// `[restrict :]` its data pointer restrict. C's other two (`static`, `volatile`) mean nothing for a slice.
typedef struct {
    bool is_const;
    bool is_restrict;
    uint32_t bad_where; // the place of a qualifier that is no good
    const char *bad;    // and why
} SmithLangSliceQuals;

// Whether the word is one of C's qualifiers of a bracket.
static bool smith_lang_slice_qualifier(const SmithToken *token) {
    static const char *const words[] = {"const", "restrict", "volatile", "static", "_Atomic"};
    if (token->kind != SmithTokenKind_Identifier) return false;
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) { // 5 words
        if (smith_lang_ident(token, words[i])) return true;
    }
    return false;
}

// The brackets `[ q... : ]` whose `[` is the token `open` (the first token of the declaration counted 0): how many
// qualifier tokens stand before the `:`, or SIZE_MAX when it is no such bracket. `quals` gets what they say.
static size_t smith_lang_slice_bracket(SmithLang *lang, const SmithHost *host, const SmithToken *first, size_t open,
                                       SmithLangSliceQuals *quals) {
    size_t words = 0;
    while (words < 4 && smith_lang_slice_qualifier(smith_lang_slice_at(lang, host, first, open + 1 + words)))
        words += 1;
    if (!smith_lang_punct(smith_lang_slice_at(lang, host, first, open + 1 + words), ":") ||
        !smith_lang_punct(smith_lang_slice_at(lang, host, first, open + 2 + words), "]")) {
        return SIZE_MAX;
    }
    for (size_t at = 0; at < words && quals != nullptr; at++) { // each qualifier
        const SmithToken *word = smith_lang_slice_at(lang, host, first, open + 1 + at);
        if (smith_lang_ident(word, "const")) {
            quals->is_const = true;
        } else if (smith_lang_ident(word, "restrict")) {
            quals->is_restrict = true;
        } else if (quals->bad == nullptr) {
            quals->bad_where = word->location;
            quals->bad =
                smith_lang_ident(word, "static")
                    ? "`static` has no meaning for a slice: it has no size to promise (C's `int a[static 4]`); "
                      "write `[const :]` or `[restrict :]`"
                    : "`volatile` has no meaning in a slice's brackets: write it on the element, "
                      "`volatile u8 xs[:]`; the brackets take `const` and `restrict`";
        }
    }
    return words;
}

// Whether the declaration that begins with `first` declares a slice: the types and stars of the element, the name and
// `[ : ]` (with the qualifiers `[ const : ]` takes). Gives the index of the name and of the `]`, counting `first` as 0.
static bool smith_lang_slice_declaration(SmithLang *lang, const SmithHost *host, const SmithToken *first, size_t *name,
                                         size_t *close, SmithLangSliceQuals *quals) {
    if (first->kind != SmithTokenKind_Identifier || smith_lang_slice_statement_word(first)) return false;
    for (size_t at = 1; at < smith_lang_slice_scan_max; at++) { // a declaration's type is a few tokens
        const SmithToken *token = smith_lang_slice_at(lang, host, first, at);
        if (token->kind == SmithTokenKind_Eof || token->foreign) return false;
        if (at == 1 && smith_lang_punct(token, "::")) {
            const SmithLangBinding *named =
                lang->bindings_count > 0 ? smith_lang_binding_find(lang, first->text, first->length) : nullptr;
            if (named != nullptr && named->is_c) return false; // the name after `c::` must reach the stream as written
        }
        if (token->kind == SmithTokenKind_Identifier || smith_lang_punct(token, "*") || smith_lang_punct(token, "::")) {
            continue;
        }
        if (!smith_lang_punct(token, "[")) return false;
        size_t words = smith_lang_slice_bracket(lang, host, first, at, nullptr);
        if (words != SIZE_MAX && smith_lang_slice_at(lang, host, first, at - 1)->kind == SmithTokenKind_Identifier) {
            smith_lang_slice_bracket(lang, host, first, at, quals);
            *name = at - 1;
            *close = at + words + 2;
            return true;
        }
        return false;
    }
    return false;
}

// How many leading tokens of the declaration are not part of the element: storage classes, `typedef` and attributes.
static size_t smith_lang_slice_leading(const SmithToken *span, size_t name) {
    size_t at = 0;
    while (at < name) { // each leading word
        if (span[at].kind == SmithTokenKind_Identifier) {
            SmithLangWord word = smith_lang_word(span[at].text, span[at].length);
            bool exported = smith_lang_equals(span[at].text, span[at].length, "export");
            if (word != SmithLangWord_Storage && word != SmithLangWord_Typedef && !exported) break;
            at += 1;
        } else if (smith_lang_punct(&span[at], "[")) {
            while (at < name && !smith_lang_punct(&span[at], "]"))
                at += 1;
            at += 2; // the two closing brackets
        } else {
            break;
        }
    }
    return at < name ? at : name;
}

static void smith_lang_slice_flag(SmithLang *lang) {
    // A generic's head (`typeof(T[:]) push_slice(...)`) is no slice written in the module: its importers keep the
    // plain lowering unless they write a `[:]` of their own, since every module imports arena.
    if (lang->decl.generic) return;
    lang->slices_seen = true;
    lang->tuples_seen = true;
}

// The groups after the first `[ : ]`, from the index `last` (of the first group's `]`, the first token counted 0 as in
// smith_lang_slice_at): `[ : ]` is a run-time dimension and `[ 2 ]` a fixed one. Adds them to `dims`; gives the index
// of the last `]` and, when a group is not one, a problem (the group is skipped).
static size_t smith_lang_slice_groups(SmithLang *lang, const SmithHost *host, const SmithToken *first, size_t last,
                                      SmithLangSliceDims *dims, const char **problem, uint32_t *where) {
    for (size_t groups = 0; groups < smith_lang_slice_dims_max; groups++) { // each `[ ... ]` after the name
        const SmithToken *open = smith_lang_slice_at(lang, host, first, last + 1);
        if (!smith_lang_punct(open, "[") || smith_lang_punct(smith_lang_slice_at(lang, host, first, last + 2), "["))
            break;
        size_t words = smith_lang_slice_bracket(lang, host, first, last + 1, nullptr);
        if (words != SIZE_MAX && words > 0 && dims->count < smith_lang_slice_dims_max) {
            *problem = "only the outermost bracket may carry qualifiers, as in C (`f32 m[const :][:]`): the "
                       "rows of a slice of more dimensions are views, with no pointer of their own to qualify";
            *where = open->location;
            dims->runtime[dims->count] = true;
            dims->count += 1;
            last += words + 3;
            continue;
        }
        const SmithToken *inside = smith_lang_slice_at(lang, host, first, last + 2);
        const SmithToken *close = smith_lang_slice_at(lang, host, first, last + 3);
        bool run = smith_lang_punct(inside, ":") && smith_lang_punct(close, "]");
        bool fixed = inside->kind == SmithTokenKind_Number && smith_lang_punct(close, "]");
        if (dims->count >= smith_lang_slice_dims_max) {
            *problem = "a run-time array has at most 8 dimensions";
            *where = open->location;
        } else if (run || fixed) {
            char digits[32];
            snprintf(digits, sizeof digits, "%.*s", (int)(inside->length < 31 ? inside->length : 31), inside->text);
            dims->runtime[dims->count] = run;
            dims->fixed[dims->count] = run ? 0 : strtoull(digits, nullptr, 0);
            dims->count += 1;
            last += 3;
            continue;
        } else {
            *problem = "a dimension of a run-time array is `[:]` or a number: `f32 xs[:][2]`";
            *where = open->location;
        }
        for (size_t guard = 0; guard < 32; guard++) { // to its `]`
            last += 1;
            const SmithToken *next = smith_lang_slice_at(lang, host, first, last);
            if (smith_lang_punct(next, "]") || next->kind == SmithTokenKind_Eof) break;
        }
    }
    return last;
}

// At the first token of a declaration (after `;`, `{`, `}`, `(`, `,` or `:`): if it declares a slice, its tokens up to
// `]` become the struct, defined where it is written, and the name. True when the token was replaced.
static bool smith_lang_slice_begin_array(SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    size_t name = 0;
    size_t close = 0;
    SmithLangSliceQuals quals = {};
    if (!smith_lang_slice_declaration(lang, host, first, &name, &close, &quals)) return false;
    static SmithToken span[smith_lang_slice_scan_max + 4];
    for (size_t at = 0; at <= close; at++) // up to the `]`
        span[at] = *smith_lang_slice_at(lang, host, first, at);
    size_t leading = smith_lang_slice_leading(span, name);
    char tag[smith_lang_tuple_name_max];
    const char *problem = nullptr;
    uint32_t where = span[name].location;
    static SmithToken made[smith_lang_slice_scan_max + 40];
    size_t used = 0;
    for (size_t at = 0; at < leading; at++)
        made[used++] = span[at];
    SmithLangSliceDims dims = smith_lang_slice_dims_one();
    dims.restrict_data = quals.is_restrict;
    const char *dims_problem = nullptr;
    uint32_t dims_where = 0;
    size_t end = smith_lang_slice_groups(lang, host, first, close, &dims, &dims_problem, &dims_where);
    if (dims_problem != nullptr) smith_lang_error(lang, host, dims_where, "%s", dims_problem);
    if (quals.bad != nullptr) smith_lang_error(lang, host, quals.bad_where, "%s", quals.bad);
    bool good = smith_lang_slice_tag_of(lang, span + leading, name - leading, &dims, tag, sizeof tag, &problem, &where);
    if (good) {
        used = smith_lang_slice_define(lang, host, made, used, sizeof made / sizeof made[0], tag, span + leading,
                                       name - leading, &dims, &span[leading]);
    } else {
        smith_lang_error(lang, host, where, "%s", problem);
        made[used++] = smith_lang_tuple_made(SmithTokenKind_Identifier, "int", 3, &span[leading], true);
    }
    if (quals.is_const) { // `[const :]`: the slice itself, its pointer and count, is const
        memmove(&made[leading + 1], &made[leading], (used - leading) * sizeof made[0]);
        made[leading] = smith_lang_tuple_made(SmithTokenKind_Identifier, "const", 5, &span[leading], leading > 0);
        used += 1;
        if (used > leading + 1) made[leading + 1].space_before = true;
    }
    made[used++] = span[name];
    if (!smith_lang_tuple_replace(lang, end, made, used)) return false;
    lang->slice.skip = span[name].location; // the name just declared starts no chain
    smith_lang_slice_flag(lang);
    snprintf(lang->slice.armed, sizeof lang->slice.armed, "%.*s", (int)span[name].length, span[name].text);
    snprintf(lang->slice.armed_tag, sizeof lang->slice.armed_tag, "%s", good ? tag : "");
    return true;
}

// A pointer to a slice, C's pointer to an array: `const char (*text)[:]`. The tokens of the declaration, counted from
// `first` as 0: the element's, `(`, the stars and qualifiers of the pointer, the name, `)` and `[ : ]`. Gives the index
// of the `(` (`open`), of the name and of the `]`.
static bool smith_lang_slice_pointer_declaration(SmithLang *lang, const SmithHost *host, const SmithToken *first,
                                                 size_t *open, size_t *name, size_t *close) {
    if (first->kind != SmithTokenKind_Identifier || smith_lang_slice_statement_word(first)) return false;
    size_t at = 1;
    for (; at < smith_lang_slice_scan_max; at++) { // the element: types, stars and `::`
        const SmithToken *token = smith_lang_slice_at(lang, host, first, at);
        if (token->kind == SmithTokenKind_Eof || token->foreign) return false;
        if (smith_lang_punct(token, "(")) break;
        if (at == 1 && smith_lang_punct(token, "::")) {
            const SmithLangBinding *named =
                lang->bindings_count > 0 ? smith_lang_binding_find(lang, first->text, first->length) : nullptr;
            if (named != nullptr && named->is_c) return false; // the name after `c::` must reach the stream as written
        }
        if (!(token->kind == SmithTokenKind_Identifier || smith_lang_punct(token, "*") ||
              smith_lang_punct(token, "::"))) {
            return false;
        }
    }
    if (at >= smith_lang_slice_scan_max - 8) return false;
    size_t stars = 0;
    size_t last = at + 1;
    for (; last < at + 8; last++) { // the pointer: stars, qualifiers, then the name
        const SmithToken *token = smith_lang_slice_at(lang, host, first, last);
        if (smith_lang_punct(token, "*")) {
            stars += 1;
        } else if (token->kind != SmithTokenKind_Identifier || stars == 0) {
            return false;
        } else if (!smith_lang_slice_qualifier(token)) {
            break;
        }
    }
    if (last >= at + 8 || !smith_lang_punct(smith_lang_slice_at(lang, host, first, last + 1), ")") ||
        !smith_lang_punct(smith_lang_slice_at(lang, host, first, last + 2), "[") ||
        smith_lang_slice_bracket(lang, host, first, last + 2, nullptr) != 0) {
        return false;
    }
    *open = at;
    *name = last;
    *close = last + 4;
    return true;
}

// At the first token of `T (*name)[:]`: the slice's struct, then the pointer's own tokens and the name, as
// `struct tag *name` would be written. True when the token was replaced.
static bool smith_lang_slice_pointer_begin(SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    size_t open = 0;
    size_t name = 0;
    size_t close = 0;
    if (!smith_lang_slice_pointer_declaration(lang, host, first, &open, &name, &close)) return false;
    static SmithToken span[smith_lang_slice_scan_max + 4];
    for (size_t at = 0; at <= close; at++) // up to the `]`
        span[at] = *smith_lang_slice_at(lang, host, first, at);
    size_t leading = smith_lang_slice_leading(span, open);
    SmithLangSliceDims dims = smith_lang_slice_dims_one();
    const char *dims_problem = nullptr;
    uint32_t dims_where = 0;
    size_t end = smith_lang_slice_groups(lang, host, first, close, &dims, &dims_problem, &dims_where);
    if (dims_problem != nullptr) smith_lang_error(lang, host, dims_where, "%s", dims_problem);
    char tag[smith_lang_tuple_name_max];
    const char *problem = nullptr;
    uint32_t where = span[open].location;
    if (!smith_lang_slice_tag_of(lang, span + leading, open - leading, &dims, tag, sizeof tag, &problem, &where)) {
        smith_lang_error(lang, host, where, "%s", problem);
        return false;
    }
    static SmithToken made[smith_lang_slice_scan_max + 40];
    size_t used = 0;
    for (size_t at = 0; at < leading; at++)
        made[used++] = span[at];
    used = smith_lang_slice_define(lang, host, made, used, sizeof made / sizeof made[0], tag, span + leading,
                                   open - leading, &dims, &span[leading]);
    for (size_t at = open + 1; at <= name; at++) { // the stars, their qualifiers and the name
        made[used] = span[at];
        made[used++].space_before = at == open + 1 || !smith_lang_punct(&span[at - 1], "*");
    }
    if (!smith_lang_tuple_replace(lang, end, made, used)) return false;
    lang->slice.skip = span[name].location;
    snprintf(lang->slice.armed, sizeof lang->slice.armed, "%.*s", (int)span[name].length, span[name].text);
    snprintf(lang->slice.armed_tag, sizeof lang->slice.armed_tag, "*%s", tag); // its `=` may add a qualifier
    smith_lang_slice_flag(lang);
    return true;
}

// At the first token of a declaration: `T xs[:]` or `T (*xs)[:]`.
static bool smith_lang_slice_begin(SmithLang *lang, const SmithHost *host, const SmithToken *first) {
    return smith_lang_slice_begin_array(lang, host, first) || smith_lang_slice_pointer_begin(lang, host, first);
}

// `typeof(T[:])`: a slice's struct as a type, the result of a function or the type of a local.
static bool smith_lang_slice_typeof(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (!smith_lang_punct(smith_lang_peek(lang, host, 0, false), "(")) return false;
    size_t close = 0;
    for (size_t at = 1; at < smith_lang_slice_scan_max && close == 0; at++) { // the type is a few tokens
        const SmithToken *next = smith_lang_peek(lang, host, at, false);
        if (next->kind == SmithTokenKind_Eof || next->foreign) return false;
        if (smith_lang_punct(next, "[") && smith_lang_punct(smith_lang_peek(lang, host, at + 1, false), ":") &&
            smith_lang_punct(smith_lang_peek(lang, host, at + 2, false), "]")) {
            close = at;
        } else if (!(next->kind == SmithTokenKind_Identifier || smith_lang_punct(next, "*") ||
                     smith_lang_punct(next, "::"))) {
            return false;
        }
    }
    if (close < 2) return false;
    SmithLangSliceDims dims = smith_lang_slice_dims_one();
    const char *dims_problem = nullptr;
    uint32_t dims_where = 0;
    size_t last = smith_lang_slice_groups(lang, host, token, close + 3, &dims, &dims_problem, &dims_where);
    if (!smith_lang_punct(smith_lang_peek(lang, host, last, false), ")")) return false;
    if (dims_problem != nullptr) smith_lang_error(lang, host, dims_where, "%s", dims_problem);
    if (dims.count > 1 && lang->decl.generic) {
        smith_lang_error(lang, host, token->location,
                         "a generic's result of more than one dimension is not built yet: `typeof(T[:])`");
        return false;
    }
    static SmithToken span[smith_lang_slice_scan_max + 4];
    for (size_t at = 0; at <= close + 3; at++) // `(` to `)`
        span[at] = *smith_lang_peek(lang, host, at, false);
    char tag[smith_lang_tuple_name_max];
    const char *problem = nullptr;
    uint32_t where = span[1].location;
    static SmithToken made[smith_lang_slice_scan_max + 40];
    if (!smith_lang_slice_tag_of(lang, span + 1, close - 1, &dims, tag, sizeof tag, &problem, &where)) {
        smith_lang_error(lang, host, where, "%s", problem);
        return false;
    }
    size_t used = smith_lang_slice_define(lang, host, made, 0, sizeof made / sizeof made[0], tag, span + 1, close - 1,
                                          &dims, token);
    made[0].space_before = token->space_before;
    made[0].line_start = token->line_start;
    snprintf(lang->slice.typeof_tag, sizeof lang->slice.typeof_tag, "%s", tag);
    if (!smith_lang_tuple_replace(lang, last + 1, made, used)) return false;
    smith_lang_slice_flag(lang);
    return true; // the `typeof` itself is dropped
}

// Whether a token begins a declaration or ends one: where a slice's type may start.
static bool smith_lang_slice_boundary(const SmithToken *token) {
    return token->kind == SmithTokenKind_Punctuator && token->length == 1 &&
           strchr(";{}(,:", token->text[0]) != nullptr;
}

// ---- What the compiler says of an expression

typedef enum {
    SmithLangSlice_None,
    SmithLangSlice_Slice,
    SmithLangSlice_Pointer,
    SmithLangSlice_Array
} SmithLangSliceKind;

typedef struct {
    SmithLangSliceKind kind;
    char tag[smith_lang_tuple_name_max];   // a slice: its struct
    char plain[smith_lang_tuple_name_max]; // a slice: its struct without `restrict` (what a cut or a row makes)
    char stem[smith_lang_tuple_name_max];  // a slice: its tag without the dimensions
    SmithLangSliceDims dims;               // a slice: its dimensions
    char element[256];                     // a pointer or an array: the element as the parser writes it
    uint64_t count;                        // an array: how many
    uint32_t rank;                         // an array: its dimensions, `array_dims` their sizes
    uint64_t array_dims[smith_lang_slice_dims_max];
} SmithLangSliceType;

// The element of `int32_t *` or `float[4]`: what stands before the last `*` or `[`.
static bool smith_lang_slice_element(const char *written, const char *canonical, char *out, size_t capacity) {
    const char *source = written[0] != 0 ? written : canonical;
    snprintf(out, capacity, "%s", source);
    char *cut = strrchr(out, '*');
    char *bracket = strrchr(out, '[');
    if (strchr(out, '(') != nullptr) return false; // a function pointer
    if (cut != nullptr && (bracket == nullptr || cut > bracket)) {
        *cut = 0;
    } else if (bracket != nullptr) {
        *bracket = 0;
    }
    size_t length = strlen(out);
    while (length > 0 && out[length - 1] == ' ')
        out[--length] = 0;
    return length > 0;
}

// A slice type from its tag: the dimensions, the tag a cut or a row of it has (without `restrict`) and its stem.
static void smith_lang_slice_type_tag(SmithLangSliceType *out, const char *tag) {
    snprintf(out->tag, sizeof out->tag, "%s", tag);
    size_t stem = 0;
    if (!smith_lang_slice_dims_parse(out->tag, &out->dims, &stem)) out->dims = smith_lang_slice_dims_one();
    snprintf(out->plain, sizeof out->plain, "%s", out->tag);
    if (out->dims.restrict_data) { // `smith_array_restrict_u8_n` is a `smith_array_u8_n` that was passed on
        snprintf(out->plain, sizeof out->plain, "%s%s", smith_lang_slice_prefix,
                 out->tag + sizeof smith_lang_slice_prefix - 1 + 9);
    }
    SmithLangSliceDims plain = {};
    if (!smith_lang_slice_dims_parse(out->plain, &plain, &stem)) stem = strlen(out->plain);
    snprintf(out->stem, sizeof out->stem, "%.*s", (int)stem, out->plain);
}

static bool smith_lang_slice_type(const SmithHost *host, const char *text, SmithLangSliceType *out) {
    *out = (SmithLangSliceType){};
    SmithExprType found = {};
    if (host->expr_type == nullptr || !host->expr_type(host->context, text, (uint32_t)strlen(text), &found)) {
        return false;
    }
    const char *canonical = found.canonical;
    const char *tag = strstr(canonical, "struct smith_array_");
    size_t length = strlen(canonical);
    while (length > 0 && canonical[length - 1] == ' ')
        length -= 1;
    if (tag != nullptr && strchr(canonical, '*') == nullptr && strchr(canonical, '[') == nullptr &&
        strchr(canonical, '(') == nullptr) {
        tag += 7;
        char name[smith_lang_tuple_name_max];
        snprintf(name, sizeof name, "%.*s", (int)strcspn(tag, " "), tag);
        out->kind = SmithLangSlice_Slice;
        smith_lang_slice_type_tag(out, name);
        return true;
    }
    if (found.is_array && found.array_rank >= 2 && found.array_rank <= smith_lang_slice_dims_max) {
        out->kind = SmithLangSlice_Array; // `float[4][2]`: canonical says a pointer to rows, the host the dimensions
        out->count = found.array_size;
        out->rank = found.array_rank;
        memcpy(out->array_dims, found.array_dims, sizeof out->array_dims);
        snprintf(out->element, sizeof out->element, "%s", found.array_element);
        return true;
    }
    if (length > 0 && canonical[length - 1] == '*' && strchr(canonical, '(') == nullptr) {
        out->kind = found.is_array ? SmithLangSlice_Array : SmithLangSlice_Pointer;
        out->count = found.array_size;
        out->rank = found.is_array ? 1 : 0;
        if (found.is_array) out->array_dims[0] = found.array_size;
    } else {
        return true;
    }
    if (!smith_lang_slice_element(found.written, canonical, out->element, sizeof out->element)) {
        out->kind = SmithLangSlice_None;
    }
    return true;
}

// The tokens of a C type written as the parser does (`const char`, `struct Foo`): words and stars.
static size_t smith_lang_slice_words(SmithToken *out, size_t capacity, const char *type, const SmithToken *like) {
    size_t used = 0;
    const char *at = type;
    while (*at != 0 && used < capacity) {
        while (*at == ' ')
            at += 1;
        const char *end = at;
        if (*at == '*') {
            end += 1;
        } else {
            while (*end != 0 && *end != ' ' && *end != '*')
                end += 1;
        }
        if (end == at) break;
        out[used++] = smith_lang_tuple_made(smith_lang_slice_kind(at), at, (size_t)(end - at), like, true);
        at = end;
    }
    if (used > 0) out[0].space_before = false;
    return used;
}

// ---- The text of tokens for the compiler

// The tokens as one expression for `expr_type`: a module's names as the lowering writes them, the tokens it made as
// they are.
static bool smith_lang_slice_text(SmithLang *lang, const SmithToken *tokens, size_t count, char *out, size_t capacity) {
    size_t used = 0;
    for (size_t at = 0; at < count; at++) { // each token
        const SmithToken *next = &tokens[at];
        char text[smith_lang_name_max];
        size_t taken = 1;
        if (used + sizeof text + 8 > capacity) return false;
        bool member = at > 0 && (smith_lang_punct(&tokens[at - 1], ".") || smith_lang_punct(&tokens[at - 1], "->"));
        if (next->kind == SmithTokenKind_Identifier && !next->synthetic && !member &&
            smith_lang_word(next->text, next->length) == SmithLangWord_Other &&
            !(lang->in_function && smith_lang_is_param(lang, next->text, next->length))) {
            taken = smith_lang_tuple_resolve(lang, tokens, at, count, text, sizeof text);
        } else {
            snprintf(text, sizeof text, "%.*s", (int)(next->length < sizeof text ? next->length : sizeof text - 1),
                     next->text);
        }
        used += (size_t)snprintf(out + used, capacity - used, "%s%s", used > 0 ? "\x1f" : "", text);
        at += taken - 1;
    }
    return count > 0;
}

// ---- The check an index or a slice makes

static bool smith_lang_slice_assertions(const SmithHost *host) {
    int64_t value = 1;
    if (host->target_fact != nullptr && host->target_fact(host->context, SmithTarget_Assertions, &value)) {
        return value != 0;
    }
    return true;
}

// The statement that fails the program unless `condition` holds (nothing where assertions are off): smith_failure
// (<smith/trace.h>) with the kind of an assert, 4, as `contract::assert` fails, so the report is the same and its line
// is the line of the index. The function is declared here, so that the file needs no import; the build links it with
// the trace library.
static void smith_lang_slice_check(const SmithHost *host, const char *condition, char *out, size_t capacity) {
    if (!smith_lang_slice_assertions(host)) {
        out[0] = 0;
        return;
    }
    snprintf(out, capacity, "extern void smith_failure ( unsigned char ) ; if ( ! ( %s ) ) smith_failure ( 4 ) ;",
             condition);
}

// ---- A postfix chain

typedef enum {
    SmithLangSliceElement_Member,
    SmithLangSliceElement_Call,
    SmithLangSliceElement_Subscript
} SmithLangSliceElementKind;

typedef struct {
    SmithLangSliceElementKind kind;
    size_t from; // [from, to) in the chain: `.name`, `( ... )` or `[ ... ]`
    size_t to;
} SmithLangSliceElement;

typedef struct {
    SmithToken tokens[smith_lang_slice_chain_max];
    size_t count;
    SmithLangSliceElement elements[smith_lang_slice_elements_max];
    size_t element_count;
    size_t head_end; // the tokens of the name (or the parenthesized head)
    bool assigned;   // an `=` follows the chain
    bool c_head;     // the chain is a name written `c::name`
    bool subscripts;
    bool calls;
} SmithLangSliceChain;

// What the lowering made of a chain, being written.
typedef struct {
    SmithToken tokens[smith_lang_slice_cur_max];
    size_t count;
    bool changed;
    bool overflow;
    bool sliced; // the last subscript was a slice (`xs[a:b]`)
} SmithLangSliceCur;

static SmithLangSliceChain smith_lang_slice_chain_state;
static SmithLangSliceCur smith_lang_slice_cur;

static void smith_lang_slice_add(SmithLangSliceCur *cur, const SmithToken *tokens, size_t count) {
    if (cur->count + count > smith_lang_slice_cur_max) {
        cur->overflow = true;
        return;
    }
    for (size_t i = 0; i < count; i++) // the tokens
        cur->tokens[cur->count++] = tokens[i];
}

// The tokens from the opening bracket at `*at` of the queue to its match, into the chain.
static bool smith_lang_slice_group(SmithLang *lang, const SmithHost *host, SmithLangSliceChain *chain, size_t *at,
                                   int depth) {
    for (size_t guard = 0; guard < smith_lang_slice_chain_max; guard++) { // a chain is a few tokens
        const SmithToken *next = smith_lang_peek(lang, host, *at, false);
        if (next->kind == SmithTokenKind_Eof || next->foreign || chain->count >= smith_lang_slice_chain_max)
            return false;
        if (smith_lang_punct(next, "(") || smith_lang_punct(next, "[") || smith_lang_punct(next, "{")) depth += 1;
        if (smith_lang_punct(next, ")") || smith_lang_punct(next, "]") || smith_lang_punct(next, "}")) depth -= 1;
        chain->tokens[chain->count++] = *next;
        *at += 1;
        if (depth == 0) return true;
    }
    return false;
}

static bool smith_lang_slice_more_element(SmithLang *lang, const SmithHost *host, SmithLangSliceChain *chain,
                                          size_t *at) {
    const SmithToken *next = smith_lang_peek(lang, host, *at, false);
    if (chain->element_count >= smith_lang_slice_elements_max) return false;
    SmithLangSliceElement *element = &chain->elements[chain->element_count];
    element->from = chain->count;
    if (smith_lang_punct(next, ".") || smith_lang_punct(next, "->")) {
        const SmithToken *name = smith_lang_peek(lang, host, *at + 1, false);
        if (name->kind != SmithTokenKind_Identifier || name->foreign || chain->count + 2 > smith_lang_slice_chain_max) {
            return false;
        }
        chain->tokens[chain->count++] = *next;
        chain->tokens[chain->count++] = *name;
        *at += 2;
        element->kind = SmithLangSliceElement_Member;
    } else if (smith_lang_punct(next, "[") || smith_lang_punct(next, "(")) {
        bool subscript = smith_lang_punct(next, "[");
        if (subscript && smith_lang_punct(smith_lang_peek(lang, host, *at + 1, false), "[")) return false; // [[
        if (!smith_lang_slice_group(lang, host, chain, at, 0)) return false;
        element->kind = subscript ? SmithLangSliceElement_Subscript : SmithLangSliceElement_Call;
        chain->subscripts = chain->subscripts || subscript;
        chain->calls = chain->calls || (!subscript && chain->count - element->from > 2);
    } else {
        return false;
    }
    element->to = chain->count;
    chain->element_count += 1;
    return true;
}

// The chain that begins with `token`: a name (`a::b`) or a parenthesized expression, then members, calls and
// subscripts. False when it is not one, or too long.
static bool smith_lang_slice_read(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                  SmithLangSliceChain *chain) {
    chain->tokens[0] = *token;
    chain->count = 1;
    chain->element_count = 0;
    chain->subscripts = chain->calls = chain->c_head = false;
    size_t at = 0;
    if (smith_lang_punct(token, "(")) {
        if (!smith_lang_slice_group(lang, host, chain, &at, 1)) return false;
        const SmithToken *after = smith_lang_peek(lang, host, at, false);
        if (!smith_lang_punct(after, ".") && !smith_lang_punct(after, "->") && !smith_lang_punct(after, "[")) {
            return false;
        }
    } else {
        const SmithLangBinding *named =
            lang->bindings_count > 0 ? smith_lang_binding_find(lang, token->text, token->length) : nullptr;
        if (named != nullptr && named->is_c && smith_lang_punct(smith_lang_peek(lang, host, 0, false), "::")) {
            smith_lang_peek(lang, host, 1, true); // a macro after `c::` reaches the stream as written
        }
        while (smith_lang_punct(smith_lang_peek(lang, host, at, false), "::") &&
               smith_lang_peek(lang, host, at + 1, false)->kind == SmithTokenKind_Identifier &&
               chain->count + 2 <= smith_lang_slice_chain_max) { // a qualified name
            chain->tokens[chain->count++] = *smith_lang_peek(lang, host, at, false);
            chain->tokens[chain->count++] = *smith_lang_peek(lang, host, at + 1, false);
            at += 2;
        }
        const SmithLangBinding *binding = chain->count == 3 && lang->bindings_count > 0
                                              ? smith_lang_binding_find(lang, token->text, token->length)
                                              : nullptr;
        chain->c_head = binding != nullptr && binding->is_c;
    }
    chain->head_end = chain->count;
    while (smith_lang_slice_more_element(lang, host, chain, &at)) { // each member, call and subscript
    }
    chain->assigned = smith_lang_punct(smith_lang_peek(lang, host, at, false), "=");
    return true;
}

// Whether the token before the one being lowered can end an operand: then this token continues an expression.
static bool smith_lang_slice_continues(SmithLang *lang, const SmithHost *host) {
    SmithToken before;
    if (!smith_lang_tuple_previous(lang, host, 1, &before)) return false;
    if (before.kind == SmithTokenKind_Punctuator) {
        return before.length == 0 || smith_lang_punct(&before, ")") || smith_lang_punct(&before, "]") ||
               smith_lang_punct(&before, ".") || smith_lang_punct(&before, "->") || smith_lang_punct(&before, "::");
    }
    if (before.kind != SmithTokenKind_Identifier) return true; // a literal
    return smith_lang_word(before.text, before.length) != SmithLangWord_Keyword;
}

// ---- A subscript of a slice

static size_t smith_lang_slice_index(SmithLangSliceCur *cur, const SmithHost *host, const SmithLangSliceSpan *spans,
                                     const SmithToken *like) {
    char check[512];
    char pattern[1024];
    smith_lang_slice_check(host, "smith_i < smith_s . count [ 0 ]", check, sizeof check);
    snprintf(pattern, sizeof pattern,
             "( * __extension__ ( { auto smith_s = ( @0 ) ; __SIZE_TYPE__ smith_i = ( @1 ) ; %s & smith_s . data [ "
             "smith_i ] ; } ) )",
             check);
    cur->count = smith_lang_slice_pattern(cur->tokens, 0, smith_lang_slice_cur_max, pattern, spans, like);
    return cur->count;
}

// A slice of a slice: a view that shares the elements.
static size_t smith_lang_slice_view(SmithLangSliceCur *cur, const SmithHost *host, const SmithLangSliceSpan *spans,
                                    const char *tag, const SmithToken *like) {
    char check[512];
    char pattern[1536];
    smith_lang_slice_check(host, "smith_a <= smith_b && smith_b <= smith_s . count [ 0 ]", check, sizeof check);
    snprintf(
        pattern, sizeof pattern,
        "__extension__ ( { auto smith_s = ( @0 ) ; __SIZE_TYPE__ smith_a = ( @1 ) ; __SIZE_TYPE__ smith_b = ( @2 ) ; "
        "%s "
        "( struct %s ) { . data = smith_a == 0 ? smith_s . data : smith_s . data + smith_a , . count = { smith_b - "
        "smith_a } } ; } )",
        check, tag);
    cur->count = smith_lang_slice_pattern(cur->tokens, 0, smith_lang_slice_cur_max, pattern, spans, like);
    return cur->count;
}

// A slice of a pointer or of a fixed array: the extent is the program's word for a pointer, the array's size for an
// array.
static bool smith_lang_slice_from_pointer(SmithLang *lang, SmithLangSliceCur *cur, const SmithHost *host,
                                          const SmithLangSliceSpan *spans, const SmithLangSliceType *type,
                                          const SmithToken *like, const char **problem, uint32_t *where) {
    static SmithToken element[48];
    size_t words = smith_lang_slice_words(element, sizeof element / sizeof element[0], type->element, like);
    char tag[smith_lang_tuple_name_max];
    SmithLangSliceDims one = smith_lang_slice_dims_one();
    if (!smith_lang_slice_tag_of(lang, element, words, &one, tag, sizeof tag, problem, where)) return false;
    SmithLangSliceSpan all[4] = {spans[0], spans[1], spans[2], {element, words}};
    char condition[160] = "smith_a <= smith_b";
    if (type->kind == SmithLangSlice_Array) {
        snprintf(condition, sizeof condition, "smith_a <= smith_b && smith_b <= %llu", (unsigned long long)type->count);
    }
    char check[512];
    char pattern[1536];
    smith_lang_slice_check(host, condition, check, sizeof check);
    snprintf(pattern, sizeof pattern,
             "__extension__ ( { auto smith_p = ( @0 ) ; __SIZE_TYPE__ smith_a = ( @1 ) ; __SIZE_TYPE__ smith_b = ( @2 "
             ") ; %s struct %s { "
             "@3 * data ; __SIZE_TYPE__ count [ 1 ] ; } ; ( struct %s ) { . data = smith_a == 0 ? smith_p : smith_p + "
             "smith_a , . "
             "count = { smith_b - smith_a } } ; } )",
             check, tag, tag);
    cur->count = smith_lang_slice_pattern(cur->tokens, 0, smith_lang_slice_cur_max, pattern, all, like);
    return true;
}

// ---- A subscript of a slice of more dimensions

// The size of dimension `at` as an expression over `smith_s`: its count or its number.
static void smith_lang_slice_dim_text(const SmithLangSliceDims *dims, size_t at, char *out, size_t capacity) {
    if (!dims->runtime[at]) {
        snprintf(out, capacity, "%llu", (unsigned long long)dims->fixed[at]);
        return;
    }
    size_t which = 0;
    for (size_t before = 0; before < at; before++) // at most 8
        which += dims->runtime[before] ? 1 : 0;
    snprintf(out, capacity, "smith_s . count [ %zu ]", which);
}

// The product of the sizes of the dimensions `from` to the last, as an expression: the elements one index step skips.
static void smith_lang_slice_stride_text(const SmithLangSliceDims *dims, size_t from, char *out, size_t capacity) {
    size_t used = 0;
    out[0] = 0;
    for (size_t at = from; at < dims->count && used + 48 < capacity; at++) { // at most 8
        char one[48];
        smith_lang_slice_dim_text(dims, at, one, sizeof one);
        used += (size_t)snprintf(out + used, capacity - used, "%s%s", at > from ? " * " : "", one);
    }
}

// `xs[i][j]` of a slice of `dims` in one step, `m` indexes in spans 1 to m (the slice's expression is span 0): the
// element when they name every dimension, else the row they name, a slice of the dimensions that are left (the first
// of them run-time: a row of fixed size is a `[:]`). Row-major, one contiguous block: no strides.
static bool smith_lang_slice_rows(SmithLangSliceCur *cur, const SmithHost *host, const SmithLangSliceSpan *spans,
                                  size_t m, const SmithLangSliceType *type, const SmithToken *like) {
    static const char *const names[smith_lang_slice_dims_max] = {"smith_i", "smith_j", "smith_k", "smith_l",
                                                                 "smith_m", "smith_n", "smith_o", "smith_q"};
    const SmithLangSliceDims *dims = &type->dims;
    char declarations[512] = "";
    char condition[512] = "";
    char offset[512];
    size_t used = 0;
    size_t on = 0;
    for (size_t at = 0; at < m; at++) { // each index, checked against its dimension
        char size[48];
        smith_lang_slice_dim_text(dims, at, size, sizeof size);
        used += (size_t)snprintf(declarations + used, sizeof declarations - used, "__SIZE_TYPE__ %s = ( @%zu ) ; ",
                                 names[at], at + 1);
        on +=
            (size_t)snprintf(condition + on, sizeof condition - on, "%s%s < %s", at > 0 ? " && " : "", names[at], size);
        if (at == 0) {
            snprintf(offset, sizeof offset, "%s", names[0]);
        } else {
            char next[512];
            snprintf(next, sizeof next, "( %s ) * %s + %s", offset, size, names[at]);
            snprintf(offset, sizeof offset, "%s", next);
        }
    }
    char check[1024];
    char pattern[4096];
    smith_lang_slice_check(host, condition, check, sizeof check);
    if (m == dims->count) {
        snprintf(pattern, sizeof pattern,
                 "( * __extension__ ( { auto smith_s = ( @0 ) ; %s%s & smith_s . data [ %s ] ; } ) )", declarations,
                 check, offset);
        cur->count = smith_lang_slice_pattern(cur->tokens, 0, smith_lang_slice_cur_max, pattern, spans, like);
        return true;
    }
    SmithLangSliceDims row = {.count = dims->count - m};
    row.runtime[0] = true;
    for (size_t at = m + 1; at < dims->count; at++) { // the dimensions after the first of the row
        row.runtime[at - m] = dims->runtime[at];
        row.fixed[at - m] = dims->fixed[at];
    }
    char suffix[64];
    smith_lang_slice_dims_suffix(&row, suffix, sizeof suffix);
    char stride[512];
    smith_lang_slice_stride_text(dims, m, stride, sizeof stride);
    char counts[512] = "";
    size_t shown = 0;
    for (size_t at = m; at < dims->count; at++) { // the sizes of the row's run-time dimensions
        if (at > m && !dims->runtime[at]) continue;
        char size[48];
        smith_lang_slice_dim_text(dims, at, size, sizeof size);
        shown += (size_t)snprintf(counts + shown, sizeof counts - shown, "%s%s", shown > 0 ? " , " : "", size);
    }
    snprintf(pattern, sizeof pattern,
             "( __extension__ ( { auto smith_s = ( @0 ) ; %s%s struct %s%s { __typeof__ ( * smith_s . data ) * data ; "
             "__SIZE_TYPE__ count [ %zu ] ; } ; ( struct %s%s ) { . data = smith_s . data + ( %s ) * ( %s ) , . count "
             "= { %s } } ; } ) )",
             declarations, check, type->stem, suffix, smith_lang_slice_dims_runtime(&row), type->stem, suffix, offset,
             stride, counts);
    cur->count = smith_lang_slice_pattern(cur->tokens, 0, smith_lang_slice_cur_max, pattern, spans, like);
    return true;
}

// A slice of the first dimension of a slice of more: the rows `a` to `b - 1`, a view of the same shape.
static size_t smith_lang_slice_view_rows(SmithLangSliceCur *cur, const SmithHost *host, const SmithLangSliceSpan *spans,
                                         const SmithLangSliceType *type, const SmithToken *like) {
    char check[512];
    char stride[512];
    char pattern[2048];
    smith_lang_slice_check(host, "smith_a <= smith_b && smith_b <= smith_s . count [ 0 ]", check, sizeof check);
    smith_lang_slice_stride_text(&type->dims, 1, stride, sizeof stride);
    snprintf(
        pattern, sizeof pattern,
        "__extension__ ( { auto smith_s = ( @0 ) ; __SIZE_TYPE__ smith_a = ( @1 ) ; __SIZE_TYPE__ smith_b = ( @2 ) "
        "; %s auto smith_r = smith_s ; smith_r . data = smith_a == 0 ? smith_s . data : smith_s . data + smith_a * "
        "( %s ) ; smith_r . count [ 0 ] = smith_b - smith_a ; smith_r ; } )",
        check, stride);
    cur->count = smith_lang_slice_pattern(cur->tokens, 0, smith_lang_slice_cur_max, pattern, spans, like);
    return cur->count;
}

// ---- Reading a subscript: `[i]`, `[a:b]`, `[:b]`, `[a:]`

typedef struct {
    size_t colon;  // index of the slice's `:` in the inner tokens, or SIZE_MAX for an index
    size_t second; // a second `:` (a step), or SIZE_MAX
} SmithLangSliceShape;

static SmithLangSliceShape smith_lang_slice_shape(const SmithToken *inner, size_t count) {
    SmithLangSliceShape shape = {.colon = SIZE_MAX, .second = SIZE_MAX};
    int depth = 0;
    int conditions = 0;
    for (size_t at = 0; at < count; at++) { // each inner token
        const SmithToken *token = &inner[at];
        if (smith_lang_punct(token, "(") || smith_lang_punct(token, "[") || smith_lang_punct(token, "{")) depth += 1;
        if (smith_lang_punct(token, ")") || smith_lang_punct(token, "]") || smith_lang_punct(token, "}")) depth -= 1;
        if (depth != 0) continue;
        if (smith_lang_punct(token, "?")) conditions += 1;
        if (!smith_lang_punct(token, ":")) continue;
        if (conditions > 0) {
            conditions -= 1;
        } else if (shape.colon == SIZE_MAX) {
            shape.colon = at;
        } else if (shape.second == SIZE_MAX) {
            shape.second = at;
        }
    }
    return shape;
}

static SmithToken smith_lang_slice_number(const char *text, const SmithToken *like) {
    return smith_lang_tuple_made(SmithTokenKind_Number, text, strlen(text), like, false);
}

// A constant index below zero is an error: the last element is `xs[countof(xs) - 1]`.
static void smith_lang_slice_negative(SmithLang *lang, const SmithHost *host, const SmithToken *inner, size_t count) {
    char text[smith_lang_slice_text_max];
    SmithExprType found = {};
    if (count == 0 || host->expr_type == nullptr || !smith_lang_slice_text(lang, inner, count, text, sizeof text) ||
        !host->expr_type(host->context, text, (uint32_t)strlen(text), &found)) {
        return;
    }
    if (found.constant && !found.is_float && found.negative && found.magnitude > 0) {
        smith_lang_error(lang, host, inner[0].location,
                         "an index is never negative: the last element is `xs[countof(xs) - 1]`");
    }
}

// The indexes `xs[i][j]` of a slice of more dimensions that follow each other from the element `index` of the chain,
// written as one step. Gives how many chain elements it took.
static size_t smith_lang_slice_row_run(SmithLang *lang, const SmithHost *host, const SmithLangSliceChain *chain,
                                       size_t index, SmithLangSliceCur *cur, const SmithLangSliceType *type) {
    SmithLangSliceSpan spans[smith_lang_slice_dims_max + 1];
    spans[0] = (SmithLangSliceSpan){cur->tokens, cur->count};
    size_t taken = 0;
    while (index + taken < chain->element_count && taken < type->dims.count) { // each index, up to the dimensions
        const SmithLangSliceElement *next = &chain->elements[index + taken];
        if (next->kind != SmithLangSliceElement_Subscript) break;
        const SmithToken *inner = &chain->tokens[next->from + 1];
        size_t count = next->to - next->from - 2;
        if (smith_lang_slice_shape(inner, count).colon != SIZE_MAX) break; // a slice ends the run
        smith_lang_slice_negative(lang, host, inner, count);
        spans[taken + 1] = (SmithLangSliceSpan){inner, count};
        taken += 1;
    }
    static SmithLangSliceCur out;
    out = (SmithLangSliceCur){};
    smith_lang_slice_rows(&out, host, spans, taken, type, &chain->tokens[chain->elements[index].from]);
    *cur = out;
    cur->changed = true;
    return taken;
}

// `xs[a:b]`: the bounds are span `inner` split at `shape`; `cur` holds the expression before it, `out` the whole after.
static bool smith_lang_slice_cut(SmithLang *lang, const SmithHost *host, const SmithLangSliceSpan *bounds,
                                 SmithLangSliceShape shape, const SmithLangSliceType *type,
                                 const SmithLangSliceCur *cur, SmithLangSliceCur *out, const SmithToken *like) {
    const SmithToken *inner = bounds->tokens;
    SmithToken zero = smith_lang_slice_number("0", like);
    SmithToken whole[8];
    SmithLangSliceSpan whole_span = {whole,
                                     smith_lang_slice_build(whole, 0, 8, "smith_s . count [ 0 ]", nullptr, like)};
    SmithLangSliceSpan low =
        shape.colon == 0 ? (SmithLangSliceSpan){&zero, 1} : (SmithLangSliceSpan){inner, shape.colon};
    size_t stop = shape.second == SIZE_MAX ? bounds->count : shape.second;
    SmithLangSliceSpan high =
        shape.colon + 1 == stop ? whole_span : (SmithLangSliceSpan){inner + shape.colon + 1, stop - shape.colon - 1};
    SmithLangSliceSpan spans[3] = {{cur->tokens, cur->count}, low, high};
    if (type->kind == SmithLangSlice_Slice && type->dims.count > 1) {
        smith_lang_slice_view_rows(out, host, spans, type, like);
    } else if (type->kind == SmithLangSlice_Slice) {
        smith_lang_slice_view(out, host, spans, type->plain, like);
    } else {
        const char *problem = nullptr;
        uint32_t where = like->location;
        if (shape.colon + 1 == stop) {
            smith_lang_error(lang, host, like->location, "slicing a pointer or an array needs both bounds: `p[a:b]`");
            spans[2] = (SmithLangSliceSpan){&zero, 1};
        }
        if (!smith_lang_slice_from_pointer(lang, out, host, spans, type, like, &problem, &where)) {
            smith_lang_error(lang, host, where, "%s", problem);
            return false;
        }
    }
    out->sliced = true;
    return true;
}

// One subscript of the chain: `cur` holds what is before it, and holds the whole afterwards.
static size_t smith_lang_slice_subscript_element(SmithLang *lang, const SmithHost *host,
                                                 const SmithLangSliceChain *chain, size_t index,
                                                 SmithLangSliceCur *cur) {
    const SmithLangSliceElement *element = &chain->elements[index];
    const SmithToken *inner = &chain->tokens[element->from + 1];
    size_t count = element->to - element->from - 2;
    SmithLangSliceShape shape = smith_lang_slice_shape(inner, count);
    char text[smith_lang_slice_text_max];
    SmithLangSliceType type = {};
    bool typed = smith_lang_slice_text(lang, cur->tokens, cur->count, text, sizeof text) &&
                 smith_lang_slice_type(host, text, &type);
    const SmithToken *like = &chain->tokens[element->from];
    bool known =
        typed &&
        (type.kind == SmithLangSlice_Slice ||
         (shape.colon != SIZE_MAX && (type.kind == SmithLangSlice_Pointer || type.kind == SmithLangSlice_Array)));
    if (!known) {
        if (shape.colon != SIZE_MAX) {
            smith_lang_error(lang, host, like->location,
                             "cannot slice this expression: it is not a slice, a pointer or an array");
            SmithToken zero_index[4];
            SmithLangSliceSpan none[1] = {{nullptr, 0}};
            size_t made = smith_lang_slice_build(zero_index, 0, 4, "[ 0 ]", none, like);
            smith_lang_slice_add(cur, zero_index, made); // so that the compiler has nothing more to say of it
            cur->changed = true;
            return 1;
        }
        smith_lang_slice_add(cur, &chain->tokens[element->from], element->to - element->from);
        return 1;
    }
    if (shape.second != SIZE_MAX) {
        smith_lang_error(lang, host, like->location, "a slice has no step: `xs[a:b]` takes the elements a to b - 1");
    }
    if (type.kind == SmithLangSlice_Slice && type.dims.count > 1 && shape.colon == SIZE_MAX) {
        return smith_lang_slice_row_run(lang, host, chain, index, cur, &type);
    }
    if (type.kind == SmithLangSlice_Slice && type.dims.count > 1 && cur->sliced) {
        smith_lang_error(lang, host, like->location,
                         "a slice of rows has no strides: slice each row, `xs[i][a:b]`, not an inner dimension "
                         "across rows, `xs[a:b][c:d]`");
        return 1; // the subscript is dropped: the rows stay
    }
    if (type.kind == SmithLangSlice_Array && type.rank > 1) {
        smith_lang_error(lang, host, like->location,
                         "a fixed array of more than one dimension has no slice: convert it, `f32 xs[:][2] = a;`");
        cur->changed = true;
        return 1; // the subscript is dropped: the array stays
    }
    static SmithLangSliceCur out;
    out = (SmithLangSliceCur){};
    if (shape.colon == SIZE_MAX) {
        smith_lang_slice_negative(lang, host, inner, count);
        SmithLangSliceSpan spans[2] = {{cur->tokens, cur->count}, {inner, count}};
        smith_lang_slice_index(&out, host, spans, like);
    } else if (!smith_lang_slice_cut(lang, host, &(SmithLangSliceSpan){inner, count}, shape, &type, cur, &out, like)) {
        return 1;
    }
    *cur = out;
    cur->changed = true;
    return 1;
}

// ---- Arrays become slices, and slices become C pointers

// The parameters of a function type as the compiler prints it (`int (struct smith_array_i32_n, unsigned long)`): the
// tag of each that is a slice, else an empty string.
static size_t smith_lang_slice_params(const char *canonical, char tags[][smith_lang_tuple_name_max], size_t capacity) {
    size_t length = strlen(canonical);
    if (length == 0 || canonical[length - 1] != ')') return 0;
    size_t open = length - 1;
    int depth = 0;
    for (; open > 0; open--) { // back to the matching parenthesis
        depth += canonical[open] == ')';
        depth -= canonical[open] == '(';
        if (depth == 0) break;
    }
    size_t count = 0;
    size_t start = open + 1;
    depth = 0;
    for (size_t at = start; at < length && count < capacity; at++) { // each parameter
        char c = canonical[at];
        depth += c == '(' || c == '[';
        depth -= c == ')' || c == ']';
        if ((c == ',' && depth == 0) || at == length - 1) {
            const char *param = canonical + start;
            while (*param == ' ')
                param += 1;
            tags[count][0] = 0;
            if (strncmp(param, "struct smith_array_", 19) == 0) {
                size_t length = strcspn(param + 7, ",) ");
                bool pointer = strncmp(param + 7 + length, " *", 2) == 0;
                bool many = pointer && param[9 + length] == '*'; // a pointer to a pointer to a slice: as it is
                // a pointer to a slice is marked by a star before its tag
                if (!many) {
                    snprintf(tags[count], smith_lang_tuple_name_max, "%s%.*s", pointer ? "*" : "", (int)length,
                             param + 7);
                }
            }
            count += 1;
            start = at + 1;
        }
    }
    return count;
}

static bool smith_lang_ty_stem_spelling(const char *stem, size_t length, char *out, size_t capacity);

// The definition of the struct a tag names, `struct tag { E * data ; usize count [ k ] ; } ;`, when no declaration at
// file scope defined it (a function whose only mention is in its parameters defines it in the prototype's scope, and a
// conversion at a call needs the complete type): written inside the conversion, where C23 makes it the same type. Empty
// when the tag is defined, or says too little of its element (a hash).
static void smith_lang_slice_definition_text(const SmithLang *lang, const char *tag, char *out, size_t capacity) {
    out[0] = 0;
    SmithLangSliceDims dims;
    size_t stem = 0;
    size_t prefix = sizeof smith_lang_slice_prefix - 1;
    if (smith_lang_slice_defined(lang, tag) || !smith_lang_slice_dims_parse(tag, &dims, &stem) || stem <= prefix) {
        return;
    }
    if (dims.restrict_data) prefix += 9;
    char element[256];
    if (stem <= prefix || !smith_lang_ty_stem_spelling(tag + prefix, stem - prefix, element, sizeof element)) return;
    snprintf(out, capacity, "struct %s { %s * %sdata ; __SIZE_TYPE__ count [ %zu ] ; } ;", tag, element,
             dims.restrict_data ? "__restrict " : "", smith_lang_slice_dims_runtime(&dims));
}

// `( struct tag ) { . data = ( expr ) , . count = { n } }`
static void smith_lang_slice_wrap(const SmithLang *lang, SmithLangSliceCur *cur, const char *tag,
                                  const SmithToken *expr, size_t count, unsigned long long n, const SmithToken *like) {
    char pattern[smith_lang_tuple_name_max + 400];
    char definition[smith_lang_tuple_name_max + 200];
    smith_lang_slice_definition_text(lang, tag, definition, sizeof definition);
    if (definition[0] != 0) {
        snprintf(pattern, sizeof pattern,
                 "__extension__ ( { %s ( struct %s ) { . data = ( @0 ) , . count = { %llu } } ; } )", definition, tag,
                 n);
    } else {
        snprintf(pattern, sizeof pattern, "( struct %s ) { . data = ( @0 ) , . count = { %llu } }", tag, n);
    }
    SmithLangSliceSpan spans[1] = {{expr, count}};
    cur->count = smith_lang_slice_pattern(cur->tokens, cur->count, smith_lang_slice_cur_max, pattern, spans, like);
}

static bool smith_lang_slice_is_string(const SmithToken *expr, size_t count) {
    for (size_t i = 0; i < count; i++) { // each token
        if (expr[i].kind != SmithTokenKind_String) return false;
    }
    return count > 0;
}

// A fixed array of more dimensions where a slice of more is wanted: `f32[4][2]` is an `f32[:][2]` of 4 rows. A `[:]`
// takes the array's size, a number must be the array's.
static void smith_lang_slice_convert_rows(SmithLang *lang, const SmithHost *host, const char *tag,
                                          const SmithLangSliceDims *dims, const SmithLangSliceType *type,
                                          const SmithToken *expr, size_t count, SmithLangSliceCur *cur) {
    bool same = type->rank == dims->count;
    for (size_t at = 0; same && at < dims->count; at++) { // each dimension
        same = dims->runtime[at] || dims->fixed[at] == type->array_dims[at];
    }
    if (!same) {
        smith_lang_error(lang, host, expr[0].location,
                         "the array has not the shape of the slice: a `[:]` takes any size, a number must be the "
                         "array's own (an `f32[4][2]` is an `f32[:][2]`)");
        char empty[smith_lang_tuple_name_max + 24];
        snprintf(empty, sizeof empty, "( struct %s ) { }", tag); // a value of the type, so that nothing else is said
        cur->count = smith_lang_slice_pattern(cur->tokens, cur->count, smith_lang_slice_cur_max, empty, nullptr, expr);
        cur->changed = true;
        return;
    }
    char counts[256] = "";
    size_t used = 0;
    char zeros[64] = "";
    for (size_t at = 0; at < dims->count; at++) { // the run-time sizes, and one `[ 0 ]` for each dimension
        if (dims->runtime[at]) {
            used += (size_t)snprintf(counts + used, sizeof counts - used, "%s%llu", used > 0 ? " , " : "",
                                     (unsigned long long)type->array_dims[at]);
        }
        snprintf(zeros + strlen(zeros), sizeof zeros - strlen(zeros), " [ 0 ]");
    }
    char pattern[smith_lang_tuple_name_max + 700];
    char definition[smith_lang_tuple_name_max + 200];
    smith_lang_slice_definition_text(lang, tag, definition, sizeof definition);
    snprintf(pattern, sizeof pattern,
             "__extension__ ( { %s ( struct %s ) { . data = & ( @0 ) %s , . count = { %s } } ; } )", definition, tag,
             zeros, counts);
    SmithLangSliceSpan spans[1] = {{expr, count}};
    cur->count = smith_lang_slice_pattern(cur->tokens, cur->count, smith_lang_slice_cur_max, pattern, spans, expr);
    cur->changed = true;
}

// How the slice of one tag converts to the slice of another, by C's rules for pointers: adding `const` (or `volatile`)
// to the element is implicit, taking it away discards qualifiers, and `restrict` on the data pointer is no part of the
// type's meaning, so it goes either way. The spellings are the tags' (`smith_array_restrict_const_char_n`); an element
// that is a pointer (`const char *`) has its qualifier on what it points at, which C does not convert either.
typedef enum {
    SmithLangSliceRelation_Other,
    SmithLangSliceRelation_Same,
    SmithLangSliceRelation_Adds,
    SmithLangSliceRelation_Discards
} SmithLangSliceRelation;

// The tag after the prefix and `restrict_`, then without its leading `const_` and `volatile_`, which go to `consts`
// and `volatiles`.
static const char *smith_lang_slice_unqualified(const char *tag, bool *consts, bool *volatiles) {
    const char *at = tag + sizeof smith_lang_slice_prefix - 1;
    if (strncmp(at, "restrict_", 9) == 0) at += 9;
    *consts = *volatiles = false;
    for (size_t guard = 0; guard < 4; guard++) { // the qualifiers lead
        if (strncmp(at, "const_", 6) == 0) {
            *consts = true;
            at += 6;
        } else if (strncmp(at, "volatile_", 9) == 0) {
            *volatiles = true;
            at += 9;
        }
    }
    return at;
}

static SmithLangSliceRelation smith_lang_slice_relation(const char *from, const char *to) {
    if (strcmp(from, to) == 0) return SmithLangSliceRelation_Same;
    bool from_const = false;
    bool from_volatile = false;
    bool to_const = false;
    bool to_volatile = false;
    const char *from_rest = smith_lang_slice_unqualified(from, &from_const, &from_volatile);
    const char *to_rest = smith_lang_slice_unqualified(to, &to_const, &to_volatile);
    if (strcmp(from_rest, to_rest) != 0) return SmithLangSliceRelation_Other;
    if (strstr(from_rest, "_ptr") != nullptr && (from_const != to_const || from_volatile != to_volatile)) {
        return SmithLangSliceRelation_Other;
    }
    if ((from_const && !to_const) || (from_volatile && !to_volatile)) return SmithLangSliceRelation_Discards;
    if (from_const != to_const || from_volatile != to_volatile) return SmithLangSliceRelation_Adds;
    return SmithLangSliceRelation_Adds; // only `restrict` differs
}

// The element of a tag as words: `const char` for `smith_array_const_char_n`.
static void smith_lang_slice_tag_words(const char *tag, char *out, size_t capacity) {
    const char *at = tag + sizeof smith_lang_slice_prefix - 1;
    if (strncmp(at, "restrict_", 9) == 0) at += 9;
    snprintf(out, capacity, "%s", at);
    size_t length = strlen(out);
    while (length > 0 &&
           (out[length - 1] == 'n' || out[length - 1] == '_' || (out[length - 1] >= '0' && out[length - 1] <= '9'))) {
        length -= 1;
        if (out[length] == '_') break;
    }
    out[length] = 0;
    for (size_t i = 0; i < length; i++) { // `_ptr` is a star
        if (out[i] == '_') out[i] = ' ';
    }
}

// A slice where a slice of another tag is wanted (`T[:]` to `const T[:]`, `restrict` added or taken off): the same
// pointer and counts in the struct of the wanted tag.
static void smith_lang_slice_requalify(SmithLang *lang, const SmithHost *host, const char *tag,
                                       const SmithLangSliceType *type, const SmithToken *expr, size_t count,
                                       SmithLangSliceCur *cur) {
    if (smith_lang_slice_relation(type->tag, tag) == SmithLangSliceRelation_Discards) {
        char from[96];
        char to[96];
        smith_lang_slice_tag_words(type->tag, from, sizeof from);
        smith_lang_slice_tag_words(tag, to, sizeof to);
        smith_lang_error(
            lang, host, expr[0].location,
            "converting a slice of `%s` to a slice of `%s` discards qualifiers: the elements are read-only", from, to);
        char empty[smith_lang_tuple_name_max + 24];
        snprintf(empty, sizeof empty, "( struct %s ) { }", tag); // a value of the type, so that nothing else is said
        cur->count = smith_lang_slice_pattern(cur->tokens, cur->count, smith_lang_slice_cur_max, empty, nullptr, expr);
        cur->changed = true;
        return;
    }
    SmithLangSliceDims dims = smith_lang_slice_dims_one();
    size_t stem = 0;
    if (!smith_lang_slice_dims_parse(tag, &dims, &stem)) dims = smith_lang_slice_dims_one();
    char counts[160] = "";
    for (size_t at = 0; at < smith_lang_slice_dims_runtime(&dims); at++) { // the counts, as they are
        snprintf(counts + strlen(counts), sizeof counts - strlen(counts), "%ssmith_s . count [ %zu ]",
                 at > 0 ? " , " : "", at);
    }
    char pattern[smith_lang_tuple_name_max + 600];
    char definition[smith_lang_tuple_name_max + 200];
    smith_lang_slice_definition_text(lang, tag, definition, sizeof definition);
    snprintf(
        pattern, sizeof pattern,
        "__extension__ ( { auto smith_s = ( @0 ) ; %s ( struct %s ) { . data = smith_s . data , . count = { %s } } "
        "; } )",
        definition, tag, counts);
    SmithLangSliceSpan spans[1] = {{expr, count}};
    cur->count = smith_lang_slice_pattern(cur->tokens, cur->count, smith_lang_slice_cur_max, pattern, spans, expr);
    cur->changed = true;
}

// The type of `X[a:b]` before it is lowered: a slice of X's element, or of what X is a slice of (one dimension).
static bool smith_lang_slice_cut_type(SmithLang *lang, const SmithHost *host, const SmithToken *expr, size_t count,
                                      SmithLangSliceType *out) {
    if (count < 4 || !smith_lang_punct(&expr[count - 1], "]")) return false;
    size_t open = count - 1;
    int depth = 0;
    for (; open > 0; open--) { // back to the matching `[`
        depth += smith_lang_punct(&expr[open], "]");
        depth -= smith_lang_punct(&expr[open], "[");
        if (depth == 0) break;
    }
    if (open == 0 || smith_lang_slice_shape(expr + open + 1, count - open - 2).colon == SIZE_MAX) return false;
    char text[smith_lang_slice_text_max];
    SmithLangSliceType base = {};
    if (!smith_lang_slice_text(lang, expr, open, text, sizeof text) || !smith_lang_slice_type(host, text, &base)) {
        return false;
    }
    *out = (SmithLangSliceType){};
    if (base.kind == SmithLangSlice_Slice && base.dims.count == 1) {
        out->kind = SmithLangSlice_Slice;
        smith_lang_slice_type_tag(out, base.plain);
        return true;
    }
    if (base.kind != SmithLangSlice_Pointer && base.kind != SmithLangSlice_Array) return false;
    static SmithToken element[48];
    size_t words = smith_lang_slice_words(element, sizeof element / sizeof element[0], base.element, &expr[0]);
    SmithLangSliceDims one = smith_lang_slice_dims_one();
    char tag[smith_lang_tuple_name_max];
    const char *problem = nullptr;
    uint32_t where = 0;
    if (!smith_lang_slice_tag_of(lang, element, words, &one, tag, sizeof tag, &problem, &where)) return false;
    out->kind = SmithLangSlice_Slice;
    smith_lang_slice_type_tag(out, tag);
    return true;
}

// An expression where a slice of `tag` is wanted: an array (or a string) is its elements, a slice is as it is. Returns
// false for a pointer, which is an error.
static bool smith_lang_slice_convert(SmithLang *lang, const SmithHost *host, const char *tag, const SmithToken *expr,
                                     size_t count, SmithLangSliceCur *cur) {
    char text[smith_lang_slice_text_max];
    SmithLangSliceType type = {};
    bool typed = count > 0 && smith_lang_slice_text(lang, expr, count, text, sizeof text) &&
                 smith_lang_slice_type(host, text, &type);
    if (!typed && count > 0) typed = smith_lang_slice_cut_type(lang, host, expr, count, &type);
    if (!typed) {
        smith_lang_slice_add(cur, expr, count);
        return true;
    }
    SmithLangSliceDims dims = smith_lang_slice_dims_one();
    size_t stem = 0;
    if (!smith_lang_slice_dims_parse(tag, &dims, &stem)) dims = smith_lang_slice_dims_one();
    if (type.kind == SmithLangSlice_Array && (type.rank > 1 || dims.count > 1)) {
        smith_lang_slice_convert_rows(lang, host, tag, &dims, &type, expr, count, cur);
        return true;
    }
    if (type.kind == SmithLangSlice_Array) {
        unsigned long long n = type.count - (smith_lang_slice_is_string(expr, count) && type.count > 0 ? 1 : 0);
        smith_lang_slice_wrap(lang, cur, tag, expr, count, n, expr);
        cur->changed = true;
        return true;
    }
    if (type.kind == SmithLangSlice_Slice && smith_lang_slice_relation(type.tag, tag) != SmithLangSliceRelation_Same &&
        smith_lang_slice_relation(type.tag, tag) != SmithLangSliceRelation_Other) {
        smith_lang_slice_requalify(lang, host, tag, &type, expr, count, cur);
        return true;
    }
    if (type.kind == SmithLangSlice_Pointer) {
        smith_lang_error(lang, host, expr[0].location,
                         "a pointer is no slice: say how many elements it points at, `p[0:n]`");
    }
    smith_lang_slice_add(cur, expr, count);
    return true;
}

// A pointer to a slice where a pointer to a slice of `tag` is wanted (C23's rule for pointers to arrays, N2607: a
// pointer to `char[:]` converts to a pointer to `const char[:]`, and the reverse discards qualifiers). The structs
// differ only in the element's qualifiers, so the pointer is cast.
static void smith_lang_slice_convert_pointer(SmithLang *lang, const SmithHost *host, const char *tag,
                                             const SmithToken *expr, size_t count, SmithLangSliceCur *cur) {
    char text[smith_lang_slice_text_max];
    SmithExprType found = {};
    bool typed = count > 0 && smith_lang_slice_text(lang, expr, count, text, sizeof text) &&
                 host->expr_type(host->context, text, (uint32_t)strlen(text), &found) &&
                 strncmp(found.canonical, "struct smith_array_", 19) == 0;
    char from[smith_lang_tuple_name_max] = "";
    if (typed) snprintf(from, sizeof from, "%.*s", (int)strcspn(found.canonical + 7, " "), found.canonical + 7);
    SmithLangSliceRelation relation = typed ? smith_lang_slice_relation(from, tag) : SmithLangSliceRelation_Same;
    if (relation == SmithLangSliceRelation_Same || relation == SmithLangSliceRelation_Other) {
        smith_lang_slice_add(cur, expr, count);
        return;
    }
    char pattern[smith_lang_tuple_name_max + 400];
    char definition[smith_lang_tuple_name_max + 200];
    smith_lang_slice_definition_text(lang, tag, definition, sizeof definition);
    if (relation == SmithLangSliceRelation_Discards) {
        char words_from[96];
        char words_to[96];
        smith_lang_slice_tag_words(from, words_from, sizeof words_from);
        smith_lang_slice_tag_words(tag, words_to, sizeof words_to);
        smith_lang_error(lang, host, expr[0].location,
                         "converting a pointer to a slice of `%s` to a pointer to a slice of `%s` discards "
                         "qualifiers: the elements are read-only",
                         words_from, words_to);
        snprintf(pattern, sizeof pattern, "__extension__ ( { %s ( struct %s * ) 0 ; } )", definition, tag);
    } else {
        snprintf(pattern, sizeof pattern, "__extension__ ( { %s ( struct %s * ) ( @0 ) ; } )", definition, tag);
    }
    SmithLangSliceSpan spans[1] = {{expr, count}};
    cur->count = smith_lang_slice_pattern(cur->tokens, cur->count, smith_lang_slice_cur_max, pattern, spans, expr);
    cur->changed = true;
}

// The arguments of a call, converted: an array where the function wants a slice, and, for a C function, a slice
// where it wants a pointer (`xs.data`; its length is `countof(xs)`).
static void smith_lang_slice_call(SmithLang *lang, const SmithHost *host, const SmithLangSliceChain *chain,
                                  const SmithLangSliceElement *element, SmithLangSliceCur *cur) {
    static char tags[smith_lang_slice_args_max][smith_lang_tuple_name_max];
    char text[smith_lang_slice_text_max];
    SmithExprType found = {};
    size_t params = 0;
    if (smith_lang_slice_text(lang, cur->tokens, cur->count, text, sizeof text) && host->expr_type != nullptr &&
        host->expr_type(host->context, text, (uint32_t)strlen(text), &found)) {
        params = smith_lang_slice_params(found.canonical, tags, smith_lang_slice_args_max);
    }
    smith_lang_slice_add(cur, &chain->tokens[element->from], 1); // `(`
    size_t start = element->from + 1;
    size_t end = element->to - 1;
    int depth = 0;
    size_t argument = 0;
    for (size_t at = start; at <= end; at++) { // each argument, split at the commas outside brackets
        const SmithToken *token = &chain->tokens[at];
        if (at < end) {
            depth += smith_lang_punct(token, "(") || smith_lang_punct(token, "[") || smith_lang_punct(token, "{");
            depth -= smith_lang_punct(token, ")") || smith_lang_punct(token, "]") || smith_lang_punct(token, "}");
            if (!(depth == 0 && smith_lang_punct(token, ","))) continue;
        }
        size_t from = start;
        if (argument < params && tags[argument][0] == '*') {
            smith_lang_slice_convert_pointer(lang, host, tags[argument] + 1, &chain->tokens[from], at - from, cur);
        } else if (argument < params && tags[argument][0] != 0) {
            smith_lang_slice_convert(lang, host, tags[argument], &chain->tokens[from], at - from, cur);
        } else if (chain->c_head && at > from) {
            char arg[smith_lang_slice_text_max];
            SmithLangSliceType type = {};
            if (smith_lang_slice_text(lang, &chain->tokens[from], at - from, arg, sizeof arg) &&
                smith_lang_slice_type(host, arg, &type) && type.kind == SmithLangSlice_Slice) {
                SmithLangSliceSpan spans[1] = {{&chain->tokens[from], at - from}};
                cur->count = smith_lang_slice_pattern(cur->tokens, cur->count, smith_lang_slice_cur_max,
                                                      "( @0 ) . data", spans, token);
                cur->changed = true;
            } else {
                smith_lang_slice_add(cur, &chain->tokens[from], at - from);
            }
        } else {
            smith_lang_slice_add(cur, &chain->tokens[from], at - from);
        }
        if (at < end) smith_lang_slice_add(cur, token, 1); // the comma
        start = at + 1;
        argument += 1;
    }
    smith_lang_slice_add(cur, &chain->tokens[element->to - 1], 1); // `)`
}

// ---- The chain, written again

static bool smith_lang_slice_eligible(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (token->foreign || token->synthetic || lang->recording || lang->in_attribute || host->expr_type == nullptr ||
        token->location == lang->slice.skip) {
        return false;
    }
    if (smith_lang_punct(token, "(")) {
        // The head of `if (`, `while (`, `for (` and `switch (` is no operand: reading its group would look at the
        // token after the `)`, the body's, before the parser has the declarations of the header.
        SmithToken keyword = {};
        if (smith_lang_tuple_previous(lang, host, 1, &keyword) && keyword.kind == SmithTokenKind_Identifier &&
            smith_lang_slice_statement_word(&keyword)) {
            return false;
        }
        return !smith_lang_slice_continues(lang, host);
    }
    if (token->kind != SmithTokenKind_Identifier ||
        smith_lang_word(token->text, token->length) != SmithLangWord_Other) {
        return false;
    }
    SmithToken before = {};
    if (smith_lang_tuple_previous(lang, host, 1, &before) && before.kind == SmithTokenKind_Identifier) {
        SmithLangWord word = smith_lang_word(before.text, before.length);
        if (word != SmithLangWord_Keyword) return false; // a declarator after its type
    }
    return !(before.kind == SmithTokenKind_Punctuator &&
             (smith_lang_punct(&before, ".") || smith_lang_punct(&before, "->") || smith_lang_punct(&before, "::")));
}

// The expression after an `=` up to the `;` or `,` that ends it, from the queue at `from`.
static size_t smith_lang_slice_right(SmithLang *lang, const SmithHost *host, size_t from, SmithToken *out,
                                     size_t capacity) {
    int depth = 0;
    size_t count = 0;
    for (; from + count < smith_lang_queue_max - 4 && count < capacity; count++) { // an initializer is a few tokens
        const SmithToken *token = smith_lang_peek(lang, host, from + count, false);
        if (token->kind == SmithTokenKind_Eof || token->foreign) return 0;
        depth += smith_lang_punct(token, "(") || smith_lang_punct(token, "[") || smith_lang_punct(token, "{");
        depth -= smith_lang_punct(token, ")") || smith_lang_punct(token, "]") || smith_lang_punct(token, "}");
        if (depth < 0 || (depth == 0 && (smith_lang_punct(token, ";") || smith_lang_punct(token, ",")))) break;
        out[count] = *token;
    }
    return count;
}

// The tokens of a type name that ends at the token `at` back (`T`, `m::T` or `struct T`), in order, as far as the
// declaration goes; 0 when the token is no type name.
static size_t smith_lang_slice_type_before(SmithLang *lang, const SmithHost *host, unsigned at, SmithToken *out) {
    SmithToken last = {};
    if (!smith_lang_tuple_previous(lang, host, at, &last) || last.kind != SmithTokenKind_Identifier ||
        smith_lang_word(last.text, last.length) != SmithLangWord_Other) {
        return 0;
    }
    if (last.location == lang->slice.qualified_at && lang->slice.qualified[0] != 0) { // `m::T`: its resolved name
        last = smith_lang_tuple_made(SmithTokenKind_Identifier, lang->slice.qualified, strlen(lang->slice.qualified),
                                     &last, false);
    }
    SmithToken before = {};
    smith_lang_tuple_previous(lang, host, at + 1, &before);
    size_t count = 0;
    if (before.kind == SmithTokenKind_Identifier &&
        (smith_lang_ident(&before, "struct") || smith_lang_ident(&before, "union") ||
         smith_lang_ident(&before, "enum"))) {
        out[count++] = before;
    }
    out[count++] = last;
    return count;
}

// The expression whose type is the declared type of `T name =` or `T *name =`, as the tokens the compiler is asked
// about: `( T ) { }` (`( T * ) 0`). False when the tokens before the `=` are no such declaration; `pointer` says which.
static bool smith_lang_slice_declared_text(SmithLang *lang, const SmithHost *host, char *text, size_t capacity,
                                           bool *pointer) {
    SmithToken name = {};
    SmithToken star = {};
    if (!smith_lang_tuple_previous(lang, host, 1, &name) || name.kind != SmithTokenKind_Identifier ||
        smith_lang_word(name.text, name.length) != SmithLangWord_Other ||
        !smith_lang_tuple_previous(lang, host, 2, &star)) {
        return false;
    }
    *pointer = smith_lang_punct(&star, "*");
    SmithToken tokens[9];
    size_t count = 0;
    tokens[count++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "(", 1, &name, true);
    size_t words = smith_lang_slice_type_before(lang, host, *pointer ? 3 : 2, tokens + count);
    if (words == 0) return false;
    count += words;
    if (*pointer) tokens[count++] = star;
    tokens[count++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, ")", 1, &name, true);
    if (*pointer) {
        tokens[count++] = smith_lang_tuple_made(SmithTokenKind_Number, "0", 1, &name, true);
    } else {
        tokens[count++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "{", 1, &name, true);
        tokens[count++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "}", 1, &name, true);
    }
    return smith_lang_slice_text(lang, tokens, count, text, capacity);
}

// The type a declaration `T name =` or `T *name =` gives its variable, when `T` is a slice (a typedef of one, or the
// struct a `typeof(U[:])` just wrote): its tag, with a star before it for the pointer. `declares` says that the tokens
// before the `=` are a declaration at all.
static bool smith_lang_slice_declared(SmithLang *lang, const SmithHost *host, char *tag, size_t capacity,
                                      bool *declares) {
    SmithToken before = {};
    if (smith_lang_tuple_previous(lang, host, 2, &before) && smith_lang_punct(&before, "}") &&
        lang->slice.typeof_tag[0] != 0) {
        *declares = true;
        snprintf(tag, capacity, "%s", lang->slice.typeof_tag);
        return true;
    }
    char text[smith_lang_slice_text_max];
    bool pointer = false;
    if (!smith_lang_slice_declared_text(lang, host, text, sizeof text, &pointer)) return false;
    *declares = true;
    SmithExprType found = {};
    if (pointer) {
        const char *canonical = found.canonical;
        if (!host->expr_type(host->context, text, (uint32_t)strlen(text), &found) ||
            strncmp(canonical, "struct smith_array_", 19) != 0 || strchr(canonical, '*') != strrchr(canonical, '*')) {
            return false;
        }
        snprintf(tag, capacity, "*%.*s", (int)strcspn(canonical + 7, " "), canonical + 7);
        return true;
    }
    SmithLangSliceType type = {};
    if (!smith_lang_slice_type(host, text, &type) || type.kind != SmithLangSlice_Slice) return false;
    snprintf(tag, capacity, "%s", type.tag);
    return true;
}

// ---- A brace initializer of a struct with slice members (issue #173)

constexpr size_t smith_lang_slice_init_depth = 8;

// What an initializer's value initializes: the struct whose braces are open (as the compiler spells it, empty for
// what is not read: values pass as they are) and the member the next value is for.
typedef struct {
    char type[256];
    uint32_t next;
} SmithLangSliceInitFrame;

// What the compiler says a member's type is, once typedefs are looked through: a slice (`tag`), a struct (`record`) or
// something else.
typedef struct {
    bool slice;
    char tag[smith_lang_tuple_name_max];
    char record[256];
} SmithLangSliceMember;

// A type spelling, `const char *` or `struct Pair`, asked of the compiler as the expression `( T ) { }`.
static bool smith_lang_slice_spelled(const SmithHost *host, const char *spelling, SmithExprType *found) {
    char text[300];
    size_t used = (size_t)snprintf(text, sizeof text, "(\x1f");
    for (size_t at = 0; spelling[at] != 0 && used + 8 < sizeof text; at++) { // the spelling, a token at each space
        text[used++] = spelling[at] == ' ' ? '\x1f' : spelling[at];
    }
    snprintf(text + used, sizeof text - used, "\x1f)\x1f{\x1f}");
    return host->expr_type(host->context, text, (uint32_t)strlen(text), found);
}

static SmithLangSliceMember smith_lang_slice_member_kind(const SmithHost *host, const char *spelling) {
    SmithLangSliceMember member = {};
    SmithExprType found = {};
    if (!smith_lang_slice_spelled(host, spelling, &found)) return member;
    const char *canonical = found.canonical;
    if (strncmp(canonical, "struct smith_array_", 19) == 0 && strchr(canonical, '*') == nullptr &&
        strchr(canonical, '[') == nullptr) {
        member.slice = true;
        snprintf(member.tag, sizeof member.tag, "%.*s", (int)strcspn(canonical + 7, " "), canonical + 7);
    } else if (strncmp(canonical, "struct ", 7) == 0 && strchr(canonical, '*') == nullptr &&
               strchr(canonical, '[') == nullptr && strchr(canonical, '(') == nullptr) {
        snprintf(member.record, sizeof member.record, "%s", canonical);
    }
    return member;
}

// The tokens of the braces that open with `open`, the queue read from place `from` on: up to and with the matching `}`.
// Zero when they do not end within the limit.
static size_t smith_lang_slice_init_read(SmithLang *lang, const SmithHost *host, const SmithToken *open, size_t from,
                                         SmithToken *out, size_t capacity) {
    size_t count = 0;
    out[count++] = *open;
    int depth = 1;
    for (size_t at = 0; at + 1 < capacity && depth > 0; at++) { // an initializer is a few tokens
        const SmithToken *token = smith_lang_peek(lang, host, from + at, false);
        if (token->kind == SmithTokenKind_Eof || token->foreign) return 0;
        depth += smith_lang_punct(token, "{") || smith_lang_punct(token, "(") || smith_lang_punct(token, "[");
        depth -= smith_lang_punct(token, "}") || smith_lang_punct(token, ")") || smith_lang_punct(token, "]");
        out[count++] = *token;
    }
    return depth == 0 ? count : 0;
}

// The end of the value that begins at `from`: the index of the `,` or `}` that follows it at its own depth.
static size_t smith_lang_slice_init_value_end(const SmithToken *tokens, size_t from, size_t count) {
    int depth = 0;
    size_t at = from;
    for (; at < count; at++) { // each token
        depth += smith_lang_punct(&tokens[at], "{") || smith_lang_punct(&tokens[at], "(") ||
                 smith_lang_punct(&tokens[at], "[");
        depth -= smith_lang_punct(&tokens[at], "}") || smith_lang_punct(&tokens[at], ")") ||
                 smith_lang_punct(&tokens[at], "]");
        if (depth < 0 || (depth == 0 && smith_lang_punct(&tokens[at], ","))) break;
    }
    return at;
}

// The member `.name` of a struct, as its index, or UINT32_MAX.
static uint32_t smith_lang_slice_init_find(const SmithHost *host, const char *type, const SmithToken *name) {
    SmithFieldInfo info = {};
    if (!host->type_field(host->context, type, (uint32_t)strlen(type), 0, &info) || info.status != SmithFields_Ok) {
        return UINT32_MAX;
    }
    for (uint32_t index = 0; index < info.count; index++) { // each named member
        SmithFieldInfo member = {};
        host->type_field(host->context, type, (uint32_t)strlen(type), index, &member);
        if (smith_lang_equals(name->text, name->length, member.name)) return index;
    }
    return UINT32_MAX;
}

// What one initializer walk has: the tokens, the struct frames and the output.
typedef struct {
    const SmithToken *tokens;
    size_t count;
    SmithLangSliceInitFrame frames[smith_lang_slice_init_depth];
    size_t depth;
} SmithLangSliceInit;

// A value for a member that is a slice: a slice expression converts as an ordinary initializer does; braces are no
// slice. Returns the index after the value.
static size_t smith_lang_slice_init_slice(SmithLang *lang, const SmithHost *host, const SmithLangSliceInit *init,
                                          size_t at, const char *tag, SmithLangSliceCur *cur) {
    size_t end = smith_lang_slice_init_value_end(init->tokens, at, init->count);
    if (smith_lang_punct(&init->tokens[at], "{")) {
        smith_lang_error(lang, host, init->tokens[at].location,
                         "a slice takes a slice expression, not braces: write the array or the string it views "
                         "(`.name = \"abc\"`), or assign the member afterwards");
        smith_lang_slice_add(cur, init->tokens + at, end - at);
        return end;
    }
    SmithLangSliceCur piece = {};
    smith_lang_slice_convert(lang, host, tag, init->tokens + at, end - at, &piece);
    smith_lang_slice_add(cur, piece.tokens, piece.count);
    cur->changed = cur->changed || piece.changed;
    cur->overflow = cur->overflow || piece.overflow;
    return end;
}

// The value at `at` of the open struct, copied or converted, and the index after it.
static size_t smith_lang_slice_init_value(SmithLang *lang, const SmithHost *host, SmithLangSliceInit *init, size_t at,
                                          SmithLangSliceCur *cur) {
    SmithLangSliceInitFrame *frame = &init->frames[init->depth - 1];
    SmithFieldInfo info = {};
    bool known = frame->type[0] != 0 &&
                 host->type_field(host->context, frame->type, (uint32_t)strlen(frame->type), frame->next, &info) &&
                 info.status == SmithFields_Ok && frame->next < info.count;
    frame->next += 1;
    SmithLangSliceMember member = known ? smith_lang_slice_member_kind(host, info.type) : (SmithLangSliceMember){};
    if (member.slice) return smith_lang_slice_init_slice(lang, host, init, at, member.tag, cur);
    if (smith_lang_punct(&init->tokens[at], "{") && init->depth < smith_lang_slice_init_depth) {
        SmithLangSliceInitFrame *inner = &init->frames[init->depth++];
        *inner = (SmithLangSliceInitFrame){};
        snprintf(inner->type, sizeof inner->type, "%s", member.record);
        smith_lang_slice_add(cur, init->tokens + at, 1);
        return at + 1;
    }
    size_t end = smith_lang_slice_init_value_end(init->tokens, at, init->count);
    smith_lang_slice_add(cur, init->tokens + at, end - at);
    return end;
}

// A designator that is not `.name =` (`.a.b =`, `[i] =`): where the struct has a slice member the walk cannot follow
// it, and a slice left unconverted would be an empty one, so it is an error.
static void smith_lang_slice_init_designator(SmithLang *lang, const SmithHost *host, const SmithLangSliceInit *init,
                                             size_t at) {
    const char *type = init->frames[init->depth - 1].type;
    SmithFieldInfo info = {};
    if (type[0] == 0 || !host->type_field(host->context, type, (uint32_t)strlen(type), 0, &info) ||
        info.status != SmithFields_Ok) {
        return;
    }
    for (uint32_t index = 0; index < info.count; index++) { // each named member
        SmithFieldInfo member = {};
        host->type_field(host->context, type, (uint32_t)strlen(type), index, &member);
        if (smith_lang_slice_member_kind(host, member.type).slice) {
            smith_lang_error(lang, host, init->tokens[at].location,
                             "a designator of a struct with a slice member names one member, `.name = value`: a "
                             "slice of a deeper designator or an array element is not read; write the braces of "
                             "the inner struct, or assign the slice afterwards");
            return;
        }
    }
}

// `{ ... }` for a struct of the spelling `type`, written again with each slice member's value converted. True when
// something changed.
static bool smith_lang_slice_init_walk(SmithLang *lang, const SmithHost *host, const char *type,
                                       const SmithToken *tokens, size_t count, SmithLangSliceCur *cur) {
    SmithLangSliceInit init = {.tokens = tokens, .count = count, .depth = 1};
    snprintf(init.frames[0].type, sizeof init.frames[0].type, "%s", type);
    smith_lang_slice_add(cur, tokens, 1);
    size_t at = 1;
    bool start = true; // a value, or a designator, begins here
    for (size_t guard = 0; guard < count && at < count && init.depth > 0 && !cur->overflow; guard++) {
        const SmithToken *token = &tokens[at];
        if (smith_lang_punct(token, "}")) {
            init.depth -= 1;
            smith_lang_slice_add(cur, token, 1);
            at += 1;
            start = false;
        } else if (smith_lang_punct(token, ",")) {
            smith_lang_slice_add(cur, token, 1);
            at += 1;
            start = true;
        } else if (start && smith_lang_punct(token, ".") && at + 2 < count &&
                   smith_lang_punct(&tokens[at + 2], "=")) { // `.name = value`
            SmithLangSliceInitFrame *frame = &init.frames[init.depth - 1];
            uint32_t member =
                frame->type[0] != 0 ? smith_lang_slice_init_find(host, frame->type, &tokens[at + 1]) : UINT32_MAX;
            if (member != UINT32_MAX) frame->next = member;
            smith_lang_slice_add(cur, token, 3);
            at += 3;
        } else if (start && (smith_lang_punct(token, ".") || smith_lang_punct(token, "["))) {
            smith_lang_slice_init_designator(lang, host, &init, at);
            smith_lang_slice_add(cur, token, 1);
            at += 1;
            start = false;
        } else if (start) {
            size_t was = init.depth;
            at = smith_lang_slice_init_value(lang, host, &init, at, cur);
            start = init.depth > was; // a nested brace opened: its first value follows
        } else {
            smith_lang_slice_add(cur, token, 1);
            at += 1;
        }
    }
    return cur->changed && !cur->overflow && init.depth == 0;
}

// The struct a brace initializer is for, from the compiler's answer for `text` (`( T ) { }`): its spelling.
static bool smith_lang_slice_init_record(const SmithHost *host, const char *text, char *type, size_t capacity) {
    SmithExprType found = {};
    const char *canonical = found.canonical;
    if (host->type_field == nullptr || !host->expr_type(host->context, text, (uint32_t)strlen(text), &found) ||
        strncmp(canonical, "struct ", 7) != 0 || strchr(canonical, '*') != nullptr ||
        strchr(canonical, '[') != nullptr || strchr(canonical, '(') != nullptr) {
        return false;
    }
    snprintf(type, capacity, "%s", canonical);
    return true;
}

// Reads the braces of an initializer (`open` is its `{`, in the queue at place 0 or the token being lowered), walks
// them and, when a slice member's value changed, writes them again in place of the queue's.
static bool smith_lang_slice_init_lower(SmithLang *lang, const SmithHost *host, const char *type,
                                        const SmithToken *open, bool queued) {
    static SmithToken tokens[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_init_read(lang, host, open, queued ? 1 : 0, tokens, smith_lang_slice_chain_max);
    if (count == 0) return false;
    static SmithLangSliceCur converted;
    converted = (SmithLangSliceCur){};
    if (!smith_lang_slice_init_walk(lang, host, type, tokens, count, &converted)) return false;
    if (!smith_lang_tuple_replace(lang, queued ? count : count - 1, converted.tokens, converted.count)) return false;
    smith_lang_slice_flag(lang);
    return true;
}

// `T name = { ... }` where `T` is a struct: its slice members' values are slices (issue #173). True when written again.
static bool smith_lang_slice_init_equals(SmithLang *lang, const SmithHost *host) {
    char text[smith_lang_slice_text_max];
    char type[256];
    bool pointer = false;
    if (!smith_lang_slice_declared_text(lang, host, text, sizeof text, &pointer) || pointer ||
        !smith_lang_slice_init_record(host, text, type, sizeof type)) {
        return false;
    }
    SmithToken brace = *smith_lang_peek(lang, host, 0, false);
    return smith_lang_punct(&brace, "{") && smith_lang_slice_init_lower(lang, host, type, &brace, true);
}

// `( T ) { ... }`, a compound literal of a struct, at its `{`: the same. True when the token was replaced.
static bool smith_lang_slice_init_compound(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (token->foreign || token->synthetic || lang->recording || host->expr_type == nullptr ||
        !smith_lang_punct(token, "{") || token->location == lang->slice.brace_skip) {
        return false;
    }
    SmithToken before[8];
    size_t open = 0;
    for (size_t back = 1; back <= 8 && open == 0; back++) { // back to the `(` of the cast
        if (!smith_lang_tuple_previous(lang, host, (unsigned)back, &before[back - 1])) return false;
        if (back == 1 && !smith_lang_punct(&before[0], ")")) return false;
        if (back > 1 && smith_lang_punct(&before[back - 1], "(")) open = back;
        if (back > 1 && !smith_lang_punct(&before[back - 1], "(") &&
            !(before[back - 1].kind == SmithTokenKind_Identifier || smith_lang_punct(&before[back - 1], "::"))) {
            return false;
        }
    }
    SmithToken outer = {};
    if (open < 3 || (smith_lang_tuple_previous(lang, host, (unsigned)open + 1, &outer) &&
                     outer.kind == SmithTokenKind_Identifier && !smith_lang_ident(&outer, "return"))) {
        return false;
    }
    SmithToken tokens[12];
    size_t count = 0;
    for (size_t back = open; back >= 1; back--) // `(` to `)`, in order
        tokens[count++] = before[back - 1];
    tokens[count++] = *token;
    tokens[count++] = smith_lang_tuple_made(SmithTokenKind_Punctuator, "}", 1, token, true);
    char text[smith_lang_slice_text_max];
    char type[256];
    if (!smith_lang_slice_text(lang, tokens, count, text, sizeof text) ||
        !smith_lang_slice_init_record(host, text, type, sizeof type)) {
        return false;
    }
    lang->slice.brace_skip = token->location;
    if (smith_lang_slice_init_lower(lang, host, type, token, false)) return true;
    lang->slice.brace_skip = 0;
    return false;
}

// The `=` being lowered, of `slice = array` or `T xs[:] = array`: the right side becomes the slice of the left's type.
// (The left side is typed from the tokens before it; a declaration's own name is not declared yet, so it is known by
// the tag its declaration made.)
static void smith_lang_slice_equals(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    if (token->foreign || token->synthetic || lang->recording || host->expr_type == nullptr ||
        host->previous == nullptr || !smith_lang_punct(token, "=")) {
        return;
    }
    SmithToken before;
    char tag[smith_lang_tuple_name_max] = "";
    bool declares = false;
    if (smith_lang_slice_init_equals(lang, host)) return;
    if (lang->slice.armed_tag[0] != 0 && smith_lang_tuple_previous(lang, host, 1, &before) &&
        smith_lang_equals(before.text, before.length, lang->slice.armed)) {
        snprintf(tag, sizeof tag, "%s", lang->slice.armed_tag);
    } else if (smith_lang_slice_declared(lang, host, tag, sizeof tag, &declares)) {
        // `Text s = ...` or `typeof(const char[:]) s = ...`: the declaration's type is a slice
    } else if (declares) {
        return; // a declaration of another type: the name before it is no earlier variable
    } else {
        char text[512];
        SmithLangSliceType type = {};
        if (!smith_lang_tuple_base(lang, host, text, sizeof text) || !smith_lang_slice_type(host, text, &type) ||
            type.kind != SmithLangSlice_Slice) {
            return;
        }
        snprintf(tag, sizeof tag, "%s", type.tag);
    }
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, 0, right, smith_lang_slice_chain_max);
    if (count == 0 || smith_lang_punct(&right[0], "{")) return;
    static SmithLangSliceCur converted;
    converted = (SmithLangSliceCur){};
    if (tag[0] == '*') {
        smith_lang_slice_convert_pointer(lang, host, tag + 1, right, count, &converted);
    } else {
        smith_lang_slice_convert(lang, host, tag, right, count, &converted);
    }
    if (converted.changed && !converted.overflow)
        smith_lang_tuple_replace(lang, count, converted.tokens, converted.count);
}

// `lhs = array;` where the chain `lhs` is a slice: the right side becomes the slice (read from the queue). Returns the
// tokens of the queue it took past the chain (the `=` and the right side), or 0.
static size_t smith_lang_slice_assign(SmithLang *lang, const SmithHost *host, const SmithLangSliceChain *chain,
                                      SmithLangSliceCur *cur) {
    char text[smith_lang_slice_text_max];
    SmithLangSliceType type = {};
    if (!smith_lang_slice_text(lang, cur->tokens, cur->count, text, sizeof text) ||
        !smith_lang_slice_type(host, text, &type) || type.kind != SmithLangSlice_Slice) {
        return 0;
    }
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, chain->count, right, smith_lang_slice_chain_max);
    if (count == 0 || smith_lang_punct(&right[0], "{")) return 0;
    static SmithLangSliceCur converted;
    converted = (SmithLangSliceCur){};
    smith_lang_slice_convert(lang, host, type.tag, right, count, &converted);
    if (!converted.changed || converted.overflow) return 0;
    smith_lang_slice_add(cur, smith_lang_peek(lang, host, chain->count - 1, false), 1); // the `=`
    smith_lang_slice_add(cur, converted.tokens, converted.count);
    return count + 1;
}

// A chain with a subscript, a call or an assignment: written again, or left as it is (false).
static bool smith_lang_slice_chain(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    SmithLangSliceChain *chain = &smith_lang_slice_chain_state;
    if (!smith_lang_slice_eligible(lang, host, token) || !smith_lang_slice_read(lang, host, token, chain)) return false;
    if (!chain->subscripts && !chain->calls && !chain->assigned) return false;
    SmithLangSliceCur *cur = &smith_lang_slice_cur;
    cur->count = 0;
    cur->changed = cur->overflow = cur->sliced = false;
    smith_lang_slice_add(cur, chain->tokens, chain->head_end);
    for (size_t i = 0; i < chain->element_count; i++) { // each member, call and subscript
        const SmithLangSliceElement *element = &chain->elements[i];
        if (element->kind == SmithLangSliceElement_Subscript) {
            i += smith_lang_slice_subscript_element(lang, host, chain, i, cur) - 1;
        } else if (element->kind == SmithLangSliceElement_Call && chain->calls) {
            smith_lang_slice_call(lang, host, chain, element, cur);
        } else {
            smith_lang_slice_add(cur, &chain->tokens[element->from], element->to - element->from);
        }
    }
    size_t extra = 0;
    if (chain->assigned) {
        extra = smith_lang_slice_assign(lang, host, chain, cur);
        cur->changed = cur->changed || extra > 0;
    }
    if (!cur->changed || cur->overflow) return false;
    for (size_t i = 0; i < cur->count; i++) { // the first token is skipped when it comes again
        if (!cur->tokens[i].synthetic && cur->tokens[i].location == token->location) {
            lang->slice.skip = token->location;
            break;
        }
    }
    if (!smith_lang_tuple_replace(lang, chain->count - 1 + extra, cur->tokens, cur->count)) return false;
    smith_lang_slice_flag(lang);
    return true; // the token itself is dropped: its copy leads the queue
}

// `return array;` in a function that returns a slice.
static void smith_lang_slice_return(SmithLang *lang, const SmithHost *host, const SmithToken *token) {
    char type[256];
    if (host->return_type == nullptr || !host->return_type(host->context, type, sizeof type) ||
        strncmp(type, "struct smith_array_", 19) != 0 || strchr(type + 7, ' ') != nullptr ||
        smith_lang_punct(smith_lang_peek(lang, host, 0, false), "{")) {
        return;
    }
    static SmithToken right[smith_lang_slice_chain_max];
    size_t count = smith_lang_slice_right(lang, host, 0, right, smith_lang_slice_chain_max);
    if (count == 0) return;
    static SmithLangSliceCur converted;
    converted = (SmithLangSliceCur){};
    smith_lang_slice_convert(lang, host, type + 7, right, count, &converted);
    if (converted.changed && !converted.overflow)
        smith_lang_tuple_replace(lang, count, converted.tokens, converted.count);
    (void)token;
}

// `countof(xs)` of a slice: `((xs).count[0])`. `span` holds the operand at 1 to close - 1. A row, `countof(xs[0])`, is
// a slice too: the operand is written again as a slice and the subscripts lower it.
static bool smith_lang_slice_countof(SmithLang *lang, const SmithHost *host, const SmithToken *token,
                                     const char *tokens, const SmithToken *span, size_t close) {
    SmithLangSliceType type = {};
    bool sliced = false; // `countof(xs[a:b])`: the operand is a slice whatever it is a slice of
    size_t end = close;  // the operand is [1, end) once its trailing subscripts are taken off
    size_t indexes = 0;
    for (size_t groups = 0; groups < smith_lang_slice_dims_max + 4 && end > 2 && smith_lang_punct(&span[end - 1], "]");
         groups++) { // each trailing `[ ... ]`
        size_t open = end - 1;
        int depth = 0;
        for (; open > 1; open--) { // back to the matching `[`
            depth += smith_lang_punct(&span[open], "]");
            depth -= smith_lang_punct(&span[open], "[");
            if (depth == 0) break;
        }
        bool colon = smith_lang_slice_shape(span + open + 1, end - 2 - open).colon != SIZE_MAX;
        if (groups == 0 && colon) sliced = true;
        indexes += colon ? 0 : 1;
        end = open;
    }
    if (!sliced && indexes > 0 && end > 1) { // `countof(xs[i])`: a row of a slice of more dimensions
        char base[smith_lang_slice_text_max];
        SmithLangSliceType of = {};
        if (smith_lang_slice_text(lang, span + 1, end - 1, base, sizeof base) &&
            smith_lang_slice_type(host, base, &of) && of.kind == SmithLangSlice_Slice && of.dims.count > indexes) {
            sliced = true;
        }
    }
    if (!sliced && (!smith_lang_slice_type(host, tokens, &type) || type.kind != SmithLangSlice_Slice)) return false;
    SmithLangSliceSpan spans[1] = {{span + 1, close - 1}};
    static SmithLangSliceCur cur;
    cur = (SmithLangSliceCur){};
    cur.count =
        smith_lang_slice_pattern(cur.tokens, 0, smith_lang_slice_cur_max, "( ( @0 ) . count [ 0 ] )", spans, token);
    cur.tokens[0].space_before = token->space_before;
    cur.tokens[0].line_start = token->line_start;
    return smith_lang_tuple_replace(lang, close + 1, cur.tokens, cur.count);
}

// ---- A slice in an instance's body (issue #119)

// An instance's body is the generic's tokens written again, not read by the lowering, so `xs` (an `auto` parameter) has
// no `countof`, subscript or cut of a slice. The replay writes them as the lowering does for a source's own slices, in
// frames: the words of the pattern and the generic's own tokens between them (which are replayed in turn, so a
// `countof(xs)` in an index is lowered too).

typedef struct {
    const char *text; // words, or nullptr: the generic's tokens [first, last)
    uint32_t first;
    uint32_t last;
} SmithLangSlicePart;

static void smith_lang_slice_parts(SmithLangReplay *replay, uint32_t place, const SmithLangSlicePart *parts,
                                   size_t count) {
    for (size_t at = count; at > 0; at--) { // the frames run last to first
        const SmithLangSlicePart *part = &parts[at - 1];
        if (part->text == nullptr) {
            smith_lang_replay_push(replay, part->first, part->last, 0);
            continue;
        }
        if (!smith_lang_replay_push(replay, 0, 0, 0)) return;
        SmithLangFrame *frame = &replay->frames[replay->count - 1];
        snprintf(frame->words, sizeof frame->words, "%s", part->text);
        frame->place = place;
    }
}

// The slice locals an instance's body has declared so far (issue #135): the replay writes a declaration `T v[:] = e;`
// as the lowering does, so a later `countof(v)`, `v[i]` or call with `v` is lowered like a parameter's. The table
// belongs to one instance; a name declared again (a sibling block's) takes the entry over.
constexpr size_t smith_lang_slice_locals_max = 16;
static struct {
    uint32_t instance;
    size_t count;
    char names[smith_lang_slice_locals_max][96];
    SmithLangSliceType types[smith_lang_slice_locals_max];
} smith_lang_slice_locals;

static bool smith_lang_slice_local_named(const SmithLangReplay *replay, uint32_t at, SmithLangSliceType *type) {
    const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
    if (smith_lang_slice_locals.instance != replay->instance_id || token->kind != SmithTokenKind_Identifier) {
        return false;
    }
    for (size_t k = smith_lang_slice_locals.count; k > 0; k--) { // the latest first
        if (smith_lang_equals(smith_lang_gt_str(token), token->length, smith_lang_slice_locals.names[k - 1])) {
            *type = smith_lang_slice_locals.types[k - 1];
            return true;
        }
    }
    return false;
}

static void smith_lang_slice_local_add(const SmithLangReplay *replay, const SmithLangGenToken *name, const char *tag) {
    if (smith_lang_slice_locals.instance != replay->instance_id) {
        smith_lang_slice_locals.instance = replay->instance_id;
        smith_lang_slice_locals.count = 0;
    }
    size_t slot = smith_lang_slice_locals.count;
    for (size_t k = 0; k < smith_lang_slice_locals.count; k++) { // declared again: the same entry
        if (smith_lang_equals(smith_lang_gt_str(name), name->length, smith_lang_slice_locals.names[k])) slot = k;
    }
    if (slot == smith_lang_slice_locals_max) return;
    smith_lang_slice_locals.count += slot == smith_lang_slice_locals.count ? 1 : 0;
    snprintf(smith_lang_slice_locals.names[slot], sizeof smith_lang_slice_locals.names[slot], "%.*s", (int)name->length,
             smith_lang_gt_str(name));
    SmithLangSliceType *type = &smith_lang_slice_locals.types[slot];
    *type = (SmithLangSliceType){.kind = SmithLangSlice_Slice};
    smith_lang_slice_type_tag(type, tag);
    size_t stem = 0;
    size_t prefix = sizeof smith_lang_slice_prefix - 1 + (type->dims.restrict_data ? 9 : 0);
    char element[256] = "int";
    if (smith_lang_slice_dims_parse(tag, &type->dims, &stem) && stem > prefix) { // the pointer's type: `float *`
        smith_lang_ty_stem_spelling(tag + prefix, stem - prefix, element, sizeof element);
    }
    snprintf(type->element, sizeof type->element, "%s *", element);
}

// The brackets `[ const restrict : ]` of a slice local whose `[` is at `open`: the index of the `:`, or UINT32_MAX when
// they are no such brackets (or more dimensions follow, which are not written here yet).
static uint32_t smith_lang_slice_local_bracket(uint32_t open, uint32_t end, SmithLangSliceDims *dims, bool *is_const) {
    uint32_t at = open + 1;
    for (; at < end && smith_lang_gt.tokens[at].kind == SmithTokenKind_Identifier; at++) { // each qualifier
        const SmithLangGenToken *word = &smith_lang_gt.tokens[at];
        if (smith_lang_gt_is(word, SmithTokenKind_Identifier, "const")) {
            *is_const = true;
        } else if (smith_lang_gt_is(word, SmithTokenKind_Identifier, "restrict")) {
            dims->restrict_data = true;
        } else {
            return UINT32_MAX;
        }
    }
    if (at + 1 >= end || !smith_lang_gt_punct(&smith_lang_gt.tokens[at], ":") ||
        !smith_lang_gt_punct(&smith_lang_gt.tokens[at + 1], "]") ||
        (at + 2 < end && smith_lang_gt_punct(&smith_lang_gt.tokens[at + 2], "["))) {
        return UINT32_MAX;
    }
    return at;
}

// The element's tokens [first, last) of the generic as the lowering reads them: the pool holds `u8` as the compiler's
// own name for it, which is mapped back so that the tag is the one an ordinary declaration gets.
static size_t smith_lang_slice_local_element(uint32_t first, uint32_t last, SmithToken elem[12]) {
    size_t count = 0;
    for (uint32_t k = first; k < last; k++) { // the tokens before the name
        const SmithLangGenToken *token = &smith_lang_gt.tokens[k];
        elem[count] = (SmithToken){.kind = (SmithTokenKind)token->kind,
                                   .text = smith_lang_gt_str(token),
                                   .length = token->length,
                                   .location = token->location};
        for (size_t s = 0; s < sizeof smith_lang_sized / sizeof smith_lang_sized[0]; s++) { // 13 entries
            if (!smith_lang_equals(elem[count].text, elem[count].length, smith_lang_sized[s].standard)) continue;
            elem[count].text = smith_lang_sized[s].name;
            elem[count].length = (uint32_t)strlen(smith_lang_sized[s].name);
        }
        count += 1;
    }
    return count;
}

// The declaration at the frame's position is a slice local, `T v[:]` or `const T v[restrict :]` (one dimension, an
// element the generic's own tokens spell): write its struct, the name and what follows as the lowering does, and take
// the name for a slice. False when it is no such declaration.
static bool smith_lang_slice_replay_local(SmithLang *lang, SmithLangReplay *replay, SmithLangFrame *frame,
                                          uint32_t place) {
    static const char *const words[] = {"static", "extern", "typedef", "register", "constexpr", "auto",        "return",
                                        "case",   "goto",   "else",    "sizeof",   "typeof",    "thread_local"};
    uint32_t first = frame->pos;
    uint32_t end = frame->end;
    const SmithLangGenToken *before = first > 0 ? &smith_lang_gt.tokens[first - 1] : nullptr;
    if (replay->typing || before == nullptr ||
        !(smith_lang_gt_punct(before, ";") || smith_lang_gt_punct(before, "{") || smith_lang_gt_punct(before, "}"))) {
        return false;
    }
    uint32_t open = UINT32_MAX;
    for (uint32_t at = first; at < end && at < first + 12 && open == UINT32_MAX; at++) { // the type's few tokens
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (token->special != SmithLangGen_Plain || token->param != 0) return false;
        if (token->kind == SmithTokenKind_Identifier) {
            for (size_t k = 0; k < sizeof words / sizeof words[0]; k++) { // 13 words
                if (smith_lang_equals(smith_lang_gt_str(token), token->length, words[k])) return false;
            }
        } else if (smith_lang_gt_punct(token, "[")) {
            open = at;
        } else if (!smith_lang_gt_punct(token, "*")) {
            return false;
        }
    }
    if (open == UINT32_MAX || open < first + 2 || smith_lang_gt.tokens[open - 1].kind != SmithTokenKind_Identifier) {
        return false;
    }
    SmithLangSliceDims dims = smith_lang_slice_dims_one();
    bool is_const = false;
    uint32_t at = smith_lang_slice_local_bracket(open, end, &dims, &is_const);
    if (at == UINT32_MAX) return false;
    SmithToken elem[12] = {};
    size_t count = smith_lang_slice_local_element(first, open - 1, elem);
    char tag[smith_lang_tuple_name_max];
    const char *problem = nullptr;
    uint32_t where = 0;
    if (!smith_lang_slice_tag_of(lang, elem, count, &dims, tag, sizeof tag, &problem, &where)) return false;
    static char text[smith_lang_tuple_name_max * 2 + 400];
    char definition[smith_lang_tuple_name_max + 200];
    smith_lang_slice_definition_text(lang, tag, definition, sizeof definition);
    const SmithLangGenToken *name = &smith_lang_gt.tokens[open - 1];
    snprintf(text, sizeof text, "%s %sstruct %s %.*s", definition, is_const ? "const " : "", tag, (int)name->length,
             smith_lang_gt_str(name));
    smith_lang_slice_local_add(replay, name, tag);
    frame->pos = at + 2;
    SmithLangSlicePart parts[] = {{text, 0, 0}};
    smith_lang_slice_parts(replay, place, parts, 1);
    return true;
}

// Whether the identifier at `at` names a parameter of the instance whose type is a slice (the entries the instance
// recorded when it was asked for; the compiler has not read the instance's signature yet), or a slice local of it.
static bool smith_lang_slice_replay_named(const SmithLangReplay *replay, uint32_t at, SmithLangSliceType *type) {
    if (smith_lang_slice_local_named(replay, at, type)) return true;
    int slot = smith_lang_param_type_of(replay->generic, replay->instance, (SmithLangSpan){at, at + 1});
    if (slot < 0) return false;
    char text[1024];
    snprintf(text, sizeof text, "%s", smith_lang_str(replay->instance->slices));
    for (char *entry = text; *entry != 0;) { // `slot \x1f tag \x1f data \x1f count \x1e`
        char *end = strchr(entry, '\x1e');
        char *tag = strchr(entry, '\x1f');
        if (end == nullptr || tag == nullptr || tag > end) return false;
        *tag++ = 0;
        char *stop = strchr(tag, '\x1f');
        if (stop != nullptr && stop < end && strtol(entry, nullptr, 10) == slot) {
            *stop++ = 0;
            char *count = strchr(stop, '\x1f');
            if (count != nullptr) *count = 0;
            *type = (SmithLangSliceType){.kind = SmithLangSlice_Slice};
            smith_lang_slice_type_tag(type, tag);
            snprintf(type->element, sizeof type->element, "%s", stop); // the pointer's type: `float *`
            return true;
        }
        entry = end + 1;
    }
    return false;
}

// The `]` that closes the `[` at `open` within [open, end), or UINT32_MAX; `colon` says a `:` stands at its top level.
static uint32_t smith_lang_slice_replay_close(uint32_t open, uint32_t end, uint32_t *colon) {
    int depth = 0;
    int conditions = 0;
    *colon = UINT32_MAX;
    for (uint32_t at = open; at < end; at++) { // the subscript
        const SmithLangGenToken *token = &smith_lang_gt.tokens[at];
        if (token->special != SmithLangGen_Plain || token->kind != SmithTokenKind_Punctuator) continue;
        if (smith_lang_gt_open(token)) depth += 1;
        if (smith_lang_gt_close(token)) depth -= 1;
        if (depth == 0) return at;
        if (depth != 1) continue;
        if (smith_lang_gt_punct(token, "?")) conditions += 1;
        if (!smith_lang_gt_punct(token, ":")) continue;
        if (conditions > 0) {
            conditions -= 1;
        } else if (*colon == UINT32_MAX) {
            *colon = at;
        }
    }
    return UINT32_MAX;
}

static bool smith_lang_slice_replay_countof(SmithLangReplay *replay, uint32_t place, SmithLangFrame *frame) {
    uint32_t at = frame->pos;
    if (at + 3 >= frame->end || !smith_lang_gt_punct(&smith_lang_gt.tokens[at + 1], "(") ||
        !smith_lang_gt_punct(&smith_lang_gt.tokens[at + 3], ")") ||
        smith_lang_gt.tokens[at + 2].special != SmithLangGen_Plain) {
        return false;
    }
    SmithLangSliceType type = {};
    if (!smith_lang_slice_replay_named(replay, at + 2, &type)) return false;
    frame->pos += 4;
    if (replay->typing) { // the compiler is asked only its type, and the slice's struct is not one it has read
        SmithLangSlicePart typed[] = {{"( ( __SIZE_TYPE__ ) 0 )", 0, 0}};
        smith_lang_slice_parts(replay, place, typed, 1);
        return true;
    }
    SmithLangSlicePart parts[] = {{"( (", 0, 0}, {nullptr, at + 2, at + 3}, {") . count [ 0 ] )", 0, 0}};
    smith_lang_slice_parts(replay, place, parts, sizeof parts / sizeof parts[0]);
    return true;
}

// `E[a:b]` of a one-dimension slice: the slice's expression is the generic's tokens [first, open), the subscript's
// [open + 1, close) split at `colon`; a view of the elements. `bounds` is first, open, colon, close.
static bool smith_lang_slice_replay_cut(const SmithHost *host, SmithLangReplay *replay, uint32_t place,
                                        const SmithLangSliceType *type, const uint32_t bounds[4], unsigned counter) {
    uint32_t first_at = bounds[0];
    uint32_t open_at = bounds[1];
    uint32_t colon = bounds[2];
    uint32_t close = bounds[3];
    char condition[160];
    char check[512];
    char head[640];
    snprintf(condition, sizeof condition, "smith_a%1$u <= smith_b%1$u && smith_b%1$u <= smith_s%1$u . count [ 0 ]",
             counter);
    smith_lang_slice_check(host, condition, check, sizeof check);
    snprintf(head, sizeof head, "__extension__ ( { auto smith_s%u = (", counter);
    char first[160];
    char second[160];
    char last[1024];
    snprintf(first, sizeof first, ") ; __SIZE_TYPE__ smith_a%u = (", counter);
    snprintf(second, sizeof second, ") ; __SIZE_TYPE__ smith_b%u = (", counter);
    snprintf(last, sizeof last,
             ") ; %s ( struct %s ) { . data = smith_a%u == 0 ? smith_s%u . data : smith_s%u . data + smith_a%u , . "
             "count = { smith_b%u - smith_a%u } } ; } )",
             check, type->tag, counter, counter, counter, counter, counter, counter);
    char whole[64];
    snprintf(whole, sizeof whole, "smith_s%u . count [ 0 ]", counter);
    SmithLangSlicePart parts[9] = {{head, 0, 0}, {nullptr, first_at, open_at}, {first, 0, 0}};
    size_t used = 3;
    if (colon == open_at + 1) {
        parts[used++] = (SmithLangSlicePart){"0", 0, 0};
    } else {
        parts[used++] = (SmithLangSlicePart){nullptr, open_at + 1, colon};
    }
    parts[used++] = (SmithLangSlicePart){second, 0, 0};
    if (colon + 1 == close) {
        parts[used++] = (SmithLangSlicePart){whole, 0, 0};
    } else {
        parts[used++] = (SmithLangSlicePart){nullptr, colon + 1, close};
    }
    parts[used++] = (SmithLangSlicePart){last, 0, 0};
    smith_lang_slice_parts(replay, place, parts, used);
    return true;
}

static bool smith_lang_slice_replay_subscript(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                              uint32_t place, SmithLangFrame *frame) {
    uint32_t at = frame->pos;
    if (at + 3 >= frame->end || !smith_lang_gt_punct(&smith_lang_gt.tokens[at + 1], "[") ||
        smith_lang_gt_punct(&smith_lang_gt.tokens[at + 2], "[")) {
        return false;
    }
    SmithLangSliceType type = {};
    uint32_t colon = UINT32_MAX;
    uint32_t close = smith_lang_slice_replay_close(at + 1, frame->end, &colon);
    if (close == UINT32_MAX || !smith_lang_slice_replay_named(replay, at, &type)) return false;
    if (type.dims.count != 1) {
        smith_lang_error(lang, host, place,
                         "a generic cannot index or cut a slice of more than one dimension yet: take a row first, "
                         "`xs[i]`, in the caller");
        return false;
    }
    if (replay->typing) { // only the type is asked of it, and the slice's struct is not one the compiler has read yet:
        char typed[640];  // an element is a value of the pointer's type, a cut a value of the slice's
        if (colon == UINT32_MAX) {
            snprintf(typed, sizeof typed, "( * ( ( %s ) 0 ) )", type.element);
        } else {
            snprintf(typed, sizeof typed, "( * ( ( struct %s * ) 0 ) )", type.tag);
        }
        SmithLangSlicePart parts[] = {{typed, 0, 0}};
        frame->pos = close + 1;
        smith_lang_slice_parts(replay, place, parts, 1);
        return true;
    }
    static unsigned counter;
    counter += 1;
    char condition[160];
    char check[512];
    char head[640];
    char tail[1024];
    frame->pos = close + 1;
    if (colon == UINT32_MAX) {
        snprintf(condition, sizeof condition, "smith_i%1$u < smith_s%1$u . count [ 0 ]", counter);
        smith_lang_slice_check(host, condition, check, sizeof check);
        snprintf(head, sizeof head, "( * __extension__ ( { auto smith_s%u = (", counter);
        snprintf(tail, sizeof tail, ") ; __SIZE_TYPE__ smith_i%1$u = (", counter);
        char end[1024];
        snprintf(end, sizeof end, ") ; %s & smith_s%u . data [ smith_i%u ] ; } ) )", check, counter, counter);
        SmithLangSlicePart parts[] = {
            {head, 0, 0}, {nullptr, at, at + 1}, {tail, 0, 0}, {nullptr, at + 2, close}, {end, 0, 0}};
        smith_lang_slice_parts(replay, place, parts, sizeof parts / sizeof parts[0]);
        return true;
    }
    return smith_lang_slice_replay_cut(host, replay, place, &type, (uint32_t[4]){at, at + 1, colon, close}, counter);
}

// The size of dimension `at` of a slice named `smith_s<counter>`, as an expression.
static void smith_lang_slice_replay_dim(const SmithLangSliceDims *dims, size_t at, unsigned counter, char *out,
                                        size_t capacity) {
    if (!dims->runtime[at]) {
        snprintf(out, capacity, "%llu", (unsigned long long)dims->fixed[at]);
        return;
    }
    size_t which = 0;
    for (size_t before = 0; before < at; before++) // at most 8
        which += dims->runtime[before] ? 1 : 0;
    snprintf(out, capacity, "smith_s%u . count [ %zu ]", counter, which);
}

// `E[i][j]` of a slice with an index for each of its dimensions, E the generic's tokens [span[0], span[1]), the
// indexes `at[k]` (open, close) pairs: the element, each index checked against its dimension (row-major, one block).
static void smith_lang_slice_replay_element(const SmithHost *host, SmithLangReplay *replay, uint32_t place,
                                            const SmithLangSliceDims *dims, const uint32_t span[2],
                                            const uint32_t (*at)[2]) {
    static unsigned counter;
    counter += 1;
    char condition[320] = "";
    char offset[320] = "";
    char names[smith_lang_slice_dims_max][32];
    size_t used = 0;
    for (size_t k = 0; k < dims->count; k++) { // each index
        char size[64];
        smith_lang_slice_replay_dim(dims, k, counter, size, sizeof size);
        snprintf(names[k], sizeof names[k], "smith_i%u_%zu", counter, k);
        used += (size_t)snprintf(condition + used, sizeof condition - used, "%s%s < %s", k > 0 ? " && " : "", names[k],
                                 size);
        if (k == 0) {
            snprintf(offset, sizeof offset, "%s", names[0]);
        } else {
            char next[320];
            snprintf(next, sizeof next, "( %s ) * %s + %s", offset, size, names[k]);
            snprintf(offset, sizeof offset, "%s", next);
        }
    }
    char check[480];
    smith_lang_slice_check(host, condition, check, sizeof check);
    char head[96];
    char between[smith_lang_slice_dims_max][96];
    char end[640];
    snprintf(head, sizeof head, "( * __extension__ ( { auto smith_s%u = (", counter);
    SmithLangSlicePart parts[2 + 2 * smith_lang_slice_dims_max + 1] = {{head, 0, 0}, {nullptr, span[0], span[1]}};
    size_t count = 2;
    for (size_t k = 0; k < dims->count; k++) {
        snprintf(between[k], sizeof between[k], ") ; __SIZE_TYPE__ %s = (", names[k]);
        parts[count++] = (SmithLangSlicePart){between[k], 0, 0};
        parts[count++] = (SmithLangSlicePart){nullptr, at[k][0] + 1, at[k][1]};
    }
    snprintf(end, sizeof end, ") ; %s & smith_s%u . data [ %s ] ; } ) )", check, counter, offset);
    parts[count++] = (SmithLangSlicePart){end, 0, 0};
    smith_lang_slice_parts(replay, place, parts, count);
}

static bool smith_lang_slice_replay_op(uint32_t at, const char *text) {
    return smith_lang_gt_punct(&smith_lang_gt.tokens[at], text);
}

// A member of a slice read by `.[F]` and subscripted, `x.[f][i]`, `p->[f][i][j]`, `x.[f][a:b]` in an instance's body
// (issue #125): the compiler has not read the instance, so the lowering writes the subscript as it does for a slice
// parameter. The object is `ident (. ident | -> ident)*` and the field is the instance's; a member that is not a slice
// (an array, a pointer) is C's own subscript, written as it is.
static bool smith_lang_slice_replay_member(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                           SmithLangFrame *frame, uint32_t place) {
    uint32_t first = frame->pos;
    uint32_t end = frame->end;
    uint32_t dot = first + 1;
    while (dot + 1 < end && smith_lang_gt.tokens[dot + 1].kind == SmithTokenKind_Identifier &&
           (smith_lang_slice_replay_op(dot, ".") || smith_lang_slice_replay_op(dot, "->")))
        dot += 2;
    if (dot + 3 >= end || !(smith_lang_slice_replay_op(dot, ".") || smith_lang_slice_replay_op(dot, "->")) ||
        !smith_lang_slice_replay_op(dot + 1, "[")) {
        return false;
    }
    uint32_t group = smith_lang_cx_skip_group(dot + 1, end);
    if (group == UINT32_MAX || group >= end || !smith_lang_slice_replay_op(group, "[")) return false;
    SmithLangFieldRef ref;
    if (smith_lang_fields_group(lang, host, replay, dot + 2, group - 1, place, &ref) != 1) return false;
    static const char tag[] = "struct smith_array_";
    if (strncmp(ref.info.type, tag, sizeof tag - 1) != 0) return false;
    SmithLangSliceType type = {.kind = SmithLangSlice_Slice};
    smith_lang_slice_type_tag(&type, ref.info.type + 7);
    char element[256];
    snprintf(element, sizeof element, "%s", ref.info.type);
    if (!smith_lang_fields_element(host, element, sizeof element)) return false;
    uint32_t indexes[smith_lang_slice_dims_max][2];
    uint32_t colon = UINT32_MAX;
    size_t taken = 0;
    uint32_t next = group;
    while (taken < type.dims.count && next < end && smith_lang_slice_replay_op(next, "[")) { // each index
        uint32_t close = smith_lang_slice_replay_close(next, end, &colon);
        if (close == UINT32_MAX || colon != UINT32_MAX) break;
        indexes[taken][0] = next;
        indexes[taken][1] = close;
        taken += 1;
        next = close + 1;
    }
    bool cut = colon != UINT32_MAX && taken == 0 && type.dims.count == 1;
    uint32_t close = taken == 0 ? smith_lang_slice_replay_close(group, end, &colon) : next - 1;
    if (close == UINT32_MAX || (taken != type.dims.count && !cut)) {
        smith_lang_error(lang, host, place,
                         "a generic cannot index or cut part of the rows of a slice member yet: index every "
                         "dimension, `x.[f][i][j]`, or take the row in the caller");
        smith_lang_generic_poison(lang, host, place);
        uint32_t skip = group;
        while (skip < end && smith_lang_slice_replay_op(skip, "[") && smith_lang_cx_skip_group(skip, end) != UINT32_MAX)
            skip = smith_lang_cx_skip_group(skip, end); // every subscript is taken, so that the error is the only one
        frame->pos = skip;
        return true;
    }
    frame->pos = close + 1;
    if (replay->typing) { // an element is a value of the pointer's type, a cut one of the slice's
        char typed[640];
        if (cut) {
            snprintf(typed, sizeof typed, "( * ( ( struct %s * ) 0 ) )", type.tag);
        } else {
            snprintf(typed, sizeof typed, "( * ( ( %s * ) 0 ) )", element);
        }
        SmithLangSlicePart part = {typed, 0, 0};
        smith_lang_slice_parts(replay, place, &part, 1);
        return true;
    }
    if (cut) {
        static unsigned counter = 1000000;
        counter += 1;
        return smith_lang_slice_replay_cut(host, replay, place, &type, (uint32_t[4]){first, group, colon, close},
                                           counter);
    }
    smith_lang_slice_replay_element(host, replay, place, &type.dims, (uint32_t[2]){first, group}, indexes);
    return true;
}

// The arguments of a call in an instance's body (issue #135): a slice parameter of the instance that is the whole
// argument and whose type differs from the callee's slice parameter by `const` or `restrict` is converted as an
// ordinary function's call converts it (`smith_lang_slice_requalify`: the same pointer and counts in the wanted
// struct); taking `const` away is the same error. The compiler has not read the instance's parameters, so only a bare
// name of one is typed (from the instance's own record); any other argument is written as it is. The callee is a name
// the compiler has declared at file scope, asked its type as an ordinary call is.
typedef struct {
    uint32_t bounds[smith_lang_slice_args_max][2]; // each argument's tokens [first, last)
    SmithLangSliceType types[smith_lang_slice_args_max];
    bool named[smith_lang_slice_args_max]; // the argument is the bare name of a slice parameter
    size_t count;
    bool any;
} SmithLangSliceReplayArgs;

// The arguments of the call whose `(` is at `open` (its group ends at `after`), split at the commas outside brackets.
static bool smith_lang_slice_replay_args(const SmithLangReplay *replay, uint32_t open, uint32_t after,
                                         SmithLangSliceReplayArgs *out) {
    *out = (SmithLangSliceReplayArgs){};
    uint32_t start = open + 1;
    int depth = 0;
    for (uint32_t i = open + 1; i < after; i++) {
        const SmithLangGenToken *token = &smith_lang_gt.tokens[i];
        if (i < after - 1) {
            depth += smith_lang_gt_open(token);
            depth -= smith_lang_gt_close(token);
            if (!(depth == 0 && smith_lang_gt_punct(token, ","))) continue;
        }
        if (out->count == smith_lang_slice_args_max) return false;
        size_t k = out->count++;
        out->bounds[k][0] = start;
        out->bounds[k][1] = i;
        const SmithLangGenToken *only = &smith_lang_gt.tokens[start];
        out->named[k] = i == start + 1 && only->kind == SmithTokenKind_Identifier &&
                        only->special == SmithLangGen_Plain && only->param == 0 &&
                        smith_lang_slice_replay_named(replay, start, &out->types[k]);
        out->any = out->any || out->named[k];
        start = i + 1;
    }
    return true;
}

// The words that write an argument of slice type `from` where the slice of `tag` is wanted: the two halves around the
// argument's own tokens (`texts[0]`, `texts[1]`); for taking `const` away the error, and a value of the type alone.
static bool smith_lang_slice_replay_words(SmithLang *lang, const SmithHost *host, uint32_t place, const char *from_tag,
                                          const char *tag, char (*texts)[640]) {
    if (smith_lang_slice_relation(from_tag, tag) == SmithLangSliceRelation_Discards) {
        char from[96];
        char to[96];
        smith_lang_slice_tag_words(from_tag, from, sizeof from);
        smith_lang_slice_tag_words(tag, to, sizeof to);
        smith_lang_error(
            lang, host, place,
            "converting a slice of `%s` to a slice of `%s` discards qualifiers: the elements are read-only", from, to);
        char definition[smith_lang_tuple_name_max + 200];
        smith_lang_slice_definition_text(lang, tag, definition, sizeof definition);
        snprintf(texts[0], 640, "__extension__ ( { %s ( struct %s ) { } ; } )", definition, tag); // nothing else
        return false;
    }
    static unsigned counter = 3000000;
    counter += 1;
    SmithLangSliceDims dims = smith_lang_slice_dims_one();
    size_t stem = 0;
    if (!smith_lang_slice_dims_parse(tag, &dims, &stem)) dims = smith_lang_slice_dims_one();
    char counts[160] = "";
    for (size_t d = 0; d < smith_lang_slice_dims_runtime(&dims); d++) {
        snprintf(counts + strlen(counts), sizeof counts - strlen(counts), "%ssmith_s%u . count [ %zu ]",
                 d > 0 ? " , " : "", counter, d);
    }
    char definition[smith_lang_tuple_name_max + 200];
    smith_lang_slice_definition_text(lang, tag, definition, sizeof definition);
    snprintf(texts[0], 640, "__extension__ ( { auto smith_s%u = (", counter);
    snprintf(texts[1], 640, ") ; %s ( struct %s ) { . data = smith_s%u . data , . count = { %s } } ; } )", definition,
             tag, counter, counts);
    return true;
}

// The call at the frame's position, written again with its converted arguments (the parts), or false when none is.
static bool smith_lang_slice_replay_write(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                          uint32_t place, const SmithLangSliceReplayArgs *args,
                                          char (*tags)[smith_lang_tuple_name_max], const uint32_t head[2]) {
    static SmithLangSlicePart parts[5 * smith_lang_slice_args_max + 2];
    static char texts[smith_lang_slice_args_max][2][640];
    size_t used = 0;
    parts[used++] = (SmithLangSlicePart){nullptr, head[0], head[0] + 2}; // the callee and `(`
    bool changed = false;
    for (size_t k = 0; k < args->count; k++) {
        bool convert = args->named[k] && tags[k][0] != 0 &&
                       smith_lang_slice_relation(args->types[k].tag, tags[k]) != SmithLangSliceRelation_Same &&
                       smith_lang_slice_relation(args->types[k].tag, tags[k]) != SmithLangSliceRelation_Other;
        if (k > 0) parts[used++] = (SmithLangSlicePart){",", 0, 0};
        if (!convert) {
            parts[used++] = (SmithLangSlicePart){nullptr, args->bounds[k][0], args->bounds[k][1]};
            continue;
        }
        changed = true;
        bool wraps = smith_lang_slice_replay_words(lang, host, place, args->types[k].tag, tags[k], texts[k]);
        parts[used++] = (SmithLangSlicePart){texts[k][0], 0, 0};
        if (!wraps) continue;
        parts[used++] = (SmithLangSlicePart){nullptr, args->bounds[k][0], args->bounds[k][1]};
        parts[used++] = (SmithLangSlicePart){texts[k][1], 0, 0};
    }
    parts[used++] = (SmithLangSlicePart){")", 0, 0};
    if (changed) smith_lang_slice_parts(replay, place, parts, used);
    return changed;
}

static bool smith_lang_slice_replay_call(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                         SmithLangFrame *frame, uint32_t place) {
    uint32_t at = frame->pos;
    if (replay->typing || host->expr_type == nullptr || at + 2 >= frame->end ||
        !smith_lang_gt_punct(&smith_lang_gt.tokens[at + 1], "(")) {
        return false;
    }
    uint32_t after = smith_lang_cx_skip_group(at + 1, frame->end);
    static SmithLangSliceReplayArgs args;
    if (after == UINT32_MAX || after > frame->end || !smith_lang_slice_replay_args(replay, at + 1, after, &args) ||
        !args.any) {
        return false;
    }
    char callee[smith_lang_name_max];
    const SmithLangGenToken *head = &smith_lang_gt.tokens[at];
    snprintf(callee, sizeof callee, "%.*s", (int)head->length, smith_lang_gt_str(head));
    SmithExprType found = {};
    static char tags[smith_lang_slice_args_max][smith_lang_tuple_name_max];
    if (!host->expr_type(host->context, callee, (uint32_t)strlen(callee), &found) ||
        smith_lang_slice_params(found.canonical, tags, smith_lang_slice_args_max) < args.count) {
        return false;
    }
    if (!smith_lang_slice_replay_write(lang, host, replay, place, &args, tags, (const uint32_t[2]){at, after})) {
        return false;
    }
    frame->pos = after;
    return true;
}

// The token at the frame's position starts `countof(xs)`, `xs[i]` or `xs[a:b]` of a slice parameter: write it as the
// lowering does and take its tokens. False when it is not one.
static bool smith_lang_slice_replay(SmithLang *lang, const SmithHost *host, SmithLangReplay *replay,
                                    SmithLangFrame *frame) {
    const SmithLangGenToken *token = &smith_lang_gt.tokens[frame->pos];
    if (replay->instance == nullptr || replay->generic == nullptr || token->kind != SmithTokenKind_Identifier ||
        token->special != SmithLangGen_Plain || token->param != 0 || smith_lang_member_before(frame->pos) ||
        replay->count + 24 >= smith_lang_replay_depth_max) {
        return false;
    }
    uint32_t place = smith_lang_gen_place(host, replay->generic, (SmithLangGenToken *)token, replay->location);
    if (smith_lang_slice_replay_member(lang, host, replay, frame, place)) return true;
    if (smith_lang_slice_replay_local(lang, replay, frame, place)) return true;
    if (replay->instance->slices.length == 0 && smith_lang_slice_locals.instance != replay->instance_id) return false;
    if (smith_lang_slice_replay_call(lang, host, replay, frame, place)) return true;
    if (smith_lang_gt_is(token, SmithTokenKind_Identifier, "countof")) {
        return smith_lang_slice_replay_countof(replay, place, frame);
    }
    return smith_lang_slice_replay_subscript(lang, host, replay, place, frame);
}
