// `smith --smith-explain --pid <pid>`: the values of a failed check, read from the stopped test as a debugger would.
// Included by smith.c after symbolize.c, whose rules for the failing frame and whose source reader it uses.
//
// The test runner (lib/smith/test.smith, option --explain <smith>) starts this as a child of the worker that has just
// failed a check or a contract, while that worker waits for it. The program does not capture anything: this attaches
// to it with lldb (shim/lldb_explain.cpp), takes the first frame that is not smith's own check machinery (the frame
// smith symbolize reports), reads its source line, and reads each variable the line names, by path, as `frame variable`
// does: `moves.count`, `expected`, `board->cells[row][col]` (an index that is a variable is read first and replaced by
// its value). No expression is evaluated and no function is called. What it finds goes to standard output, one line
// each, `@ name = value` (the names padded to one width), and `@! text` for a note: why nothing could be shown (the
// machine refuses to debug, or this smith has no debugger). The runner puts the lines in the failure's details and
// `smith symbolize` prints them under the source line. The process exits 0 whatever it found.
//
// Without lldb in smith (the stage0 and an LLVM install without lldb) there are no values and one note. Time: the
// attach, the symbols of the program and the reads, about a quarter of a second
// (knowledge/failure-reports-and-tests.md).

#include <ctype.h>

#include "../shim/lldb_explain.h"

constexpr size_t smith_explain_frames_max = 64;
constexpr size_t smith_explain_values_max = 12;
constexpr size_t smith_explain_name_max = 128;
constexpr size_t smith_explain_value_max = 200;
constexpr int smith_explain_exit_unavailable = 3;

#ifdef SMITH_LLDB
constexpr bool smith_explain_has_lldb = true;
#else
constexpr bool smith_explain_has_lldb = false;

// The stage0 and an LLVM install without lldb: nothing to attach with.
bool smith_explain_attach(long pid, const char *self, char *problem, size_t capacity) {
    (void)pid;
    (void)self;
    snprintf(problem, capacity, "this smith has no debugger");
    return false;
}

size_t smith_explain_frames(SmithExplainFrame *out, size_t capacity) {
    (void)out;
    (void)capacity;
    return 0;
}

SmithExplainState smith_explain_read(size_t frame, const char *path, char *out, size_t capacity, long long *integer) {
    (void)frame;
    (void)path;
    (void)out;
    (void)capacity;
    (void)integer;
    return SmithExplain_Missing;
}

void smith_explain_detach(void) {}
#endif

typedef struct {
    char label[smith_explain_name_max];
    char value[smith_explain_value_max];
} SmithExplainValue;

static bool smith_explain_ident_start(char c) {
    return isalpha((unsigned char)c) != 0 || c == '_';
}

static bool smith_explain_ident_char(char c) {
    return isalnum((unsigned char)c) != 0 || c == '_';
}

// Words of the language that are not variables: keywords, constants and the type names a line may spell.
static bool smith_explain_reserved(const char *text, size_t length) {
    static const char *const words[] = {
        "if",     "else",      "for",      "while",  "do",     "switch", "case",    "break", "return",
        "sizeof", "alignof",   "countof",  "typeof", "true",   "false",  "nullptr", "NULL",  "auto",
        "const",  "constexpr", "static",   "void",   "bool",   "char",   "short",   "int",   "long",
        "float",  "double",    "unsigned", "signed", "struct", "enum",   "union",   "u8",    "u16",
        "u32",    "u64",       "i8",       "i16",    "i32",    "i64",    "usize",   "isize", "f16",
        "f32",    "f64",       "size_t",   "export", "import", "module", "type"};
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++)
        if (strlen(words[i]) == length && strncmp(words[i], text, length) == 0) return true;
    return false;
}

// The end of the brackets that open at text[0], or nullptr when they do not close on this line.
static const char *smith_explain_close(const char *text) {
    int depth = 0;
    for (const char *c = text; *c != '\0'; c++) { // bounded by the line
        if (*c == '[') depth += 1;
        if (*c == ']' && --depth == 0) return c;
    }
    return nullptr;
}

// How many bytes of the path that starts at text[0], an identifier: `a`, `a.b`, `p->q[row][col]`, `box::origin`.
static size_t smith_explain_extent(const char *text) {
    size_t at = 0;
    for (size_t step = 0; step < smith_explain_name_max; step++) { // an identifier, then `::` and more of them
        while (smith_explain_ident_char(text[at]))
            at += 1;
        if (!(text[at] == ':' && text[at + 1] == ':' && smith_explain_ident_start(text[at + 2]))) break;
        at += 2;
    }
    for (size_t step = 0; step < 32; step++) { // a path has a few members and indexes
        if (text[at] == '.' && smith_explain_ident_start(text[at + 1])) {
            at += 1;
        } else if (text[at] == '-' && text[at + 1] == '>' && smith_explain_ident_start(text[at + 2])) {
            at += 2;
        } else if (text[at] == '[') {
            const char *close = smith_explain_close(text + at);
            if (close == nullptr) break;
            at = (size_t)(close - text) + 1;
            continue;
        } else {
            break;
        }
        while (smith_explain_ident_char(text[at]))
            at += 1;
    }
    return at;
}

static bool smith_explain_listed(const SmithExplainValue *values, size_t count, const char *label) {
    for (size_t i = 0; i < count; i++)
        if (strcmp(values[i].label, label) == 0) return true;
    return false;
}

// The paths a source line names, in order and once each, in values[].label. A name followed by `(` is a call, not a
// value; the arguments are read on. Literals and comments are skipped.
static size_t smith_explain_paths(const char *source, SmithExplainValue *values, size_t capacity) {
    size_t count = 0;
    const char *c = source;
    for (size_t step = 0; *c != '\0' && count < capacity && step < strlen(source) + 1; step++) {
        if (c[0] == '/' && c[1] == '/') break;
        if (*c == '"' || *c == '\'') { // a string or character literal
            const char *end = strchr(c + 1, *c);
            c = end != nullptr ? end + 1 : c + strlen(c);
            continue;
        }
        if (isdigit((unsigned char)*c) != 0) { // a number, with its suffix and digit separators
            while (smith_explain_ident_char(*c) || *c == '.' || *c == '\'')
                c += 1;
            continue;
        }
        if (!smith_explain_ident_start(*c)) {
            c += 1;
            continue;
        }
        bool member = (c > source && c[-1] == '.') || (c > source + 1 && c[-1] == '>' && c[-2] == '-');
        size_t length = smith_explain_extent(c);
        const char *after = c + length + strspn(c + length, " \t");
        bool call = *after == '(';
        if (!member && !call && !smith_explain_reserved(c, length) && length < smith_explain_name_max) {
            char label[smith_explain_name_max];
            snprintf(label, sizeof label, "%.*s", (int)length, c);
            if (!smith_explain_listed(values, count, label)) snprintf(values[count++].label, sizeof label, "%s", label);
        }
        c += length;
    }
    return count;
}

static bool smith_explain_digits(const char *text) {
    if (*text == '\0') return false;
    for (; *text != '\0'; text++)
        if (isdigit((unsigned char)*text) == 0) return false;
    return true;
}

// The text of an index with its blanks trimmed.
static void smith_explain_inner(const char *open, const char *close, char *inner, size_t capacity) {
    const char *from = open + 1 + strspn(open + 1, " \t");
    size_t length = (size_t)(close - from);
    while (length > 0 && (from[length - 1] == ' ' || from[length - 1] == '\t'))
        length -= 1;
    snprintf(inner, capacity, "%.*s", (int)length, from);
}

// What lldb is asked to read for a label: the same path with each index that is a variable replaced by its value
// (`cells[row][col]` -> `cells[1][2]`). An index that is neither a number nor a path (`i + 1`) ends the path before it,
// so the array is shown whole.
static bool smith_explain_lookup(size_t frame, const char *label, char *lookup, size_t capacity) {
    size_t used = 0;
    for (const char *c = label; *c != '\0' && used + 24 < capacity;) { // bounded by the label
        if (*c != '[') {
            lookup[used++] = *c++;
            continue;
        }
        const char *close = smith_explain_close(c);
        char inner[smith_explain_name_max];
        smith_explain_inner(c, close != nullptr ? close : c, inner, sizeof inner);
        long long number = 0;
        if (close == nullptr || inner[0] == '\0') break;
        if (smith_explain_digits(inner)) {
            number = strtoll(inner, nullptr, 10);
        } else {
            char text[smith_explain_value_max];
            bool simple = smith_explain_ident_start(inner[0]) && smith_explain_extent(inner) == strlen(inner) &&
                          strchr(inner, '[') == nullptr;
            if (!simple || smith_explain_read(frame, inner, text, sizeof text, &number) != SmithExplain_Ok) break;
        }
        used += (size_t)snprintf(lookup + used, capacity - used, "[%lld]", number);
        c = close + 1;
    }
    lookup[used] = '\0';
    return used > 0;
}

// Shortens a path by its last member or index: `a.b[1].c` -> `a.b[1]` -> `a.b` -> `a`. False when nothing is left to
// cut.
static bool smith_explain_shorten(char *label) {
    size_t length = strlen(label);
    for (size_t at = length; at > 0; at--) { // bounded by the label
        char c = label[at - 1];
        bool arrow = c == '>' && at > 1 && label[at - 2] == '-';
        if (c != '.' && c != '[' && !arrow) continue;
        label[arrow ? at - 2 : at - 1] = '\0';
        return label[0] != '\0';
    }
    return false;
}

// Reads one path of the line: the whole path, else the longest start of it that is a variable (a member that debug info
// does not have is not worth losing the variable for). The label that was read is left in label.
static bool smith_explain_read_path(size_t frame, char *label, char *text, size_t capacity) {
    for (size_t step = 0; step < 16; step++) { // a path has a few members and indexes
        char lookup[smith_explain_name_max];
        if (smith_explain_lookup(frame, label, lookup, sizeof lookup) &&
            smith_explain_read(frame, lookup, text, capacity, nullptr) != SmithExplain_Missing)
            return true;
        if (!smith_explain_shorten(label)) return false;
    }
    return false;
}

// Reads every path of the line in the frame. A path that is not a variable (a function, a type, a module's name) is
// dropped. Returns how many values are kept, in values[].
static size_t smith_explain_read_all(size_t frame, const char *source, SmithExplainValue *values) {
    SmithExplainValue paths[smith_explain_values_max * 2];
    size_t found = smith_explain_paths(source, paths, sizeof paths / sizeof paths[0]);
    size_t kept = 0;
    for (size_t i = 0; i < found && kept < smith_explain_values_max; i++) {
        char text[smith_explain_value_max];
        if (!smith_explain_read_path(frame, paths[i].label, text, sizeof text)) continue;
        if (smith_explain_listed(values, kept, paths[i].label)) continue;
        snprintf(values[kept].label, sizeof values[kept].label, "%s", paths[i].label);
        snprintf(values[kept].value, sizeof values[kept].value, "%s", text);
        kept += 1;
    }
    return kept;
}

// The first frame that is not smith's check machinery and has a line: the one `smith symbolize` reports. The frames
// inside the machinery (the runner waiting for this process, a sanitizer's wrapper of poll) are not looked at: the
// search starts at the first frame of the machinery.
static bool smith_explain_failing(const SmithExplainFrame *frames, size_t count, size_t *index) {
    size_t first = 0;
    while (first < count && !smith_sym_machinery(frames[first].function))
        first += 1;
    if (first == count) first = 0;
    for (size_t i = first; i < count; i++) {
        if (frames[i].file[0] == '\0' || smith_sym_machinery(frames[i].function)) continue;
        if (smith_sym_system_path(frames[i].file)) continue;
        *index = i;
        return true;
    }
    return false;
}

static void smith_explain_print(const SmithExplainValue *values, size_t count) {
    size_t width = 0;
    for (size_t i = 0; i < count; i++)
        if (strlen(values[i].label) > width) width = strlen(values[i].label);
    for (size_t i = 0; i < count; i++)
        printf("@ %-*s = %s\n", (int)width, values[i].label, values[i].value);
}

static int smith_explain_run(long pid, const char *self) {
    static SmithExplainFrame frames[smith_explain_frames_max];
    char problem[512];
    if (!smith_explain_attach(pid, self, problem, sizeof problem)) {
        char *sentence = strstr(problem, ". "); // lldb says a paragraph; the first sentence is the reason
        if (sentence != nullptr) *sentence = '\0';
        printf("@! values not shown: the debugger could not attach (%s; see the smith-debug skill)\n", problem);
        return 0;
    }
    size_t count = smith_explain_frames(frames, smith_explain_frames_max);
    size_t index = 0;
    char source[1024];
    if (!smith_explain_failing(frames, count, &index) ||
        !smith_sym_source(frames[index].file, frames[index].line, source, sizeof source)) {
        printf("@! values not shown: no source line for the failing frame\n");
        smith_explain_detach();
        return 0;
    }
    SmithExplainValue values[smith_explain_values_max];
    size_t kept = smith_explain_read_all(index, source, values);
    smith_explain_detach();
    smith_explain_print(values, kept);
    if (kept == 0) printf("@! values not shown: no variable of the line could be read\n");
    return 0;
}

// `smith --smith-explain --pid <pid>` and `smith --smith-explain --available`.
static int smith_explain_main(int argc, char **argv, const char *self) {
    long pid = 0;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--available") == 0) return smith_explain_has_lldb ? 0 : smith_explain_exit_unavailable;
        if (strcmp(argv[i], "--pid") == 0 && i + 1 < argc) pid = strtol(argv[++i], nullptr, 10);
    }
    if (pid <= 0) smith_die(smith_exit_usage, "usage: smith --smith-explain --pid <pid>", nullptr);
    alarm(30); // a backstop: the runner ends this process at its own deadline
    int status = smith_explain_run(pid, self);
    fflush(stdout);
    return status;
}
