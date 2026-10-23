// `smith symbolize`, and smith answering as llvm-symbolizer: the traces <smith/trace.h> writes, turned into the failing
// line and the call chain. Included by smith.c.
//
// One filter does the work wherever output passes through smith: it copies its input, and replaces each block of
// `smith-trace` lines with a report. `smith test` pipes the test runner through it; `smith symbolize` runs it on
// a file or on standard input (a log, a crash report) with the binary named when the trace's own path is gone. The
// lookups are in shim/symbolize.cpp (LLVM's symbolizer); this file has the parsing, the choice of the failing frame
// and the text. Without the shim (the stage0 has no LLVM) `smith symbolize` says so.
//
// The report, for a block with the heading "check failed":
//
//   check failed at tests/moves_test.c:42
//        42 |     smith_check(moves.count == expected);
//   called from
//     tests/moves_test.c:42  moves_every_step
//     lib/test.c:812         smith_test_execute
//
// The failing frame is the first frame that is not smith's own check, contract and abort machinery (a function whose
// name starts with smith_test_, smith_check, smith_requires, smith_ensures, smith_invariant, smith_assert,
// smith_failure, smith_contract_, smith_trace_, or the library as a module: smith::test::, smith::contract:: or
// smith::trace::, and the older smith.test. and smith.contract.) and not a frame of a system library (libc's abort
// path, which has no debug info). The chain ends at the first frame of the test runner (smith_test_... or
// smith::test::...) or at main. A
// `.smith` function is shown as written, `geometry::box::area` (the debug names of issue #27), and so is the name of a
// module's test.

constexpr size_t smith_sym_frames_max = 64;
constexpr size_t smith_sym_images_max = 32;
constexpr size_t smith_sym_rows_max = 256;
constexpr size_t smith_sym_inline_max = 8;
constexpr size_t smith_sym_line_max = 4096;
constexpr size_t smith_sym_notes_max = 4;
constexpr size_t smith_sym_chain_max = 32;
constexpr size_t smith_sym_details_max = 8;
constexpr size_t smith_sym_arguments_max = 4;
constexpr int smith_sym_exit_unavailable = 3;

typedef struct {
    char function[512];
    char file[1024];
    uint32_t line;
    uint32_t column;
} SmithSymbol;

#ifdef SMITH_IN_PROCESS
bool smith_symbolize_uuid(const char *path, char out[37]);
uint64_t smith_symbolize_text_base(const char *path);
const char *smith_symbolize_problem(void);
int smith_symbolize_code(const char *binary, uint64_t address, SmithSymbol *out, int capacity);
#else
static bool smith_symbolize_uuid(const char *path, char out[37]) {
    (void)path;
    (void)out;
    return false;
}

static uint64_t smith_symbolize_text_base(const char *path) {
    (void)path;
    return 0;
}

static const char *smith_symbolize_problem(void) {
    return "";
}

static int smith_symbolize_code(const char *binary, uint64_t address, SmithSymbol *out, int capacity) {
    (void)binary;
    (void)address;
    (void)out;
    (void)capacity;
    return 0;
}
#endif

typedef struct {
    char uuid[40];
    uint64_t text;
    uint64_t slide;
    uint64_t size;
    char path[1024];     // as the trace names it
    char resolved[1024]; // the file to read: --binary, else the path
    bool checked;
    bool usable; // resolved was read, and its UUID is the trace's
} SmithSymImage;

typedef struct {
    uint64_t address;
    bool exact;
    char name[256]; // the nearest symbol the program wrote, or ""
} SmithSymFrame;

typedef struct {
    char indent[32];
    char heading[128];
    char site[1024];
    char details[smith_sym_details_max][256]; // what was wrong: the values of a failed check
    size_t detail_count;
    SmithSymImage images[smith_sym_images_max];
    size_t image_count;
    SmithSymFrame frames[smith_sym_frames_max];
    size_t frame_count;
} SmithSymTrace;

typedef struct {
    char function[512];
    char file[1024];
    uint32_t line;
    char image[256]; // the file name of the image, when the row has no source
    int image_index; // into the trace's images, or -1
    bool symbolized; // it has a source file
    bool system;     // it is in a system library
} SmithSymRow;

typedef struct {
    char text[smith_sym_notes_max][1200];
    size_t count;
} SmithSymNotes;

static const char *smith_sym_binary_override;
static char smith_sym_directory[PATH_MAX];

static bool smith_sym_starts(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static const char *smith_sym_base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash != nullptr ? slash + 1 : path;
}

static void smith_sym_note(SmithSymNotes *notes, const char *text) {
    for (size_t i = 0; i < notes->count; i++)
        if (strcmp(notes->text[i], text) == 0) return;
    if (notes->count < smith_sym_notes_max) snprintf(notes->text[notes->count++], sizeof notes->text[0], "%s", text);
}

// Parsing.

static void smith_sym_copy(char *out, size_t capacity, const char *text) {
    snprintf(out, capacity, "%s", text);
    size_t length = strlen(out);
    while (length > 0 && (out[length - 1] == '\n' || out[length - 1] == '\r'))
        out[--length] = '\0';
}

// If the line is `<blanks>smith-trace <word> <rest>`, the word and the rest, and the blanks in indent.
static bool smith_sym_line(const char *line, char *indent, size_t indent_capacity, const char **word,
                           const char **rest) {
    size_t blanks = strspn(line, " \t");
    if (!smith_sym_starts(line + blanks, "smith-trace ")) return false;
    snprintf(indent, indent_capacity, "%.*s", (int)blanks, line);
    *word = line + blanks + strlen("smith-trace ");
    *rest = *word + strcspn(*word, " \n");
    if (**rest == ' ') *rest += 1;
    return true;
}

// The next word of the text and the text after it: word is copied, rest points past the blanks that follow.
static const char *smith_sym_word(const char *text, char *word, size_t capacity) {
    size_t length = strcspn(text, " \t\n");
    snprintf(word, capacity, "%.*s", (int)length, text);
    text += length;
    return text + strspn(text, " \t");
}

static bool smith_sym_hex(const char *word, uint64_t *value) {
    char *end = nullptr;
    *value = strtoull(word, &end, 16);
    return end != word && *end == '\0';
}

static void smith_sym_parse_image(SmithSymTrace *trace, const char *rest) {
    if (trace->image_count >= smith_sym_images_max) return;
    SmithSymImage *image = &trace->images[trace->image_count];
    *image = (SmithSymImage){};
    char text[32];
    char slide[32];
    char size[32];
    rest = smith_sym_word(rest, image->uuid, sizeof image->uuid);
    rest = smith_sym_word(rest, text, sizeof text);
    rest = smith_sym_word(rest, slide, sizeof slide);
    rest = smith_sym_word(rest, size, sizeof size);
    if (!smith_sym_hex(text, &image->text) || !smith_sym_hex(slide, &image->slide) ||
        !smith_sym_hex(size, &image->size))
        return;
    smith_sym_copy(image->path, sizeof image->path, rest);
    trace->image_count += 1;
}

static void smith_sym_parse_frame(SmithSymTrace *trace, const char *rest, bool exact) {
    if (trace->frame_count >= smith_sym_frames_max) return;
    SmithSymFrame *frame = &trace->frames[trace->frame_count];
    *frame = (SmithSymFrame){.exact = exact};
    char address[32];
    rest = smith_sym_word(rest, address, sizeof address);
    if (!smith_sym_hex(address, &frame->address)) return;
    smith_sym_copy(frame->name, sizeof frame->name, rest);
    char *plus = strrchr(frame->name, '+'); // name+0xoff: the function is the name
    if (plus != nullptr) *plus = '\0';
    trace->frame_count += 1;
}

// Applies one line of a block. False at the block's end.
static bool smith_sym_apply(SmithSymTrace *trace, const char *word, const char *rest) {
    if (smith_sym_starts(word, "end")) return false;
    if (smith_sym_starts(word, "site ")) smith_sym_copy(trace->site, sizeof trace->site, rest);
    if (smith_sym_starts(word, "detail ") && trace->detail_count < smith_sym_details_max)
        smith_sym_copy(trace->details[trace->detail_count++], sizeof trace->details[0], rest);
    if (smith_sym_starts(word, "image ")) smith_sym_parse_image(trace, rest);
    if (smith_sym_starts(word, "pc ")) smith_sym_parse_frame(trace, rest, true);
    if (smith_sym_starts(word, "frame ")) smith_sym_parse_frame(trace, rest, false);
    return true;
}

// Rows.

static const char *smith_sym_skipped[] = {
    "smith_test_",     "smith_check",   "smith_requires",    "smith_ensures", "smith_invariant",
    "smith_assert",    "smith_failure", "smith_contract_",   "smith_trace_",  "smith.test.",
    "smith.contract.", "smith::test::", "smith::contract::", "smith::trace::"};

static bool smith_sym_machinery(const char *function) {
    while (*function == '_')
        function += 1;
    for (size_t i = 0; i < sizeof smith_sym_skipped / sizeof smith_sym_skipped[0]; i++)
        if (smith_sym_starts(function, smith_sym_skipped[i])) return true;
    return false;
}

static bool smith_sym_system_path(const char *path) {
    return smith_sym_starts(path, "/usr/lib/") || smith_sym_starts(path, "/System/") ||
           smith_sym_starts(path, "/Library/Apple/");
}

static int smith_sym_find_image(const SmithSymTrace *trace, uint64_t address) {
    for (size_t i = 0; i < trace->image_count; i++)
        if (address >= trace->images[i].text && address - trace->images[i].text < trace->images[i].size) return (int)i;
    return -1;
}

// The file to read for an image, checked once: the --binary file when its name or UUID is the image's, else the path
// the trace gives. A file that is not the one that ran is a note, and no lines.
static void smith_sym_check_image(SmithSymImage *image, SmithSymNotes *notes) {
    if (image->checked) return;
    image->checked = true;
    const char *override = smith_sym_binary_override;
    // --binary stands for the program: when it has the image's name, or the image's own file is gone.
    bool named = override != nullptr && (strcmp(smith_sym_base_name(override), smith_sym_base_name(image->path)) == 0 ||
                                         access(image->path, F_OK) != 0);
    snprintf(image->resolved, sizeof image->resolved, "%s", named ? override : image->path);
    char uuid[40];
    if (!smith_symbolize_uuid(image->resolved, uuid)) {
        char text[1200];
        snprintf(text, sizeof text, "cannot read %s, so its frames have no lines%s", image->resolved,
                 override == nullptr ? "; name the program with --binary" : "");
        smith_sym_note(notes, text);
        return;
    }
    if (strcmp(image->uuid, "-") != 0 && strcmp(uuid, image->uuid) != 0) {
        char text[1200];
        snprintf(text, sizeof text, "%s is not the program that ran (its UUID is %s, the trace's is %s)",
                 image->resolved, uuid, image->uuid);
        smith_sym_note(notes, text);
        return;
    }
    image->usable = true;
}

static size_t smith_sym_row_add(SmithSymRow *rows, size_t count, const SmithSymbol *symbol, int image_index) {
    SmithSymRow *row = &rows[count];
    *row = (SmithSymRow){.line = symbol->line, .image_index = image_index, .symbolized = symbol->file[0] != '\0'};
    snprintf(row->function, sizeof row->function, "%s", symbol->function);
    snprintf(row->file, sizeof row->file, "%s", symbol->file);
    return count + 1;
}

// The rows of one frame: its inline frames when the debug info has them, else one row with what is known.
static size_t smith_sym_rows_for(SmithSymTrace *trace, const SmithSymFrame *frame, SmithSymRow *rows, size_t count,
                                 SmithSymNotes *notes) {
    int index = smith_sym_find_image(trace, frame->address);
    SmithSymbol symbols[smith_sym_inline_max] = {};
    int found = 0;
    if (index >= 0 && !smith_sym_system_path(trace->images[index].path)) {
        SmithSymImage *image = &trace->images[index];
        smith_sym_check_image(image, notes);
        if (image->usable) {
            uint64_t address = frame->address - image->slide - (frame->exact ? 0 : 1);
            found = smith_symbolize_code(image->resolved, address, symbols, (int)smith_sym_inline_max);
            if (smith_symbolize_problem()[0] != '\0') smith_sym_note(notes, smith_symbolize_problem());
        }
    }
    for (int i = 0; i < found && count < smith_sym_rows_max; i++)
        count = smith_sym_row_add(rows, count, &symbols[i], index);
    if (found == 0 && count < smith_sym_rows_max) {
        SmithSymbol bare = {.line = 0};
        snprintf(bare.function, sizeof bare.function, "%s", frame->name);
        count = smith_sym_row_add(rows, count, &bare, index);
        SmithSymRow *row = &rows[count - 1];
        row->system = index < 0 || smith_sym_system_path(trace->images[index].path);
        if (index >= 0) snprintf(row->image, sizeof row->image, "%s", smith_sym_base_name(trace->images[index].path));
    }
    return count;
}

static size_t smith_sym_rows(SmithSymTrace *trace, SmithSymRow *rows, SmithSymNotes *notes) {
    size_t count = 0;
    for (size_t i = 0; i < trace->frame_count; i++)
        count = smith_sym_rows_for(trace, &trace->frames[i], rows, count, notes);
    return count;
}

// Text.

// A path relative to the current folder when it is under it.
static const char *smith_sym_short(const char *path) {
    // The system's /var, /tmp and /etc are links into /private: getcwd names the real folder, a compile's paths may
    // not.
    const char *folder = smith_sym_directory;
    if (smith_sym_starts(folder, "/private/") && !smith_sym_starts(path, "/private/")) folder += strlen("/private");
    size_t length = strlen(folder);
    if (length > 0 && strncmp(path, folder, length) == 0 && path[length] == '/') return path + length + 1;
    return path;
}

// Line number `line` of the file, without its newline and trailing blanks. False when it cannot be read.
static bool smith_sym_source(const char *file, uint32_t line, char *text, size_t capacity) {
    FILE *source = fopen(file, "r");
    if (source == nullptr) return false;
    bool found = false;
    for (uint32_t current = 1; current <= line; current++) { // bounded by the line asked for
        if (fgets(text, (int)capacity, source) == nullptr) break;
        if (current == line) found = true;
        if (strchr(text, '\n') == nullptr && !feof(source)) { // a longer line: skip its rest
            int c = 0;
            for (size_t skipped = 0; skipped < (size_t)1 << 20 && c != '\n' && c != EOF; skipped++)
                c = fgetc(source);
        }
    }
    fclose(source);
    size_t length = strlen(text);
    while (found && length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r' || text[length - 1] == ' '))
        text[--length] = '\0';
    return found;
}

static bool smith_sym_newer(const char *source, const char *binary) {
    struct stat source_info;
    struct stat binary_info;
    if (stat(source, &source_info) != 0 || stat(binary, &binary_info) != 0) return false;
    return source_info.st_mtimespec.tv_sec > binary_info.st_mtimespec.tv_sec;
}

typedef struct {
    size_t first; // the failing row
    size_t last;  // the last row of the chain
    bool found;
} SmithSymChain;

static bool smith_sym_skip(const SmithSymRow *row) {
    return smith_sym_machinery(row->function) || (!row->symbolized && row->system);
}

static SmithSymChain smith_sym_chain(const SmithSymRow *rows, size_t count) {
    SmithSymChain chain = {};
    for (size_t i = 0; i < count && !chain.found; i++) {
        if (smith_sym_skip(&rows[i])) continue;
        chain.first = i;
        chain.found = true;
    }
    chain.last = chain.first;
    for (size_t i = chain.first; chain.found && i < count && i < chain.first + smith_sym_chain_max; i++) {
        chain.last = i;
        const char *name = rows[i].function;
        if (strcmp(name, "main") == 0 || strcmp(name, "_main") == 0) break;
        const char *bare = name + (name[0] == '_');
        if (i > chain.first && smith_sym_machinery(name) &&
            (smith_sym_starts(bare, "smith_test_") || smith_sym_starts(bare, "smith::test::")))
            break;
    }
    return chain;
}

// A generic check may be given its type first, `test::check_equal(a, b)`: that argument names no value.
static size_t smith_sym_drop_type(char arguments[smith_sym_arguments_max][128], size_t count) {
    static const char *const types[] = {"bool",  "char",  "i8",     "i16",    "i32",       "i64",    "isize", "u8",
                                        "u16",   "u32",   "u64",    "usize",  "f32",       "f64",    "int",   "long",
                                        "short", "float", "double", "size_t", "ptrdiff_t", "uint8_t"};
    if (count < 2) return count;
    bool type = strchr(arguments[0], '*') != nullptr || strncmp(arguments[0], "const ", 6) == 0;
    for (size_t i = 0; !type && i < sizeof types / sizeof types[0]; i++)
        type = strcmp(arguments[0], types[i]) == 0;
    if (!type) return count;
    for (size_t i = 1; i < count; i++)
        memcpy(arguments[i - 1], arguments[i], sizeof arguments[0]);
    return count - 1;
}

// The arguments of the check call (smith_check_equal_u64 in C, test::check_equal in .smith) on the source line: the
// texts between the top-level commas of its parentheses. Returns how many (at most smith_sym_arguments_max); each is a
// copy with its blanks trimmed.
static size_t smith_sym_arguments(const char *source, char arguments[smith_sym_arguments_max][128]) {
    const char *call = strstr(source, "smith_check_");
    if (call == nullptr) call = strstr(source, "::check_"); // test::check_equal(a, b)
    const char *open = call != nullptr ? strchr(call, '(') : nullptr;
    if (open == nullptr) return 0;
    size_t count = 0;
    size_t used = 0;
    int depth = 0;
    for (const char *c = open + 1; *c != '\0' && count < smith_sym_arguments_max; c++) {
        bool end = depth == 0 && (*c == ',' || *c == ')');
        if (!end && used + 1 < sizeof arguments[0]) arguments[count][used++] = *c;
        if (*c == '(' || *c == '[' || *c == '{') depth += 1;
        if ((*c == ')' || *c == ']' || *c == '}') && depth > 0) depth -= 1;
        if (!end) continue;
        arguments[count][used] = '\0';
        char *text = arguments[count];
        text += strspn(text, " \t");
        size_t length = strlen(text);
        while (length > 0 && text[length - 1] == ' ')
            text[--length] = '\0';
        memmove(arguments[count], text, length + 1);
        count += 1;
        used = 0;
        if (*c == ')') break;
    }
    return smith_sym_drop_type(arguments, count);
}

// A value line `label = value   (type)` split in place: the label, the value and the type's text. False for any other
// line.
static bool smith_sym_value_line(char *line, char **label, char **value, char **type) {
    char *equals = strstr(line, " = ");
    char *parenthesis = strrchr(line, '(');
    if (equals == nullptr || parenthesis == nullptr || parenthesis < equals) return false;
    *equals = '\0';
    *label = line;
    *value = equals + 3;
    *type = parenthesis;
    while (parenthesis > *value && parenthesis[-1] == ' ')
        *--parenthesis = '\0';
    return true;
}

// The name a check's value goes by: the text of the argument on the source line (a, b and tolerance are the first, the
// second and the third), else the label.
static const char *smith_sym_name(const char *label, char arguments[smith_sym_arguments_max][128],
                                  size_t argument_count) {
    size_t index = strcmp(label, "a") == 0 ? 0 : strcmp(label, "b") == 0 ? 1 : strcmp(label, "tolerance") == 0 ? 2 : 99;
    return index < argument_count && arguments[index][0] != '\0' ? arguments[index] : label;
}

// `name = value`, or just the value when the name is the value (an argument written as the literal 999).
static void smith_sym_named(char *out, size_t capacity, const char *name, const char *value) {
    if (strcmp(name, value) == 0 || name[0] == '\0')
        snprintf(out, capacity, "%s", value);
    else
        snprintf(out, capacity, "%s = %s", name, value);
}

// What was wrong, under the source line, aligned with its text: two values of one type on one line (`count = 3,
// expected = 4   (u64)`), else a line each.
static void smith_sym_print_values(FILE *out, const char *indent, const SmithSymTrace *trace, const char *source) {
    char arguments[smith_sym_arguments_max][128] = {};
    size_t argument_count = smith_sym_arguments(source, arguments);
    char lines[smith_sym_details_max][256];
    char *labels[smith_sym_details_max] = {};
    char *values[smith_sym_details_max] = {};
    char *types[smith_sym_details_max] = {};
    for (size_t i = 0; i < trace->detail_count; i++) {
        snprintf(lines[i], sizeof lines[i], "%s", trace->details[i]);
        if (!smith_sym_value_line(lines[i], &labels[i], &values[i], &types[i])) labels[i] = nullptr;
    }
    bool pair = trace->detail_count == 2 && labels[0] != nullptr && labels[1] != nullptr &&
                strcmp(labels[0], "a") == 0 && strcmp(labels[1], "b") == 0 && strcmp(types[0], types[1]) == 0;
    if (pair) {
        char first[300];
        char second[300];
        smith_sym_named(first, sizeof first, smith_sym_name("a", arguments, argument_count), values[0]);
        smith_sym_named(second, sizeof second, smith_sym_name("b", arguments, argument_count), values[1]);
        fprintf(out, "%s%s, %s   %s\n", indent, first, second, types[0]);
        return;
    }
    for (size_t i = 0; i < trace->detail_count; i++) {
        char named[300];
        if (labels[i] == nullptr) {
            fprintf(out, "%s%s\n", indent, trace->details[i]);
            continue;
        }
        size_t label_length = strlen(labels[i]);
        while (label_length > 0 && labels[i][label_length - 1] == ' ') // the labels are padded to one width
            labels[i][--label_length] = '\0';
        smith_sym_named(named, sizeof named, smith_sym_name(labels[i], arguments, argument_count), values[i]);
        fprintf(out, "%s%s   %s\n", indent, named, types[i]);
    }
}

// The source line, what was wrong under it, and a note if the file is newer than the program.
static void smith_sym_print_source(FILE *out, const char *indent, const SmithSymTrace *trace, const char *file,
                                   uint32_t line, const char *binary) {
    char text[512];
    bool shown = smith_sym_source(file, line, text, sizeof text);
    if (shown) fprintf(out, "%s%5u | %s\n", indent, (unsigned)line, text);
    char values_indent[96];
    snprintf(values_indent, sizeof values_indent, "%s        ", indent); // under the text of the source line
    smith_sym_print_values(out, values_indent, trace, shown ? text : "");
    if (shown && binary != nullptr && smith_sym_newer(file, binary))
        fprintf(out, "%snote: %s is newer than the program, so line %u may have changed\n", indent,
                smith_sym_short(file), (unsigned)line);
}

static void smith_sym_print_row(FILE *out, const char *indent, const SmithSymRow *row, int width) {
    char where[1100];
    if (row->symbolized)
        snprintf(where, sizeof where, "%s:%u", smith_sym_short(row->file), (unsigned)row->line);
    else
        snprintf(where, sizeof where, "%s", row->image[0] != '\0' ? row->image : "??");
    fprintf(out, "%s  %-*s  %s\n", indent, width, where, row->function[0] != '\0' ? row->function : "??");
}

static int smith_sym_width(const SmithSymRow *rows, SmithSymChain chain) {
    int width = 0;
    for (size_t i = chain.first; i <= chain.last; i++) {
        int length = rows[i].symbolized
                         ? (int)strlen(smith_sym_short(rows[i].file)) + 1 + (rows[i].line >= 100000 ? 6 : 5)
                         : (int)strlen(rows[i].image[0] != '\0' ? rows[i].image : "??");
        if (length > width) width = length;
    }
    return width < 60 ? width : 60;
}

// Prints the report of one block to out.
static void smith_sym_report(FILE *out, SmithSymTrace *trace) {
    static SmithSymRow rows[smith_sym_rows_max];
    SmithSymNotes notes = {};
    size_t count = smith_sym_rows(trace, rows, &notes);
    SmithSymChain chain = smith_sym_chain(rows, count);
    const char *indent = trace->indent;
    if (chain.found && rows[chain.first].symbolized) {
        const SmithSymRow *row = &rows[chain.first];
        const SmithSymImage *image = row->image_index >= 0 ? &trace->images[row->image_index] : nullptr;
        fprintf(out, "%s%s at %s:%u\n", indent, trace->heading, smith_sym_short(row->file), (unsigned)row->line);
        char deeper[64];
        snprintf(deeper, sizeof deeper, "%s  ", indent);
        smith_sym_print_source(out, deeper, trace, row->file, row->line, image != nullptr ? image->resolved : nullptr);
        fprintf(out, "%scalled from\n", indent);
        int width = smith_sym_width(rows, chain);
        for (size_t i = chain.first; i <= chain.last; i++)
            smith_sym_print_row(out, indent, &rows[i], width);
    } else if (trace->site[0] != '\0') { // no debug info for the stack: the program's own word for where
        fprintf(out, "%s%s at %s\n", indent, trace->heading, trace->site);
        char file[1024];
        snprintf(file, sizeof file, "%s", trace->site);
        char *colon = strrchr(file, ':');
        if (colon != nullptr) {
            *colon = '\0';
            char deeper[64];
            snprintf(deeper, sizeof deeper, "%s  ", indent);
            smith_sym_print_source(out, deeper, trace, file, (uint32_t)strtoul(colon + 1, nullptr, 10), nullptr);
        }
    } else {
        fprintf(out, "%s%s (no debug info for its stack%s)\n", indent, trace->heading,
                count == 0 ? "" : "; build with -g");
        char deeper[64];
        snprintf(deeper, sizeof deeper, "%s  ", indent);
        smith_sym_print_values(out, deeper, trace, "");
        for (size_t i = 0; i < count && i < smith_sym_chain_max; i++)
            smith_sym_print_row(out, indent, &rows[i], 24);
    }
    for (size_t i = 0; i < notes.count; i++)
        fprintf(out, "%snote: %s\n", indent, notes.text[i]);
}

// The filter.

// Reads the lines of a block (the first, `begin`, already seen), and reports it. Returns when the block ends. A line
// that is not part of the block ends it too, and is left in line for the caller.
static bool smith_sym_block(FILE *in, FILE *out, SmithSymTrace *trace, char *line, size_t capacity) {
    bool open = true;
    bool pending = false; // line holds a line of the input that is not the block's
    while (open && fgets(line, (int)capacity, in) != nullptr) {
        char indent[32];
        const char *word = nullptr;
        const char *rest = nullptr;
        if (!smith_sym_line(line, indent, sizeof indent, &word, &rest)) {
            pending = true;
            break;
        }
        open = smith_sym_apply(trace, word, rest);
    }
    smith_sym_report(out, trace);
    return pending;
}

static int smith_sym_filter(FILE *in, FILE *out) {
    static SmithSymTrace trace;
    char line[smith_sym_line_max];
    bool at_start = true;
    bool have = fgets(line, sizeof line, in) != nullptr;
    for (size_t guard = 0; have && guard < (size_t)1 << 40; guard++) {
        char indent[32];
        const char *word = nullptr;
        const char *rest = nullptr;
        if (at_start && smith_sym_line(line, indent, sizeof indent, &word, &rest) && smith_sym_starts(word, "begin ")) {
            trace = (SmithSymTrace){};
            snprintf(trace.indent, sizeof trace.indent, "%s", indent);
            smith_sym_copy(trace.heading, sizeof trace.heading, rest);
            bool pending = smith_sym_block(in, out, &trace, line, sizeof line);
            at_start = true;
            have = pending ? true : fgets(line, sizeof line, in) != nullptr;
            fflush(out);
            continue;
        }
        fputs(line, out);
        at_start = strchr(line, '\n') != nullptr;
        if (at_start) fflush(out);
        have = fgets(line, sizeof line, in) != nullptr;
    }
    fflush(out);
    return 0;
}

// `smith symbolize [--binary <program>] [<file>]`, and `smith symbolize --available`.
static int smith_symbolize_main(int argc, char **argv) {
    const char *file = nullptr;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--available") == 0) {
#ifdef SMITH_IN_PROCESS
            return 0;
#else
            return smith_sym_exit_unavailable;
#endif
        }
        if (strcmp(argv[i], "--binary") == 0 && i + 1 < argc) {
            smith_sym_binary_override = argv[++i];
        } else if ((argv[i][0] == '-' && argv[i][1] != '\0') || file != nullptr) {
            smith_die(smith_exit_usage, "usage: smith symbolize [--binary <program>] [<trace file>]", nullptr);
        } else {
            file = argv[i];
        }
    }
#ifndef SMITH_IN_PROCESS
    smith_die(smith_exit_missing,
              "smith symbolize needs the full smith, which links LLVM's symbolizer; this one does not", nullptr);
#endif
    if (getcwd(smith_sym_directory, sizeof smith_sym_directory) == nullptr) smith_sym_directory[0] = '\0';
    FILE *in = file != nullptr && strcmp(file, "-") != 0 ? fopen(file, "r") : stdin;
    if (in == nullptr) smith_die(smith_exit_usage, "cannot read", file);
    int status = smith_sym_filter(in, stdout);
    if (in != stdin) fclose(in);
    return status;
}

// smith run as llvm-symbolizer, which the sanitizers' runtimes start (ASAN_SYMBOLIZER_PATH) and speak to on standard
// input: `"<module>" <offset>` or `<module> <offset>` per line, optionally after CODE or DATA. For each CODE address
// the answer is a function name line and a file:line:column line per frame (the inlined frames first), then an empty
// line.
// --relative-address says the offset is from the module's load address, so the link address of __TEXT is added.
typedef struct {
    bool relative;
    bool inlines;
    const char *object; // --obj=<file>: every line is an address in it
} SmithSymProtocol;

static void smith_sym_answer(const SmithSymProtocol *protocol, const char *module, uint64_t offset, bool code) {
    SmithSymbol symbols[smith_sym_inline_max] = {};
    int found = 0;
    if (code) {
        uint64_t address = offset + (protocol->relative ? smith_symbolize_text_base(module) : 0);
        found = smith_symbolize_code(module, address, symbols, (int)smith_sym_inline_max);
    }
    if (found == 0) {
        puts(code ? "??\n??:0:0" : "??\n0 0");
    }
    for (int i = 0; i < found && (protocol->inlines || i == 0); i++)
        printf("%s\n%s:%u:%u\n", symbols[i].function[0] != '\0' ? symbols[i].function : "??",
               symbols[i].file[0] != '\0' ? symbols[i].file : "??", (unsigned)symbols[i].line,
               (unsigned)symbols[i].column);
    puts("");
    fflush(stdout);
}

// A request line: the module and the offset. False for a blank or malformed line.
static bool smith_sym_request(char *line, bool *code, char **module, uint64_t *offset) {
    char *at = line + strspn(line, " \t");
    *code = true;
    if (smith_sym_starts(at, "CODE ")) at += strlen("CODE ");
    if (smith_sym_starts(at, "DATA ")) {
        at += strlen("DATA ");
        *code = false;
    }
    at += strspn(at, " \t");
    if (*at == '"') {
        *module = at + 1;
        char *close = strchr(at + 1, '"');
        if (close == nullptr) return false;
        *close = '\0';
        at = close + 1;
    } else {
        *module = at;
        at += strcspn(at, " \t\n");
        if (*at == '\0') return false;
        *at++ = '\0';
    }
    // A sanitizer names the slice of a universal binary after the file: `/path/program:arm64`.
    char *colon = strrchr(*module, ':');
    if (colon != nullptr && access(*module, F_OK) != 0) *colon = '\0';
    char *end = nullptr;
    *offset = strtoull(at, &end, 0);
    return end != at;
}

static int smith_llvm_symbolizer_main(int argc, char **argv) {
    SmithSymProtocol protocol = {.inlines = true};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--relative-address") == 0) protocol.relative = true;
        if (strcmp(argv[i], "--no-inlines") == 0 || strcmp(argv[i], "--inlines=false") == 0) protocol.inlines = false;
        if (smith_sym_starts(argv[i], "--obj=")) protocol.object = argv[i] + strlen("--obj=");
    }
#ifndef SMITH_IN_PROCESS
    smith_die(smith_exit_missing, "this smith has no symbolizer; it needs the full smith", nullptr);
#endif
    char line[smith_sym_line_max];
    for (size_t reads = 0; reads < (size_t)1 << 40 && fgets(line, sizeof line, stdin) != nullptr; reads++) {
        bool code = true;
        char *module = nullptr;
        uint64_t offset = 0;
        if (protocol.object != nullptr) {
            module = (char *)protocol.object;
            char *end = nullptr;
            offset = strtoull(line, &end, 0);
            if (end == line) continue;
        } else if (!smith_sym_request(line, &code, &module, &offset)) {
            continue;
        }
        smith_sym_answer(&protocol, module, offset, code);
    }
    return 0;
}
