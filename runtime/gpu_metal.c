// The Metal backend of a GPU kernel (issue #18, knowledge/gpu-metal.md): `smith cc --smith-metal kernel.smith -o
// k.metal` writes Metal source for every function of the file whose first parameter is `gpu::Thread *`. The lowering of
// the file (the plain lexer of runtime/lang_lex.c, the text `--smith-lower` prints) is C; the functions of a kernel are
// in the subset that is the same in Metal Shading Language (C types, arithmetic, `if`, `for`, `float` math), so the
// body is copied with its names mapped (`__UINT32_TYPE__` is `uint`, `tanhf` is `tanh`), and the signature is the part
// that is written: each pointer parameter is a `device` buffer and each other parameter a `constant` one, numbered in
// order, and the entry point makes the thread. Included by smith.c after runtime/lang_lex.c.

constexpr size_t smith_metal_params_max = 16;

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
    {"_Bool", "bool"},
    {"tanhf", "tanh"},
    {"expf", "exp"},
    {"logf", "log"},
    {"sinf", "sin"},
    {"cosf", "cos"},
    {"sqrtf", "sqrt"},
    {"fabsf", "fabs"},
    {"fminf", "fmin"},
    {"fmaxf", "fmax"},
    {"powf", "pow"},
    {"floorf", "floor"},
    {"ceilf", "ceil"},
};

typedef struct {
    const char *text;
    size_t length;
} SmithMetalSpan;

typedef struct {
    SmithMetalSpan type; // everything before the name, `const float *`
    SmithMetalSpan name;
    bool pointer;
} SmithMetalParam;

static bool smith_metal_word_char(char c) {
    return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
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

// One parameter as the helper function takes it: a pointer is a `device` buffer, the rest are values.
static void smith_metal_write_param(FILE *out, const SmithMetalParam *param) {
    if (param->pointer) fputs("device ", out);
    smith_metal_write(out, param->type.text, param->type.length);
    fwrite(param->name.text, 1, param->name.length, out);
}

// The entry point: a thread's position in the grid, then the buffers in the order of the parameters after the thread.
static void smith_metal_entry(FILE *out, SmithMetalSpan name, const SmithMetalParam *params, size_t count) {
    fprintf(out, "kernel void %.*s(uint smith_position [[thread_position_in_grid]]", (int)name.length, name.text);
    for (size_t i = 1; i < count; i++) { // bounded by the parameters
        fputs(", ", out);
        if (params[i].pointer) {
            smith_metal_write_param(out, &params[i]);
        } else {
            fputs("constant ", out);
            smith_metal_write(out, params[i].type.text, params[i].type.length);
            fprintf(out, "&%.*s", (int)params[i].name.length, params[i].name.text);
        }
        fprintf(out, " [[buffer(%zu)]]", i - 1);
    }
    fprintf(out, ") {\n    smith_gpu_Thread smith_thread = {smith_position};\n    %.*s_body(&smith_thread",
            (int)name.length, name.text);
    for (size_t i = 1; i < count; i++)
        fprintf(out, ", %.*s", (int)params[i].name.length, params[i].name.text);
    fputs(");\n}\n\n", out);
}

// Writes the kernel whose head is text[head..brace) and whose body ends at `end`; false when the head is not a kernel.
static bool smith_metal_kernel(FILE *out, const char *text, size_t head, size_t brace, size_t end) {
    const char *open = memchr(text + head, '(', brace - head);
    const char *close = open != nullptr ? memchr(open, ')', text + brace - open) : nullptr;
    if (close == nullptr) return false;
    static const char thread[] = "smith_gpu_Thread *";
    if ((size_t)(close - open) <= sizeof thread || strncmp(open + 1, thread, sizeof thread - 1) != 0) return false;
    size_t name_end = (size_t)(open - text);
    while (name_end > head && text[name_end - 1] == ' ')
        name_end--;
    size_t name = name_end;
    while (name > head && smith_metal_word_char(text[name - 1]))
        name--;
    SmithMetalParam params[smith_metal_params_max];
    size_t count = 0;
    if (!smith_metal_params((SmithMetalSpan){open + 1, (size_t)(close - open - 1)}, params, &count)) return false;
    SmithMetalSpan id = {text + name, name_end - name};
    fprintf(out, "static void %.*s_body(thread smith_gpu_Thread *%.*s", (int)id.length, id.text,
            (int)params[0].name.length, params[0].name.text);
    for (size_t i = 1; i < count; i++) { // bounded by the parameters
        fputs(", ", out);
        smith_metal_write_param(out, &params[i]);
    }
    fputs(") ", out);
    smith_metal_write(out, text + brace, end - brace);
    fputs("\n\n", out);
    smith_metal_entry(out, id, params, count);
    return true;
}

// A file-scope `static constexpr float k = 1.0f;` is a `constant` of the Metal file (a kernel may use it).
static void smith_metal_constant(FILE *out, const char *statement, size_t length) {
    static const char prefix[] = "static constexpr ";
    while (length > 0 && (statement[0] == '\n' || statement[0] == ' ')) {
        statement++;
        length--;
    }
    if (length <= sizeof prefix || strncmp(statement, prefix, sizeof prefix - 1) != 0) return;
    fputs("constant ", out);
    smith_metal_write(out, statement + sizeof prefix - 1, length - (sizeof prefix - 1));
    fputs("\n", out);
}

// Writes the Metal source of every kernel in the lowered text, after the prelude; the number of kernels.
static size_t smith_metal_source(FILE *out, const char *text, size_t length) {
    fputs("#include <metal_stdlib>\nusing namespace metal;\n\nstruct smith_gpu_Thread {\n    uint index;\n};\n\n", out);
    size_t kernels = 0;
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
            smith_metal_constant(out, text + statement, i + 1 - statement);
            statement = ++i;
        } else if (text[i] == '{') {
            size_t end = smith_metal_close(text, length, i);
            while (statement < i && (text[statement] == '\n' || text[statement] == ' '))
                statement++;
            kernels += smith_metal_kernel(out, text, statement, i, end) ? 1 : 0;
            i = statement = end;
        } else {
            i++;
        }
    }
    return kernels;
}

// What `--smith-lower` would print, taken by `--smith-metal` instead (runtime/lang_lex.c, smith_lang_dump_sink).
static FILE *smith_metal_out;
static size_t smith_metal_kernels;

static void smith_metal_sink(const char *text, size_t length) {
    smith_metal_kernels = smith_metal_source(smith_metal_out, text, length);
}

// smith cc --smith-metal kernel.smith [-o kernel.metal] [-I dir]...: the kernels of the file as Metal source.
static int smith_metal_main(int count, char **args) {
    const char *usage = "usage: smith cc --smith-metal kernel.smith [-o kernel.metal] [-I dir]";
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
    smith_metal_out = output != nullptr ? fopen(output, "w") : stdout;
    if (smith_metal_out == nullptr) smith_die(smith_exit_missing, "cannot write", output);
    smith_lang_dump_sink = smith_metal_sink;
    int status = smith_lang_run_raw(file, nullptr, true);
    smith_lang_dump_sink = nullptr;
    if (status == 0 && smith_metal_kernels == 0) {
        fprintf(stderr, "%s: no kernel: a kernel is a function whose first parameter is `gpu::Thread *`\n", file);
        status = 1;
    }
    if (output != nullptr) {
        fclose(smith_metal_out);
        if (status != 0) remove(output);
    }
    return status;
}
