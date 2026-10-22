// smith's reader of GNU make's common subset (issue #44): a Makefile in, a build.c out, the text of both in smith's C.
// `smith build --dump-config` in a folder with a Makefile and no build file prints the translation. Included by
// smith.c; the files are runtime/make.c (this one: memory, text, variables), make_expand.c (the expander of $(...)),
// make_read.c (lines, directives, conditionals, rules), make_graph.c (the targets a build needs) and make_emit.c (the
// build.c). Docs: docs/build-c.md, "Translating a Makefile".
//
// How it works. The Makefile is evaluated at translation time, as make does, with the files that are there: variables
// are expanded, `$(wildcard)` is read, a pattern rule is applied to each target that needs one, and each target becomes
// one declaration in build.c. What cannot be known then is kept as a "hole", a C expression that build.c evaluates
// when it runs: the platform (`$(shell uname -s)` is b->os), an environment variable, the project's folder, and a
// variable that `make VAR=value` can set (the option -DVAR=value). A hole travels through the text in-band, as the
// bytes 0x01 <number> 0x02; a conditional that tests one keeps both branches (a variable that differs becomes a
// conditional expression). A variable that a recipe uses as a whole word becomes a named array in build.c; its use is
// marked in-band with 0x03 <variable> 0x04 ... 0x05.
//
// Nothing here recurses and nothing is freed: the memory is one mapped region, every loop is bounded, and an
// expression is expanded by a machine with a stack of its own (make_expand.c).
#include <dirent.h>
#include <glob.h>
#include <stdint.h>
#include <sys/mman.h>

constexpr size_t smith_make_memory_size = (size_t)2 << 30; // reserved; touched as used
constexpr size_t smith_make_vars_max = 8192;
constexpr size_t smith_make_slots = 16384; // the variables' hash table, a power of two
constexpr size_t smith_make_holes_max = 4096;
constexpr size_t smith_make_diags_max = 512;
constexpr size_t smith_make_buffer_size = (size_t)32 << 20;
constexpr size_t smith_make_name_max = 4096;
constexpr size_t smith_make_words_max = (size_t)1 << 20;
constexpr size_t smith_make_input_max = (size_t)16 << 20;
constexpr size_t smith_make_files_max = 64;

// In-band marks (bytes no Makefile holds).
constexpr char smith_make_hole_begin = '\x01';
constexpr char smith_make_hole_end = '\x02';
constexpr char smith_make_use_begin = '\x03';
constexpr char smith_make_use_sep = '\x04';
constexpr char smith_make_use_end = '\x05';

typedef struct {
    const char *file;
    uint32_t line;
} SmithMakeWhere;

typedef struct {
    SmithMakeWhere where;
    const char *message;
} SmithMakeDiag;

// A variable. A simple one holds its expansion (`value`, and `marked` with the uses of other variables marked); a
// recursive one holds its text, expanded where it is used. A knob is a variable `make VAR=value` can set: its value is
// a hole, and `default_value` is what the Makefile gives it.
typedef struct {
    const char *name;
    const char *value;
    const char *marked;
    bool simple;
    bool defined;
    bool knob;
    bool used_as_array;   // some recipe, or an array that is one, uses it as whole words
    bool option;          // a hole refers to its option (-DNAME=value): build.c declares it
    const char *fallback; // a knob's value when no option or environment variable gives one (marked)
    const char *source;   // the text the Makefile assigned, for the comments of build.c
    bool built_in;        // make's own default (CC, CXX, ...)
    bool command_line;    // set in a way a Makefile assignment must not replace (not used yet)
    SmithMakeWhere where;
    uint32_t order; // the order of the first assignment, for emitting arrays in the Makefile's order
} SmithMakeVar;

typedef struct {
    const char *expression; // a C expression of type const char *
} SmithMakeHole;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} SmithMakeBuffer;

typedef struct {
    uint32_t slots[smith_make_slots]; // index + 1 into vars, 0 for empty
    SmithMakeVar vars[smith_make_vars_max];
    size_t var_count;
    SmithMakeHole holes[smith_make_holes_max];
    size_t hole_count;
    SmithMakeDiag diags[smith_make_diags_max];
    size_t diag_count;
    SmithMakeWhere here; // what is being read or expanded, for the diagnostics
    char *pool;
    size_t pool_used;
    const char *root; // the folder the Makefile is in, as the translation was asked for
    const char *file; // the Makefile, relative to root
    uint32_t order;
    bool no_knobs; // an undefined variable is empty, not a knob (make's own rules use CFLAGS and the like)
} SmithMake;

static SmithMake smith_make;

// ---- Memory

static void *smith_make_alloc(size_t size) {
    constexpr size_t align = 16;
    if (smith_make.pool == nullptr) {
        void *region = mmap(nullptr, smith_make_memory_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (region == MAP_FAILED) smith_die(smith_exit_usage, "make: cannot reserve memory", nullptr);
        smith_make.pool = region;
    }
    size_t start = (smith_make.pool_used + align - 1) & ~(align - 1);
    if (size > smith_make_memory_size || start > smith_make_memory_size - size)
        smith_die(smith_exit_usage, "the Makefile is too large to translate", nullptr);
    smith_make.pool_used = start + size;
    return smith_make.pool + start;
}

static const char **smith_make_new_list(size_t count) {
    return (const char **)smith_make_alloc(count * sizeof(char *));
}

static char *smith_make_dup(const char *text, size_t length) {
    char *copy = smith_make_alloc(length + 1);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static const char *smith_make_copy(const char *text) {
    return smith_make_dup(text, strlen(text));
}

[[gnu::format(printf, 1, 2)]] static const char *smith_make_format(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    va_list again;
    va_copy(again, arguments);
    int length = vsnprintf(nullptr, 0, format, arguments);
    va_end(arguments);
    SMITH_ASSERT(length >= 0);
    char *text = smith_make_alloc((size_t)length + 1);
    vsnprintf(text, (size_t)length + 1, format, again);
    va_end(again);
    return text;
}

// ---- Buffers

static void smith_make_buffer_init(SmithMakeBuffer *buffer, size_t capacity) {
    buffer->data = smith_make_alloc(capacity);
    buffer->length = 0;
    buffer->capacity = capacity;
}

static void smith_make_put(SmithMakeBuffer *buffer, const char *text, size_t length) {
    if (length > buffer->capacity - buffer->length)
        smith_die(smith_exit_usage, "make: an expansion is too long", nullptr);
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
}

static void smith_make_put_char(SmithMakeBuffer *buffer, char c) {
    smith_make_put(buffer, &c, 1);
}

static void smith_make_put_text(SmithMakeBuffer *buffer, const char *text) {
    smith_make_put(buffer, text, strlen(text));
}

[[gnu::format(printf, 2, 3)]] static void smith_make_put_format(SmithMakeBuffer *buffer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    size_t room = buffer->capacity - buffer->length;
    int length = vsnprintf(buffer->data + buffer->length, room, format, arguments);
    va_end(arguments);
    if (length < 0 || (size_t)length >= room) smith_die(smith_exit_usage, "make: an expansion is too long", nullptr);
    buffer->length += (size_t)length;
}

// The buffer's text as a string in the pool.
static const char *smith_make_take(const SmithMakeBuffer *buffer, size_t from) {
    SMITH_ASSERT(from <= buffer->length);
    return smith_make_dup(buffer->data + from, buffer->length - from);
}

// ---- Diagnostics

[[gnu::format(printf, 2, 3)]] static void smith_make_report_at(SmithMakeWhere where, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char message[1024];
    vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);
    if (smith_make.diag_count >= smith_make_diags_max) return;
    for (size_t i = 0; i < smith_make.diag_count; i++) { // the same report once
        const SmithMakeDiag *seen = &smith_make.diags[i];
        if (seen->where.line == where.line && seen->where.file == where.file && strcmp(seen->message, message) == 0)
            return;
    }
    smith_make.diags[smith_make.diag_count++] = (SmithMakeDiag){where, smith_make_copy(message)};
}

[[gnu::format(printf, 1, 2)]] static void smith_make_report(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char message[1024];
    vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);
    smith_make_report_at(smith_make.here, "%s", message);
}

// ---- Text

static bool smith_make_blank(char c) {
    return c == ' ' || c == '\t';
}

static bool smith_make_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// The next word of text[*at .. length): its start and length, or false at the end. A hole or a use mark is part of a
// word (they hold no space), and so is what a use mark encloses when it has words in it: callers that need the words
// of a marked text read it with smith_make_words_marked.
static bool smith_make_next_word(const char *text, size_t length, size_t *at, size_t *start, size_t *size) {
    size_t i = *at;
    while (i < length && smith_make_space(text[i]))
        i++;
    if (i >= length) {
        *at = i;
        return false;
    }
    *start = i;
    while (i < length && !smith_make_space(text[i]))
        i++;
    *size = i - *start;
    *at = i;
    return true;
}

static size_t smith_make_trim_length(const char *text, size_t length, size_t *from) {
    size_t begin = 0;
    while (begin < length && smith_make_space(text[begin]))
        begin++;
    size_t end = length;
    while (end > begin && smith_make_space(text[end - 1]))
        end--;
    *from = begin;
    return end - begin;
}

static const char *smith_make_trim(const char *text) {
    size_t from = 0;
    size_t size = smith_make_trim_length(text, strlen(text), &from);
    return smith_make_dup(text + from, size);
}

static bool smith_make_has_hole(const char *text, size_t length) {
    return memchr(text, smith_make_hole_begin, length) != nullptr;
}

// The text without use marks.
static const char *smith_make_unmark(const char *text) {
    size_t length = strlen(text);
    if (strchr(text, smith_make_use_begin) == nullptr && strchr(text, smith_make_use_end) == nullptr) return text;
    char *out = smith_make_alloc(length + 1);
    size_t used = 0;
    bool in_number = false;
    for (size_t i = 0; i < length; i++) { // bounded by the length
        char c = text[i];
        if (c == smith_make_use_begin) {
            in_number = true;
        } else if (c == smith_make_use_sep) {
            in_number = false;
        } else if (c != smith_make_use_end && !in_number) {
            out[used++] = c;
        }
    }
    out[used] = '\0';
    return out;
}

// ---- Words

typedef struct {
    const char *text;
    size_t length;
} SmithMakeWord;

// The words of text into a pool array; *count is how many.
static SmithMakeWord *smith_make_words(const char *text, size_t length, size_t *count) {
    size_t at = 0;
    size_t start = 0;
    size_t size = 0;
    size_t total = 0;
    while (smith_make_next_word(text, length, &at, &start, &size)) // bounded by the length
        total++;
    SmithMakeWord *words = smith_make_alloc((total + 1) * sizeof(SmithMakeWord));
    at = 0;
    total = 0;
    while (smith_make_next_word(text, length, &at, &start, &size)) {
        words[total++] = (SmithMakeWord){text + start, size};
    }
    *count = total;
    return words;
}

static const char *smith_make_word_text(SmithMakeWord word) {
    return smith_make_dup(word.text, word.length);
}

// ---- Patterns

// Whether text matches pattern (a `%` stands for any text, once). The stem is what `%` matched.
static bool smith_make_match(const char *pattern, size_t pattern_length, const char *text, size_t length,
                             const char **stem, size_t *stem_length) {
    const char *percent = memchr(pattern, '%', pattern_length);
    if (percent == nullptr) {
        if (pattern_length != length || memcmp(pattern, text, length) != 0) return false;
        *stem = text;
        *stem_length = 0;
        return true;
    }
    size_t before = (size_t)(percent - pattern);
    size_t after = pattern_length - before - 1;
    if (length < before + after || memcmp(text, pattern, before) != 0 ||
        memcmp(text + length - after, percent + 1, after) != 0)
        return false;
    *stem = text + before;
    *stem_length = length - before - after;
    return true;
}

// pattern with its `%` replaced by stem, or the pattern when it has none.
static const char *smith_make_substitute(const char *pattern, size_t pattern_length, const char *stem,
                                         size_t stem_length) {
    const char *percent = memchr(pattern, '%', pattern_length);
    if (percent == nullptr) return smith_make_dup(pattern, pattern_length);
    size_t before = (size_t)(percent - pattern);
    size_t after = pattern_length - before - 1;
    char *out = smith_make_alloc(before + stem_length + after + 1);
    memcpy(out, pattern, before);
    memcpy(out + before, stem, stem_length);
    memcpy(out + before + stem_length, percent + 1, after);
    out[before + stem_length + after] = '\0';
    return out;
}

// ---- Holes

// The number of the hole for a C expression; one expression is one hole.
static size_t smith_make_hole(const char *expression) {
    for (size_t i = 0; i < smith_make.hole_count; i++) {
        if (strcmp(smith_make.holes[i].expression, expression) == 0) return i;
    }
    if (smith_make.hole_count >= smith_make_holes_max) smith_die(smith_exit_usage, "make: too many options", nullptr);
    smith_make.holes[smith_make.hole_count] = (SmithMakeHole){smith_make_copy(expression)};
    return smith_make.hole_count++;
}

static const char *smith_make_hole_text(const char *expression) {
    return smith_make_format("%c%zu%c", smith_make_hole_begin, smith_make_hole(expression), smith_make_hole_end);
}

// At text[i] there is a hole: its number, and where it ends (past the end mark).
static size_t smith_make_hole_at(const char *text, size_t length, size_t i, size_t *number) {
    SMITH_ASSERT(text[i] == smith_make_hole_begin);
    size_t value = 0;
    size_t j = i + 1;
    while (j < length && text[j] != smith_make_hole_end) { // bounded by the length
        value = value * 10 + (size_t)(text[j] - '0');
        j++;
    }
    SMITH_ASSERT(j < length && value < smith_make.hole_count);
    *number = value;
    return j + 1;
}

// ---- Variables

static uint32_t smith_make_hash(const char *name, size_t length) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < length; i++) {
        hash ^= (unsigned char)name[i];
        hash *= 16777619U;
    }
    return hash;
}

static SmithMakeVar *smith_make_var_find(const char *name, size_t length) {
    uint32_t at = smith_make_hash(name, length) & (smith_make_slots - 1);
    for (size_t probes = 0; probes < smith_make_slots; probes++) {
        uint32_t slot = smith_make.slots[at];
        if (slot == 0) return nullptr;
        SmithMakeVar *var = &smith_make.vars[slot - 1];
        if (strlen(var->name) == length && memcmp(var->name, name, length) == 0) return var;
        at = (at + 1) & (smith_make_slots - 1);
    }
    return nullptr;
}

// The variable of that name, made (undefined) when it is not there.
static SmithMakeVar *smith_make_var_get(const char *name, size_t length) {
    SmithMakeVar *found = smith_make_var_find(name, length);
    if (found != nullptr) return found;
    if (smith_make.var_count >= smith_make_vars_max) smith_die(smith_exit_usage, "make: too many variables", nullptr);
    uint32_t at = smith_make_hash(name, length) & (smith_make_slots - 1);
    for (size_t probes = 0; probes < smith_make_slots && smith_make.slots[at] != 0; probes++)
        at = (at + 1) & (smith_make_slots - 1);
    SmithMakeVar *var = &smith_make.vars[smith_make.var_count];
    *var = (SmithMakeVar){.name = smith_make_dup(name, length), .value = "", .marked = ""};
    smith_make.slots[at] = (uint32_t)++smith_make.var_count;
    return var;
}

static size_t smith_make_var_index(const SmithMakeVar *var) {
    return (size_t)(var - smith_make.vars);
}

// Sets a variable's whole state (what a log restores).
static void smith_make_var_set(SmithMakeVar *var, const char *value, const char *marked, bool simple) {
    var->value = value;
    var->marked = marked;
    var->simple = simple;
    var->defined = true;
    var->where = smith_make.here;
    if (var->order == 0) var->order = ++smith_make.order;
}

// The value of a variable for the Makefile's own text, e.g. `.DEFAULT_GOAL`: nullptr if it is not defined.
static const char *smith_make_var_value(const char *name) {
    const SmithMakeVar *var = smith_make_var_find(name, strlen(name));
    return var != nullptr && var->defined ? var->value : nullptr;
}
