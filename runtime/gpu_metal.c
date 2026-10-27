// The Metal backend of a GPU kernel (issue #18, knowledge/gpu-metal.md): `smith cc --smith-metal kernel.smith -o
// k.metal` writes Metal source for every function of the file whose first parameter is `gpu::Thread *`, and
// `-o k.metallib` also builds it. The lowering of the file (the plain lexer of runtime/lang_lex.c, the text
// `--smith-lower` prints) is C; the functions of a kernel are in the subset that is the same in Metal Shading Language
// (C types, arithmetic, `if`, `for`, `float` math), so a body is copied with its names mapped (`__UINT32_TYPE__` is
// `uint`, `tanhf` is `tanh`), and the signature is the part that is written: each pointer parameter is a `device`
// buffer (a `constant` one for a `const` pointer to a struct, the uniforms) and each other parameter a `constant`
// value, numbered in order, and the entry point makes the thread. What a kernel calls is written too, once: the
// functions of the file it reaches, the structs and constants they name. Included by smith.c after
// runtime/lang_lex.c.

static int smith_spawn_with(const char *program, char *const *args, const posix_spawn_file_actions_t *actions);

constexpr size_t smith_metal_params_max = 16;
constexpr size_t smith_metal_items_max = 512;
constexpr size_t smith_metal_shared_max = 8;

// A name of the lowered text and its Metal spelling.
typedef struct {
    const char *c;
    const char *metal;
} SmithMetalName;

// The sized types as the lowering writes them, and the float functions that Metal spells without the `f`.
static const SmithMetalName smith_metal_names[] = {
    {"__INT8_TYPE__", "char"},
    {"__UINT8_TYPE__", "uchar"},
    {"__INT16_TYPE__", "short"},
    {"__UINT16_TYPE__", "ushort"},
    {"__INT32_TYPE__", "int"},
    {"__UINT32_TYPE__", "uint"},
    {"__INT64_TYPE__", "long"},
    {"__UINT64_TYPE__", "ulong"},
    {"__SIZE_TYPE__", "ulong"},
    {"_Bool", "bool"},
    {"tanhf", "tanh"},
    {"expf", "exp"},
    {"exp2f", "exp2"},
    {"logf", "log"},
    {"log2f", "log2"},
    {"sinf", "sin"},
    {"cosf", "cos"},
    {"tanf", "tan"},
    {"asinf", "asin"},
    {"acosf", "acos"},
    {"atanf", "atan"},
    {"atan2f", "atan2"},
    {"sinhf", "sinh"},
    {"coshf", "cosh"},
    {"sqrtf", "sqrt"},
    {"fabsf", "fabs"},
    {"fminf", "fmin"},
    {"fmaxf", "fmax"},
    {"fmaf", "fma"},
    {"fmodf", "fmod"},
    {"copysignf", "copysign"},
    {"powf", "pow"},
    {"floorf", "floor"},
    {"ceilf", "ceil"},
    {"roundf", "round"},
    {"truncf", "trunc"},
};

// What a kernel may call from smith::gpu, and the thread it runs as (the fields of lib/smith/gpu.smith, in its order).
constexpr char smith_metal_prelude[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n\n"
    "struct smith_gpu_Thread {\n"
    "    uint index;\n    uint index_y;\n    uint local;\n    uint local_y;\n    uint group;\n    uint group_y;\n"
    "    uint groups;\n    uint groups_y;\n    uint lane;\n    uint simd;\n    uint simds;\n};\n\n"
    "[[maybe_unused]] static void smith_gpu_barrier(thread smith_gpu_Thread *) {\n"
    "    threadgroup_barrier(mem_flags::mem_threadgroup);\n}\n\n"
    "[[maybe_unused]] static float smith_gpu_simd_sum(thread smith_gpu_Thread *, float x) {\n    return "
    "simd_sum(x);\n}\n\n";

typedef struct {
    const char *text;
    size_t length;
} SmithMetalSpan;

typedef struct {
    SmithMetalSpan type; // everything before the name, `const float *`
    SmithMetalSpan name;
    bool pointer;
} SmithMetalParam;

typedef enum {
    SmithMetalItem_Constant,
    SmithMetalItem_Struct,
    SmithMetalItem_Function,
    SmithMetalItem_Kernel,
} SmithMetalKind;

// One declaration of the file that Metal may need: a `constexpr`, a struct or a function with a body.
typedef struct {
    SmithMetalKind kind;
    SmithMetalSpan head; // a constexpr: the statement; a struct: `struct Name`; a function: the text before its body
    SmithMetalSpan body; // the `{...}` of a struct or function
    SmithMetalSpan name;
    bool used; // a kernel, or named by something used
} SmithMetalItem;

// A threadgroup array that `gpu::shared(t, T, n)` asked for in a kernel: Metal declares it at the top of the kernel.
typedef struct {
    SmithMetalSpan type;
    SmithMetalSpan count;
} SmithMetalShared;

static SmithMetalItem smith_metal_items[smith_metal_items_max];
static size_t smith_metal_item_count;
static bool smith_metal_failed;

static bool smith_metal_word_char(char c) {
    return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool smith_metal_space_char(char c) {
    return c == ' ' || c == '\n' || c == '\t';
}

static bool smith_metal_is(SmithMetalSpan span, const char *word) {
    return strlen(word) == span.length && strncmp(span.text, word, span.length) == 0;
}

// Writes text[0..length) with each name of the table replaced.
static void smith_metal_write(FILE *out, const char *text, size_t length) {
    for (size_t i = 0; i < length;) { // one step per word or character
        if (!smith_metal_word_char(text[i])) {
            fputc(text[i++], out);
            continue;
        }
        size_t end = i;
        while (end < length && smith_metal_word_char(text[end]))
            end++;
        const char *spelling = nullptr;
        for (size_t k = 0; k < sizeof smith_metal_names / sizeof smith_metal_names[0]; k++) { // bounded by the table
            const SmithMetalName *entry = &smith_metal_names[k];
            if (strlen(entry->c) == end - i && strncmp(entry->c, text + i, end - i) == 0) spelling = entry->metal;
        }
        if (spelling != nullptr) {
            fputs(spelling, out);
        } else {
            fwrite(text + i, 1, end - i, out);
        }
        i = end;
    }
}

static void smith_metal_put(FILE *out, SmithMetalSpan span) {
    smith_metal_write(out, span.text, span.length);
}

// Whether `word` is one of the words of `haystack`.
static bool smith_metal_mentions(SmithMetalSpan haystack, SmithMetalSpan word) {
    for (size_t i = 0; i < haystack.length;) { // one step per word or character
        if (!smith_metal_word_char(haystack.text[i])) {
            i++;
            continue;
        }
        size_t end = i;
        while (end < haystack.length && smith_metal_word_char(haystack.text[end]))
            end++;
        if (end - i == word.length && strncmp(haystack.text + i, word.text, word.length) == 0) return true;
        i = end;
    }
    return false;
}

// The end of a comment, string or character literal that starts at text[i], or i when none does.
static size_t smith_metal_skip_literal(const char *text, size_t length, size_t i) {
    char c = text[i];
    if (c == '/' && i + 1 < length && text[i + 1] == '/') {
        while (i < length && text[i] != '\n')
            i++;
        return i;
    }
    if (c == '/' && i + 1 < length && text[i + 1] == '*') {
        for (i += 2; i + 1 < length; i++) { // bounded by the text
            if (text[i] == '*' && text[i + 1] == '/') return i + 2;
        }
        return length;
    }
    if (c != '"' && c != '\'') return i;
    for (i++; i < length && text[i] != c; i++) { // bounded by the text
        if (text[i] == '\\') i++;
    }
    return i < length ? i + 1 : length;
}

// The index just after the `}` that closes the `{` at text[open].
static size_t smith_metal_close(const char *text, size_t length, size_t open) {
    size_t depth = 0;
    for (size_t i = open; i < length;) { // one step per character or literal
        size_t after = smith_metal_skip_literal(text, length, i);
        if (after != i) {
            i = after;
            continue;
        }
        depth += text[i] == '{' ? 1 : 0;
        if (text[i] == '}' && --depth == 0) return i + 1;
        i++;
    }
    return length;
}

// The `)` that closes the `(` at open, inside [open, end).
static const char *smith_metal_close_paren(const char *open, const char *end) {
    size_t depth = 0;
    for (const char *p = open; p < end; p++) { // bounded by the head
        depth += *p == '(' ? 1 : 0;
        if (*p == ')' && --depth == 0) return p;
    }
    return nullptr;
}

// Splits a parameter list on its commas; false for more parameters than a kernel takes.
static bool smith_metal_params(SmithMetalSpan list, SmithMetalParam *params, size_t *count) {
    *count = 0;
    size_t start = 0;
    for (size_t i = 0; i <= list.length; i++) { // bounded by the list
        if (i < list.length && list.text[i] != ',') continue;
        size_t end = i;
        while (end > start && list.text[end - 1] == ' ')
            end--;
        while (start < end && list.text[start] == ' ')
            start++;
        if (end == start) continue;
        const char *attribute = nullptr;             // `_t __attribute__((unused))`: the lowering of a `_` name
        for (size_t k = start; k + 13 <= end; k++) { // bounded by the parameter
            if (attribute == nullptr && strncmp(list.text + k, "__attribute__", 13) == 0) attribute = list.text + k;
        }
        end = attribute != nullptr ? (size_t)(attribute - list.text) : end;
        while (end > start && list.text[end - 1] == ' ')
            end--;
        if (*count == smith_metal_params_max) return false;
        size_t name = end;
        while (name > start && smith_metal_word_char(list.text[name - 1]))
            name--;
        SmithMetalParam *param = &params[(*count)++];
        param->name = (SmithMetalSpan){list.text + name, end - name};
        param->type = (SmithMetalSpan){list.text + start, name - start};
        param->pointer = memchr(param->type.text, '*', param->type.length) != nullptr;
        start = i + 1;
    }
    return true;
}

// A type the lowering spells with a keyword or a sized-type macro: not a struct.
static bool smith_metal_scalar(SmithMetalSpan word) {
    static const char *const scalars[] = {"float", "half",  "int",  "char",     "short",  "long",
                                          "bool",  "_Bool", "void", "unsigned", "signed", "double"};
    for (size_t i = 0; i < sizeof scalars / sizeof scalars[0]; i++) { // bounded by the table
        if (smith_metal_is(word, scalars[i])) return true;
    }
    return word.length > 4 && word.text[0] == '_' && word.text[1] == '_' && word.text[word.length - 1] == '_' &&
           word.text[word.length - 2] == '_';
}

// The address space of a pointer parameter: the thread itself is `thread`, a `const` pointer to a struct (the
// uniforms) is `constant`, any other pointer is a `device` buffer.
static const char *smith_metal_space(SmithMetalSpan type) {
    const char *star = memchr(type.text, '*', type.length);
    SmithMetalSpan before = {type.text, star != nullptr ? (size_t)(star - type.text) : type.length};
    bool is_const = smith_metal_mentions(before, (SmithMetalSpan){"const", 5});
    size_t end = before.length;
    while (end > 0 && !smith_metal_word_char(before.text[end - 1]))
        end--;
    size_t start = end;
    while (start > 0 && smith_metal_word_char(before.text[start - 1]))
        start--;
    SmithMetalSpan base = {before.text + start, end - start};
    if (smith_metal_is(base, "smith_gpu_Thread")) return "thread";
    return is_const && !smith_metal_scalar(base) ? "constant" : "device";
}

// One parameter as a helper function takes it: a pointer in its address space, the rest as values.
static void smith_metal_write_param(FILE *out, const SmithMetalParam *param) {
    if (param->pointer) fprintf(out, "%s ", smith_metal_space(param->type));
    smith_metal_put(out, param->type);
    fwrite(param->name.text, 1, param->name.length, out);
}

// If the statement at text[at] declares a pointer, `const float *row = ...;`, this is true with the name and the
// index after the `=`. Metal wants an address space on the type of such a variable, and the initializer knows it, so
// the statement is written with `auto`.
static bool smith_metal_pointer_declaration(const char *text, size_t at, size_t end, SmithMetalSpan *name,
                                            size_t *init) {
    size_t before = 0;
    size_t after = 0;
    bool star = false;
    size_t i = at;
    while (i < end) { // one step per word or character
        if (smith_metal_space_char(text[i])) {
            i++;
        } else if (text[i] == '*' && before > 0) {
            star = true;
            after = 0;
            i++;
        } else if (smith_metal_word_char(text[i])) {
            size_t start = i;
            while (i < end && smith_metal_word_char(text[i]))
                i++;
            SmithMetalSpan word = {text + start, i - start};
            if (before == 0 && (smith_metal_is(word, "return") || smith_metal_is(word, "else"))) return false;
            before += star ? 0 : 1;
            after += star ? 1 : 0;
            *name = word;
        } else {
            break;
        }
    }
    if (!star || after != 1 || i >= end || text[i] != '=' || (i + 1 < end && text[i + 1] == '=')) return false;
    *init = i + 1;
    return true;
}

// The index after the `)` that closes the `(` at text[open], skipping literals.
static size_t smith_metal_close_paren_at(const char *text, size_t length, size_t open) {
    size_t depth = 0;
    for (size_t i = open; i < length;) { // one step per character or literal
        size_t after = smith_metal_skip_literal(text, length, i);
        if (after != i) {
            i = after;
            continue;
        }
        depth += text[i] == '(' ? 1 : 0;
        if (text[i] == ')' && --depth == 0) return i + 1;
        i++;
    }
    return length;
}

// The last occurrence of a word that starts with `prefix` in text[from, to), or `to` when there is none.
static size_t smith_metal_find_prefix(const char *text, size_t from, size_t to, const char *prefix, bool last) {
    size_t width = strlen(prefix);
    size_t found = to;
    for (size_t i = from; i + width <= to; i++) { // bounded by the span
        bool start = i == from || !smith_metal_word_char(text[i - 1]);
        if (start && strncmp(text + i, prefix, width) == 0) {
            found = i;
            if (!last) return found;
        }
    }
    return found;
}

static SmithMetalSpan smith_metal_trim(const char *text, size_t from, size_t to) {
    while (from < to && smith_metal_space_char(text[from]))
        from++;
    while (to > from && smith_metal_space_char(text[to - 1]))
        to--;
    return (SmithMetalSpan){text + from, to - from};
}

// `gpu::shared(t, T, n)` is lowered to a statement expression that declares the instance and calls it,
// `__extension__({extern T *smith_gpu_shared__f32(...)__asm__("...");smith_gpu_shared__f32(t,n);})`, which starts at
// text[at]. It becomes the name of a threadgroup array of n values of T, which the kernel declares first. Returns the
// index after the expression, or `at` when it is some other generic's.
static size_t smith_metal_shared_expression(FILE *out, const char *text, size_t length, size_t at,
                                            SmithMetalShared *shared, size_t *count) {
    size_t open = at + strlen("__extension__");
    while (open < length && smith_metal_space_char(text[open]))
        open++;
    if (open >= length || text[open] != '(') return at;
    size_t end = smith_metal_close_paren_at(text, length, open);
    static const char instance[] = "smith_gpu_shared__";
    size_t declared = smith_metal_find_prefix(text, open, end, instance, false);
    size_t called = smith_metal_find_prefix(text, open, end, instance, true);
    size_t keyword = smith_metal_find_prefix(text, open, end, "extern", false);
    if (declared == end || keyword >= declared) return at;
    SmithMetalSpan type = smith_metal_trim(text, keyword + strlen("extern"), declared);
    while (type.length > 0 && type.text[type.length - 1] == '*')
        type.length--;
    type = smith_metal_trim(type.text, 0, type.length);
    size_t args = called;
    while (args < end && text[args] != '(')
        args++;
    size_t comma = args;
    while (comma < end && text[comma] != ',')
        comma++;
    size_t closed = smith_metal_close_paren_at(text, end, args);
    SmithMetalSpan size = smith_metal_trim(text, comma + 1, closed > 0 ? closed - 1 : closed);
    if (shared == nullptr || *count >= smith_metal_shared_max || comma == end) {
        fprintf(stderr, "gpu::shared(t, T, n) is for the body of a kernel, at most %zu of them\n",
                smith_metal_shared_max);
        smith_metal_failed = true;
        return end;
    }
    shared[*count] = (SmithMetalShared){type, size};
    fprintf(out, "smith_shared_%zu", (*count)++);
    return end;
}

// The call `smith_gpu_shared(t, T, n)` whose `(` is at text[open], as the raw lowering leaves it when it has not read
// the header of smith::gpu (no -I for it): the same threadgroup array. Returns the index after the call.
static size_t smith_metal_shared_call(FILE *out, const char *text, size_t length, size_t open, SmithMetalShared *shared,
                                      size_t *count) {
    size_t end = smith_metal_close_paren_at(text, length, open);
    size_t first = open;
    while (first < end && text[first] != ',')
        first++;
    size_t second = first + 1;
    while (second < end && text[second] != ',')
        second++;
    if (second >= end || shared == nullptr || *count >= smith_metal_shared_max) {
        fprintf(stderr, "gpu::shared(t, T, n) is for the body of a kernel, at most %zu of them\n",
                smith_metal_shared_max);
        smith_metal_failed = true;
        return end;
    }
    shared[*count] =
        (SmithMetalShared){smith_metal_trim(text, first + 1, second), smith_metal_trim(text, second + 1, end - 1)};
    fprintf(out, "smith_shared_%zu", (*count)++);
    return end;
}

// Writes the body of a kernel or helper, with names mapped, a pointer variable declared `auto` and each
// `gpu::shared` call replaced by its threadgroup array (recorded in `shared`).
static void smith_metal_body(FILE *out, SmithMetalSpan body, SmithMetalShared *shared, size_t *shared_count) {
    const char *text = body.text;
    bool statement = false;
    for (size_t i = 0; i < body.length;) { // one step per word, literal or character
        size_t after = smith_metal_skip_literal(text, body.length, i);
        char c = text[i];
        if (after != i || c == '{' || c == '}' || c == ';' || smith_metal_space_char(c)) {
            size_t to = after != i ? after : i + 1;
            fwrite(text + i, 1, to - i, out);
            statement = after == i && !smith_metal_space_char(c) ? true : statement;
            i = to;
            continue;
        }
        SmithMetalSpan name;
        size_t init = 0;
        if (statement && smith_metal_pointer_declaration(text, i, body.length, &name, &init)) {
            fputs("auto ", out);
            fwrite(name.text, 1, name.length, out);
            fputs(" =", out);
            i = init;
            statement = false;
            continue;
        }
        statement = false;
        size_t end = i;
        while (end < body.length && smith_metal_word_char(text[end]))
            end++;
        size_t open = end;
        while (open < body.length && smith_metal_space_char(text[open]))
            open++;
        size_t taken = i;
        if (end - i == 13 && strncmp(text + i, "__extension__", 13) == 0)
            taken = smith_metal_shared_expression(out, text, body.length, i, shared, shared_count);
        if (end - i == 16 && strncmp(text + i, "smith_gpu_shared", 16) == 0 && open < body.length && text[open] == '(')
            taken = smith_metal_shared_call(out, text, body.length, open, shared, shared_count);
        if (taken != i) {
            i = taken;
        } else {
            size_t width = end > i ? end - i : 1;
            smith_metal_write(out, text + i, width);
            i += width;
        }
    }
}

// The entry point's arguments that come from the thread's place, and the thread made from them.
constexpr char smith_metal_places[] =
    "uint2 smith_position [[thread_position_in_grid]], uint2 smith_local [[thread_position_in_threadgroup]], "
    "uint2 smith_group [[threadgroup_position_in_grid]], uint2 smith_groups [[threadgroups_per_grid]], "
    "uint smith_lane [[thread_index_in_simdgroup]], uint smith_simd [[simdgroup_index_in_threadgroup]], "
    "uint smith_simds [[simdgroups_per_threadgroup]]";
constexpr char smith_metal_make_thread[] =
    "    smith_gpu_Thread smith_thread = {smith_position.x, smith_position.y, smith_local.x, smith_local.y, "
    "smith_group.x, smith_group.y, smith_groups.x, smith_groups.y, smith_lane, smith_simd, smith_simds};\n";

// The kernel's parameters as the entry point takes them, after the places: a pointer is a buffer in its address
// space, any other parameter a `constant` value, numbered from 0 in order after the thread.
static void smith_metal_entry_params(FILE *out, const SmithMetalParam *params, size_t count) {
    for (size_t i = 1; i < count; i++) { // bounded by the parameters
        fputs(", ", out);
        if (params[i].pointer) {
            smith_metal_write_param(out, &params[i]);
        } else {
            fputs("constant ", out);
            smith_metal_put(out, params[i].type);
            fprintf(out, "&%.*s", (int)params[i].name.length, params[i].name.text);
        }
        fprintf(out, " [[buffer(%zu)]]", i - 1);
    }
}

// Writes the kernel whose head is item->head and whose body is item->body: the entry point, with the body in it.
static void smith_metal_kernel(FILE *out, const SmithMetalItem *item) {
    const char *open = memchr(item->head.text, '(', item->head.length);
    const char *close = smith_metal_close_paren(open, item->head.text + item->head.length);
    SmithMetalParam params[smith_metal_params_max];
    size_t count = 0;
    SmithMetalShared shared[smith_metal_shared_max];
    size_t shared_count = 0;
    SmithMetalSpan returns = smith_metal_trim(item->head.text, 0, (size_t)(item->name.text - item->head.text));
    if (!(returns.length >= 4 && strncmp(returns.text + returns.length - 4, "void", 4) == 0)) {
        fprintf(stderr, "%.*s: a kernel returns void, and a function that takes a `gpu::Thread *` is a kernel\n",
                (int)item->name.length, item->name.text);
        smith_metal_failed = true;
        return;
    }
    if (!smith_metal_params((SmithMetalSpan){open + 1, (size_t)(close - open - 1)}, params, &count)) {
        fprintf(stderr, "%.*s: a kernel takes at most %zu parameters\n", (int)item->name.length, item->name.text,
                smith_metal_params_max);
        smith_metal_failed = true;
        return;
    }
    FILE *sink = fopen("/dev/null", "w"); // the first pass finds the shared arrays, which are declared first
    if (sink == nullptr) return;
    smith_metal_body(sink, item->body, shared, &shared_count);
    fclose(sink);
    fprintf(out, "kernel void %.*s(%s", (int)item->name.length, item->name.text, smith_metal_places);
    smith_metal_entry_params(out, params, count);
    fprintf(out, ") {\n%s    thread smith_gpu_Thread *%.*s = &smith_thread;\n", smith_metal_make_thread,
            (int)params[0].name.length, params[0].name.text);
    for (size_t i = 0; i < shared_count; i++) { // bounded by the arrays
        fputs("    threadgroup ", out);
        smith_metal_put(out, shared[i].type);
        fprintf(out, " smith_shared_%zu[", i);
        smith_metal_put(out, shared[i].count);
        fputs("];\n", out);
    }
    shared_count = 0;
    smith_metal_body(out, item->body, shared, &shared_count);
    fputs("\n}\n\n", out);
}

// A helper function: its pointers are in address spaces, its body is written as a kernel's, and it makes no
// threadgroup memory of its own (Metal declares that in a kernel).
static void smith_metal_helper(FILE *out, const SmithMetalItem *item) {
    const char *open = memchr(item->head.text, '(', item->head.length);
    const char *close = smith_metal_close_paren(open, item->head.text + item->head.length);
    SmithMetalParam params[smith_metal_params_max];
    size_t count = 0;
    if (!smith_metal_params((SmithMetalSpan){open + 1, (size_t)(close - open - 1)}, params, &count)) {
        fprintf(stderr, "%.*s: a function takes at most %zu parameters\n", (int)item->name.length, item->name.text,
                smith_metal_params_max);
        smith_metal_failed = true;
        return;
    }
    smith_metal_write(out, item->head.text, (size_t)(open - item->head.text));
    fputc('(', out);
    for (size_t i = 0; i < count; i++) { // bounded by the parameters
        fputs(i > 0 ? ", " : "", out);
        smith_metal_write_param(out, &params[i]);
    }
    fputs(") ", out);
    smith_metal_body(out, item->body, nullptr, nullptr);
    fputs("\n\n", out);
}

// The name after `struct ` in a head, or the word before the `;` after a body for `typedef struct {...} Name;`.
static SmithMetalSpan smith_metal_struct_name(const char *text, size_t length, SmithMetalSpan head, size_t body_end) {
    size_t end = head.length;
    while (end > 0 && smith_metal_space_char(head.text[end - 1]))
        end--;
    size_t start = end;
    while (start > 0 && smith_metal_word_char(head.text[start - 1]))
        start--;
    SmithMetalSpan word = {head.text + start, end - start};
    if (!smith_metal_is(word, "struct")) return word;
    size_t semi = body_end;
    while (semi < length && text[semi] != ';')
        semi++;
    size_t to = semi;
    while (to > body_end && smith_metal_space_char(text[to - 1]))
        to--;
    size_t from = to;
    while (from > body_end && smith_metal_word_char(text[from - 1]))
        from--;
    return (SmithMetalSpan){text + from, to - from};
}

// Records the declaration whose head is text[head..brace) and whose body ends at `end`, if Metal can need it.
static void smith_metal_item(const char *text, size_t length, size_t head, size_t brace, size_t end) {
    if (smith_metal_item_count == smith_metal_items_max) return;
    SmithMetalItem *item = &smith_metal_items[smith_metal_item_count];
    item->head = (SmithMetalSpan){text + head, brace - head};
    item->body = (SmithMetalSpan){text + brace, end - brace};
    item->used = false;
    const char *open = memchr(item->head.text, '(', item->head.length);
    static const char struct_word[] = "struct ";
    static const char typedef_word[] = "typedef struct";
    bool plain = item->head.length >= sizeof struct_word - 1 &&
                 strncmp(item->head.text, struct_word, sizeof struct_word - 1) == 0;
    bool typed = item->head.length >= sizeof typedef_word - 1 &&
                 strncmp(item->head.text, typedef_word, sizeof typedef_word - 1) == 0;
    if (open == nullptr && (plain || typed)) {
        item->kind = SmithMetalItem_Struct;
        item->name = smith_metal_struct_name(text, length, item->head, end);
    } else if (open != nullptr) {
        size_t name_end = (size_t)(open - item->head.text);
        while (name_end > 0 && item->head.text[name_end - 1] == ' ')
            name_end--;
        size_t name = name_end;
        while (name > 0 && smith_metal_word_char(item->head.text[name - 1]))
            name--;
        item->name = (SmithMetalSpan){item->head.text + name, name_end - name};
        static const char thread[] = "smith_gpu_Thread *";
        const char *close = smith_metal_close_paren(open, item->head.text + item->head.length);
        bool kernel = close != nullptr && (size_t)(close - open) > sizeof thread &&
                      strncmp(open + 1, thread, sizeof thread - 1) == 0;
        item->kind = kernel ? SmithMetalItem_Kernel : SmithMetalItem_Function;
        item->used = kernel;
    } else {
        return;
    }
    smith_metal_item_count++;
}

// A file-scope `static constexpr float k = 1.0f;` is a `constant` of the Metal file (a kernel may use it).
static void smith_metal_constant(const char *statement, size_t length) {
    static const char prefix[] = "static constexpr ";
    while (length > 0 && smith_metal_space_char(statement[0])) {
        statement++;
        length--;
    }
    if (length <= sizeof prefix || strncmp(statement, prefix, sizeof prefix - 1) != 0) return;
    if (smith_metal_item_count == smith_metal_items_max) return;
    const char *equals = memchr(statement, '=', length);
    if (equals == nullptr) return;
    size_t end = (size_t)(equals - statement);
    while (end > 0 && statement[end - 1] == ' ')
        end--;
    size_t start = end;
    while (start > 0 && smith_metal_word_char(statement[start - 1]))
        start--;
    SmithMetalItem *item = &smith_metal_items[smith_metal_item_count++];
    item->kind = SmithMetalItem_Constant;
    item->head = (SmithMetalSpan){statement + sizeof prefix - 1, length - (sizeof prefix - 1)};
    item->body = (SmithMetalSpan){statement, 0};
    item->name = (SmithMetalSpan){statement + start, end - start};
    item->used = false;
}

// Reads the declarations of the lowered text into the table.
static void smith_metal_collect(const char *text, size_t length) {
    smith_metal_item_count = 0;
    size_t statement = 0;
    for (size_t i = 0; i < length;) { // one step per character or literal
        size_t after = smith_metal_skip_literal(text, length, i);
        if (after != i) {
            i = after;
        } else if (text[i] == '#' && (i == 0 || text[i - 1] == '\n')) {
            while (i < length && text[i] != '\n')
                i++;
            statement = i;
        } else if (text[i] == ';') {
            smith_metal_constant(text + statement, i + 1 - statement);
            statement = ++i;
        } else if (text[i] == '{') {
            size_t end = smith_metal_close(text, length, i);
            while (statement < i && smith_metal_space_char(text[statement]))
                statement++;
            smith_metal_item(text, length, statement, i, end);
            i = statement = end;
        } else {
            i++;
        }
    }
}

// Marks what the kernels reach: a helper, struct or constant that a used item names, and so on.
static void smith_metal_mark(void) {
    bool changed = true;
    for (size_t pass = 0; changed && pass <= smith_metal_item_count; pass++) { // each pass marks one more or ends
        changed = false;
        for (size_t i = 0; i < smith_metal_item_count; i++) { // bounded by the table
            if (!smith_metal_items[i].used) continue;
            for (size_t j = 0; j < smith_metal_item_count; j++) { // bounded by the table
                SmithMetalItem *other = &smith_metal_items[j];
                bool named = smith_metal_mentions(smith_metal_items[i].head, other->name) ||
                             smith_metal_mentions(smith_metal_items[i].body, other->name);
                if (i != j && !other->used && named) changed = other->used = true;
            }
        }
    }
}

static void smith_metal_emit(FILE *out, const SmithMetalItem *item) {
    if (item->kind == SmithMetalItem_Constant) {
        fputs("constant ", out);
        smith_metal_put(out, item->head);
        fputs("\n", out);
    } else if (item->kind == SmithMetalItem_Struct) {
        fprintf(out, "struct %.*s ", (int)item->name.length, item->name.text);
        smith_metal_put(out, item->body);
        fputs(";\n\n", out);
    } else if (item->kind == SmithMetalItem_Function) {
        smith_metal_helper(out, item);
    } else {
        smith_metal_kernel(out, item);
    }
}

// Writes the Metal source of every kernel in the lowered text, after the prelude; the number of kernels.
static size_t smith_metal_source(FILE *out, const char *text, size_t length) {
    fputs(smith_metal_prelude, out);
    smith_metal_collect(text, length);
    smith_metal_mark();
    size_t kernels = 0;
    for (size_t i = 0; i < smith_metal_item_count; i++) { // bounded by the table
        if (!smith_metal_items[i].used) continue;
        smith_metal_emit(out, &smith_metal_items[i]);
        kernels += smith_metal_items[i].kind == SmithMetalItem_Kernel ? 1 : 0;
    }
    return kernels;
}

// What `--smith-lower` would print, taken by `--smith-metal` instead (runtime/lang_lex.c, smith_lang_dump_sink).
static FILE *smith_metal_out;
static size_t smith_metal_kernels;
static FILE *smith_metal_table;      // the table of kernels (<library>.kernels.c), when a library is built
static const char *smith_metal_stem; // the name of the library without its folder and suffix

// The label of the symbol of the function `name` in the lowered text: the string of the `__asm__("...")` that follows
// its first declaration (`_elementwise::gelu_kernel`), or null.
static const char *smith_metal_label(const char *text, size_t length, SmithMetalSpan name, size_t *size) {
    for (size_t i = 0; i + name.length < length; i++) { // bounded by the text
        if (strncmp(text + i, name.text, name.length) != 0 || text[i + name.length] != '(') continue;
        if (i > 0 && smith_metal_word_char(text[i - 1])) continue;
        const char *label = strstr(text + i, "__asm__(\"");
        const char *end = label != nullptr ? strchr(label + 9, '"') : nullptr;
        if (end == nullptr) return nullptr;
        *size = (size_t)(end - (label + 9));
        return label + 9;
    }
    return nullptr;
}

// The table of the kernels the program finds by their address (knowledge/gpu-metal.md): for each kernel its function,
// its name in the Metal library and the library's name, in a section the program reads whatever its symbols are
// (stripped, LTO). The table is C, compiled and linked into the program by smith_program.
static void smith_metal_write_table(const char *text, size_t length) {
    fprintf(smith_metal_table,
            "// The kernels of %s: the address of each function, its name in the Metal library and the library's.\n"
            "// Written by `smith cc --smith-metal` (a build output), read by smith::gpu.\n"
            "typedef struct {\n    const void *function;\n    const char *name;\n    const char *library;\n} "
            "SmithGpuKernel;\n\n",
            smith_metal_stem);
    size_t number = 0;
    for (size_t i = 0; i < smith_metal_item_count; i++) { // bounded by the table
        const SmithMetalItem *item = &smith_metal_items[i];
        size_t size = 0;
        const char *label =
            item->kind == SmithMetalItem_Kernel ? smith_metal_label(text, length, item->name, &size) : nullptr;
        if (label == nullptr) continue;
        fprintf(smith_metal_table,
                "extern void smith_gpu_function_%zu(void) __asm__(\"%.*s\");\n"
                "[[gnu::used, gnu::retain, gnu::section(\"__DATA,__smith_gpu\")]] static const SmithGpuKernel "
                "smith_gpu_kernel_%zu = {(const void *)smith_gpu_function_%zu, \"%.*s\", \"%s\"};\n",
                number, (int)size, label, number, number, (int)item->name.length, item->name.text, smith_metal_stem);
        number++;
    }
}

static void smith_metal_sink(const char *text, size_t length) {
    smith_metal_kernels = smith_metal_source(smith_metal_out, text, length);
    if (smith_metal_table != nullptr) smith_metal_write_table(text, length);
}

static bool smith_metal_has_suffix(const char *text, const char *suffix) {
    size_t length = strlen(text);
    size_t tail = strlen(suffix);
    return length >= tail && strcmp(text + length - tail, suffix) == 0;
}

// Whether Apple's `metal` resolves (`xcrun -f metal`): the MetalToolchain is a separate download.
static bool smith_metal_toolchain(const char *self) {
    char xcrun[PATH_MAX];
    if (!smith_find_in_path("xcrun", self, xcrun)) return false;
    char *args[] = {xcrun, (char *)"-f", (char *)"metal", nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    int status = smith_spawn_with(xcrun, args, &actions);
    posix_spawn_file_actions_destroy(&actions);
    return status == 0;
}

// Source -> .air -> .metallib with Apple's tools; no fast math, as the learner's kernels are built. The status.
static int smith_metal_build(const char *self, const char *source, const char *output) {
    char xcrun[PATH_MAX];
    if (!smith_find_in_path("xcrun", self, xcrun)) smith_die(smith_exit_missing, "cannot find xcrun", nullptr);
    char air[PATH_MAX];
    snprintf(air, sizeof air, "%s.air", output);
    char *compile[] = {xcrun,
                       (char *)"-sdk",
                       (char *)"macosx",
                       (char *)"metal",
                       (char *)"-fno-fast-math",
                       (char *)"-c",
                       (char *)source,
                       (char *)"-o",
                       air,
                       nullptr};
    int status = smith_spawn_with(xcrun, compile, nullptr);
    if (status != 0) return status;
    char *link[] = {xcrun, (char *)"-sdk", (char *)"macosx", (char *)"metallib",
                    air,   (char *)"-o",   (char *)output,   nullptr};
    status = smith_spawn_with(xcrun, link, nullptr);
    remove(air);
    return status;
}

// A copy of the source as the library, when there is no `metal`: the program compiles it when it starts.
static int smith_metal_copy(const char *source, const char *output) {
    FILE *in = fopen(source, "r");
    FILE *out = fopen(output, "w");
    if (in == nullptr || out == nullptr) smith_die(smith_exit_missing, "cannot write", output);
    char buffer[4096];
    size_t got = 0;
    while ((got = fread(buffer, 1, sizeof buffer, in)) > 0) // bounded by the file
        fwrite(buffer, 1, got, out);
    fclose(in);
    fclose(out);
    return 0;
}

// smith cc --smith-metal kernel.smith [-o kernel.metal|kernel.metallib] [-I dir]...: the kernels of the file as Metal
// source; a .metallib is built by Apple's `metal` when it resolves, and is the source itself when it does not.
static int smith_metal_main(int count, char **args, const char *self) {
    const char *usage = "usage: smith cc --smith-metal kernel.smith [-o kernel.metal|kernel.metallib] [-I dir]";
    const char *file = nullptr;
    const char *output = nullptr;
    for (int i = 0; i < count; i++) { // bounded by the arguments
        if (strcmp(args[i], "-o") == 0 && i + 1 < count) {
            output = args[++i];
        } else if (strcmp(args[i], "-I") == 0 && i + 1 < count &&
                   smith_header_flag_count + 2 <= smith_header_flags_max) {
            smith_header_flags[smith_header_flag_count++] = args[i];
            smith_header_flags[smith_header_flag_count++] = args[++i];
        } else if (file == nullptr && args[i][0] != '-') {
            file = args[i];
        } else {
            smith_die(smith_exit_usage, usage, nullptr);
        }
    }
    if (file == nullptr) smith_die(smith_exit_usage, usage, nullptr);
    bool library = output != nullptr && smith_metal_has_suffix(output, ".metallib");
    char source[PATH_MAX];
    if (library) snprintf(source, sizeof source, "%s.metal", output);
    const char *written = library ? source : output;
    smith_metal_out = written != nullptr ? fopen(written, "w") : stdout;
    if (smith_metal_out == nullptr) smith_die(smith_exit_missing, "cannot write", written);
    char table[PATH_MAX];
    char stem[PATH_MAX];
    smith_metal_table = nullptr;
    if (library) {
        const char *slash = strrchr(output, '/');
        snprintf(stem, sizeof stem, "%s", slash != nullptr ? slash + 1 : output);
        stem[strlen(stem) - strlen(".metallib")] = '\0';
        snprintf(table, sizeof table, "%.*s.kernels.c", (int)(strlen(output) - strlen(".metallib")), output);
        smith_metal_stem = stem;
        smith_metal_table = fopen(table, "w");
        if (smith_metal_table == nullptr) smith_die(smith_exit_missing, "cannot write", table);
    }
    smith_lang_dump_sink = smith_metal_sink;
    smith_metal_failed = false;
    int status = smith_lang_run_raw(file, nullptr, true);
    smith_lang_dump_sink = nullptr;
    if (status == 0 && smith_metal_failed) status = 1;
    if (status == 0 && smith_metal_kernels == 0) {
        fprintf(stderr, "%s: no kernel: a kernel is a function whose first parameter is `gpu::Thread *`\n", file);
        status = 1;
    }
    if (written != nullptr) fclose(smith_metal_out);
    if (smith_metal_table != nullptr) fclose(smith_metal_table);
    if (status != 0 && library) remove(table);
    if (status == 0 && library) {
        status =
            smith_metal_toolchain(self) ? smith_metal_build(self, source, output) : smith_metal_copy(source, output);
        remove(source);
    }
    if (written != nullptr && status != 0) remove(output != nullptr ? output : written);
    return status;
}
