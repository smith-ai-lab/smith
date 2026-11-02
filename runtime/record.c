// Per-test records (issue #456, knowledge/coverage-selection.md). Included by smith.c after runtime/coverage.c.
//
// Every test that ran under coverage keeps a record in `.smith/records/<test>.rec` (or $SMITH_RECORDS_DIR), keyed by
// content only, so it needs no repository and no commit, and a record that came from elsewhere is as good as one made
// here: a hash that still matches means the same thing. Text, a tab between fields:
//   smith-record 2
//   test <name>
//   result <pass|fail>
//   opened 1                                            the record lists what the test opened (read, readdir lines)
//   fn <file> <name> <lines> <hash> <first line hash>    a function the test executed, found again by its first line
//   outside <file> <hash>                               the file's code outside its functions (globals, macros, types,
//   the
//                                                       lines of a table's braces): for each executed file, and each
//                                                       .c or .h file one of them includes, chains included
//   input <path> <hash>                                  any other file the executed files include, and build.c
//   read <path> <hash>                                   a file of the project the test or its children read
//   readdir <path>/ <hash>                              a folder of the project they listed: the names in it
// A test is stale when its last result failed, a function it executed is not found as it was, an outside hash or an
// input or a read file differs (or is gone). A hash is 64-bit FNV-1a; the guard is against edits, not forgery.
constexpr size_t smith_rec_source_max = 1U << 23;
constexpr size_t smith_rec_records_max = 8192;
constexpr size_t smith_rec_seen_max = 1024;
constexpr size_t smith_rec_mention_slots = 1U << 15;

static char smith_rec_source[smith_rec_source_max + 1];
static uint64_t smith_rec_line_hash[smith_cov_lines_max];
static bool smith_rec_line_code[smith_cov_lines_max];
static bool smith_rec_line_opens[smith_cov_lines_max];  // a function's opening line: `...) {`
static bool smith_rec_line_inside[smith_cov_lines_max]; // a line of a function, its opening and closing lines included
static size_t smith_rec_line_count;
static char smith_rec_source_path[PATH_MAX];
static bool smith_rec_source_ok;
static const char *smith_rec_seen[smith_rec_seen_max];
static size_t smith_rec_seen_count;

// Judged elsewhere (runtime/changed.c, issue #456): functions a change reaches through an `#embed` (file, name), and an
// input whose change is not a reason by itself (build.c, when its diff touches only functions no test runs).
constexpr size_t smith_rec_touched_max = 64;
static const char *smith_rec_touched_file[smith_rec_touched_max];
static const char *smith_rec_touched_name[smith_rec_touched_max];
static size_t smith_rec_touched_count;
static const char *smith_rec_ignored_input;

static bool smith_rec_touched(const char *file, const char *name) {
    size_t length = strlen(name);
    for (size_t i = 0; i < smith_rec_touched_count; i++) { // bounded by the touched functions
        if (strcmp(smith_rec_touched_file[i], file) != 0) continue;
        const char *want = smith_rec_touched_name[i];
        size_t tail = strlen(want);
        if (length >= tail && strcmp(name + length - tail, want) == 0 &&
            (length == tail || name[length - tail - 1] == ':'))
            return true;
    }
    return false;
}

static uint64_t smith_rec_fnv(const void *data, size_t size, uint64_t hash) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) { // bounded by the data
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static constexpr uint64_t smith_rec_seed = 14695981039346656037ULL;

static uint64_t smith_rec_mix(uint64_t hash, uint64_t value) {
    return smith_rec_fnv(&value, sizeof value, hash);
}

static void smith_rec_absolute(const char *relative, char *out, size_t size) {
    snprintf(out, size, "%s/%s", smith_cov_root, relative);
}

// Reads a file of the project whole into smith_rec_source; its size, or 0 when it cannot be read or is empty.
static size_t smith_rec_read(const char *relative) {
    char path[PATH_MAX];
    smith_rec_absolute(relative, path, sizeof path);
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) return 0;
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return 0;
    size_t size = fread(smith_rec_source, 1, smith_rec_source_max, file);
    if (size == smith_rec_source_max && fgetc(file) != EOF) smith_cov_cross("a source file a record hashes (8 MiB)");
    fclose(file);
    smith_rec_source[size] = '\0';
    return size;
}

// The hash of a whole file, or false when it is gone.
static bool smith_rec_file_hash(const char *relative, uint64_t *hash) {
    size_t size = smith_rec_read(relative);
    if (size == 0) return false;
    *hash = smith_rec_fnv(smith_rec_source, size, smith_rec_seed);
    return true;
}

// The braces of a file as its lines are read. The `{` of a namespace or of `extern "C"` (a C++ file) is not a depth:
// the functions in it are the file's functions.
constexpr size_t smith_rec_levels_max = 64;

typedef struct {
    int depth;                         // the open braces that count
    bool in_comment;                   // a block comment continues across lines
    size_t levels;                     // the open braces of all kinds
    bool counts[smith_rec_levels_max]; // whether each of the first ones counts as depth
} SmithRecBraces;

static bool smith_rec_transparent(const char *line, size_t length) {
    while (length > 0 && (*line == ' ' || *line == '\t')) {
        line++;
        length--;
    }
    return (length >= 9 && memcmp(line, "namespace", 9) == 0) || (length >= 8 && memcmp(line, "extern \"", 8) == 0);
}

// The braces after one line, ignoring strings, character constants and comments.
static void smith_rec_braces_after(const char *line, size_t length, SmithRecBraces *state) {
    bool first = smith_rec_transparent(line, length); // the first `{` of such a line is the namespace's
    for (size_t i = 0; i < length; i++) {             // bounded by the line
        char c = line[i];
        if (state->in_comment) {
            if (c == '*' && i + 1 < length && line[i + 1] == '/') {
                state->in_comment = false;
                i++;
            }
        } else if (c == '/' && i + 1 < length && line[i + 1] == '/') {
            break;
        } else if (c == '/' && i + 1 < length && line[i + 1] == '*') {
            state->in_comment = true;
            i++;
        } else if (c == '"' || c == '\'') {
            for (i++; i < length && line[i] != c; i++) { // bounded by the line
                if (line[i] == '\\') i++;
            }
        } else if (c == '{') {
            bool counts = !first;
            first = false;
            if (state->levels < smith_rec_levels_max) state->counts[state->levels] = counts;
            state->levels++;
            if (counts) state->depth++;
        } else if (c == '}' && state->levels > 0) {
            state->levels--;
            bool counts = state->levels >= smith_rec_levels_max || state->counts[state->levels];
            if (counts && state->depth > 0) state->depth--;
        }
    }
}

// Loads a source file into smith_rec_source and the tables of its lines (1-based): hash, depth, whether it is code.
static bool smith_rec_load_source(const char *relative) {
    if (strcmp(relative, smith_rec_source_path) == 0) return smith_rec_source_ok;
    snprintf(smith_rec_source_path, sizeof smith_rec_source_path, "%s", relative);
    smith_rec_line_count = 0;
    smith_rec_source_ok = smith_rec_read(relative) > 0;
    if (!smith_rec_source_ok) return false;
    SmithRecBraces braces = {};
    bool in_function = false;
    char *line = smith_rec_source;
    for (size_t guard = 0; *line != '\0' && guard < smith_cov_lines_max - 1; guard++) { // bounded by the lines
        char *end = strchr(line, '\n');
        size_t length = end != nullptr ? (size_t)(end - line) : strlen(line);
        size_t at = ++smith_rec_line_count;
        smith_rec_line_hash[at] = smith_rec_fnv(line, length, smith_rec_seed);
        char saved = line[length];
        line[length] = '\0';
        smith_rec_line_code[at] = !braces.in_comment && smith_cov_code_line(line);
        size_t trimmed = length;
        while (trimmed > 0 && (line[trimmed - 1] == ' ' || line[trimmed - 1] == '\r')) // trailing blanks
            trimmed--;
        smith_rec_line_opens[at] = braces.depth == 0 && trimmed >= 3 && line[trimmed - 1] == '{' &&
                                   strstr(line, ")") != nullptr && strchr(line, '=') == nullptr;
        in_function = in_function || smith_rec_line_opens[at];
        smith_rec_line_inside[at] = in_function;
        smith_rec_braces_after(line, length, &braces);
        if (braces.depth == 0) in_function = false;
        line[length] = saved;
        if (end == nullptr) break;
        line = end + 1;
    }
    return true;
}

static uint64_t smith_rec_range(size_t first, size_t last) {
    uint64_t hash = smith_rec_seed;
    for (size_t line = first; line <= last && line <= smith_rec_line_count; line++) // bounded by the lines
        hash = smith_rec_mix(hash, smith_rec_line_hash[line]);
    return hash;
}

// The hash of the code outside functions: every line that is code and no part of a function (its opening line
// `...) {`, its body, its closing `}`), so adding or moving a function changes nothing here while a global, a macro, a
// declaration or a line inside the braces of a table or a type does.
static uint64_t smith_rec_outside(void) {
    uint64_t hash = smith_rec_seed;
    for (size_t line = 1; line <= smith_rec_line_count; line++) { // bounded by the lines
        if (!smith_rec_line_code[line] || smith_rec_line_inside[line]) continue;
        hash = smith_rec_mix(hash, smith_rec_line_hash[line]);
    }
    return hash;
}

// Whether a function with `count` lines, first line hash `first` and hash `hash` is somewhere in the loaded file.
static bool smith_rec_find(uint64_t first, size_t count, uint64_t hash) {
    for (size_t line = 1; line + count - 1 <= smith_rec_line_count; line++) { // bounded by the lines
        if (smith_rec_line_hash[line] == first && smith_rec_range(line, line + count - 1) == hash) return true;
    }
    return false;
}

// ---- Writing a record

static bool smith_rec_once(const char *path) {
    for (size_t i = 0; i < smith_rec_seen_count; i++) { // bounded by the files
        if (strcmp(smith_rec_seen[i], path) == 0) return false;
    }
    if (smith_rec_seen_count < smith_rec_seen_max) smith_rec_seen[smith_rec_seen_count++] = smith_cov_copy(path);
    return true;
}

static void smith_rec_write_input(FILE *out, const char *kind, const char *relative) {
    uint64_t hash = 0;
    if (!smith_rec_once(relative) || !smith_rec_file_hash(relative, &hash)) return;
    fprintf(out, "%s\t%s\t%016llx\n", kind, relative, (unsigned long long)hash);
    smith_rec_source_path[0] = '\0'; // smith_rec_source holds another file now
}

// The `outside` line of one file, once: its globals, macros, types and tables (function bodies are the `fn` lines'
// business). False when the file was written already or cannot be read.
static bool smith_rec_write_outside(FILE *out, const char *file) {
    if (!smith_rec_once(file) || !smith_rec_load_source(file)) return false;
    fprintf(out, "outside\t%s\t%016llx\n", file, (unsigned long long)smith_rec_outside());
    return true;
}

// The files that, once an included .c or .h file is found, are followed for their own includes (the lowering is
// runtime/lang.c and the .c files it includes): a chain is as long as the project's includes.
constexpr size_t smith_rec_queue_max = 512;
static const char *smith_rec_queue[smith_rec_queue_max];
static size_t smith_rec_queue_count;

// Where `#include "name"` written in `file` is, as a path of the project: beside the file, in include/, or at the root.
static bool smith_rec_resolve(const char *file, const char *name, char *relative, size_t size) {
    char beside[PATH_MAX];
    snprintf(beside, sizeof beside, "%s", file);
    char *slash = strrchr(beside, '/');
    if (slash != nullptr)
        slash[1] = '\0';
    else
        beside[0] = '\0';
    const char *const folders[] = {beside, "include/", "", nullptr};
    for (size_t f = 0; folders[f] != nullptr; f++) { // a short list
        char absolute[PATH_MAX];
        snprintf(relative, size, "%s%s", folders[f], name);
        smith_rec_absolute(relative, absolute, sizeof absolute);
        struct stat info;
        if (stat(absolute, &info) == 0 && S_ISREG(info.st_mode)) return true;
    }
    return false;
}

// The files an executed file includes by `#include "x"` or `<x>`, as paths of the project. A .c or .h file is judged
// like the executed ones (`fn` lines for what ran, `outside` for the rest), so a change to one function of it is
// a change to that function; any other file is an input as a whole. The files these include are followed too.
static void smith_rec_write_includes(FILE *out, const char *file) {
    if (!smith_rec_load_source(file)) return;
    static char text[smith_rec_source_max + 1];
    memcpy(text, smith_rec_source, strlen(smith_rec_source) + 1);
    smith_rec_source_path[0] = '\0';
    for (char *at = strstr(text, "#include"); at != nullptr; at = strstr(at + 8, "#include")) { // each include
        char name[PATH_MAX];
        char relative[PATH_MAX];
        const char *start = at + 8 + strspn(at + 8, " \t");
        char close = '\0';
        if (*start == '<') close = '>';
        if (*start == '"') close = '"';
        if (close == '\0') continue;
        snprintf(name, sizeof name, "%.*s", (int)strcspn(start + 1, close == '>' ? ">\n" : "\"\n"), start + 1);
        if (!smith_rec_resolve(file, name, relative, sizeof relative)) continue;
        size_t length = strlen(relative);
        bool code =
            length > 2 && (strcmp(relative + length - 2, ".c") == 0 || strcmp(relative + length - 2, ".h") == 0);
        if (!code) {
            smith_rec_write_input(out, "input", relative);
        } else if (smith_rec_write_outside(out, relative) && smith_rec_queue_count < smith_rec_queue_max) {
            smith_rec_queue[smith_rec_queue_count++] = smith_cov_copy(relative);
        }
        smith_rec_source_path[0] = '\0';
    }
}

// The listing of a folder as a hash: the names in it, in any order. False when it cannot be listed.
static bool smith_rec_dir_hash(const char *relative, uint64_t *hash) {
    char path[PATH_MAX];
    smith_rec_absolute(relative, path, sizeof path);
    DIR *folder = opendir(path);
    if (folder == nullptr) return false;
    uint64_t sum = 0;
    uint64_t count = 0;
    for (struct dirent *item = readdir(folder); item != nullptr; item = readdir(folder)) { // each entry
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
        sum ^= smith_rec_fnv(item->d_name, strlen(item->d_name), smith_rec_seed);
        count++;
    }
    closedir(folder);
    *hash = smith_rec_mix(sum, count);
    return true;
}

static bool smith_rec_inert_path(const char *relative) {
    return strncmp(relative, ".smith/", 7) == 0 || strncmp(relative, "build/", 6) == 0 ||
           strncmp(relative, ".git/", 5) == 0;
}

// A folder the test listed: the names in it (a file added or removed changes the hash). What is in it is in the record
// only if the test opened it.
static void smith_rec_write_folder(FILE *out, const char *relative) {
    uint64_t hash = 0;
    if (!smith_rec_once(relative) || !smith_rec_dir_hash(relative, &hash)) return;
    fprintf(out, "readdir\t%s\t%016llx\n", relative, (unsigned long long)hash);
}

// The files the test read, one path a line in $SMITH_COVERAGE_DIR/reads/<clean>.txt (written by io's files and by the
// scratch projects through SMITH_READS_LOG, issue #739): those of the project. A line ending in `/` is a folder that
// was listed.
static void smith_rec_write_reads(FILE *out, const char *dir, const char *clean) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/reads/%s.txt", dir, clean);
    static char list[1U << 22];
    FILE *file = fopen(path, "r");
    if (file == nullptr) return;
    size_t size = fread(list, 1, sizeof list - 1, file);
    if (size == sizeof list - 1 && fgetc(file) != EOF) smith_cov_cross("a test's list of files read (4 MiB)");
    fclose(file);
    list[size] = '\0';
    char root_text[PATH_MAX];
    const char *root_given = realpath(smith_cov_root, root_text) != nullptr ? root_text : smith_cov_root;
    size_t root = strlen(root_given);
    for (char *line = strtok(list, "\n"); line != nullptr; line = strtok(nullptr, "\n")) { // each path
        if (strncmp(line, root_given, root) != 0 || line[root] != '/') continue;
        char *relative = line + root + 1;
        size_t length = strlen(relative);
        if (relative[0] == '\0' || smith_rec_inert_path(relative)) continue;
        if (relative[length - 1] == '/')
            smith_rec_write_folder(out, relative);
        else
            smith_rec_write_input(out, "read", relative);
    }
}

// The functions of one file the test executed (functions [from, to) of the lcov), and the file's outside hash.
static void smith_rec_write_file(FILE *out, size_t from, size_t to) {
    const char *file = smith_cov_files[smith_cov_functions[from].file];
    bool any = false;
    for (size_t f = from; f < to; f++)
        any = any || smith_cov_function_run[f];
    if (!any || !smith_rec_load_source(file)) return;
    for (size_t f = from; f < to; f++) { // bounded by the functions of the file
        const SmithCovFunction *fn = &smith_cov_functions[f];
        if (!smith_cov_function_run[f] || fn->last < fn->first || fn->last > smith_rec_line_count) continue;
        fprintf(out, "fn\t%s\t%s\t%u\t%016llx\t%016llx\n", file, fn->name, fn->last - fn->first + 1,
                (unsigned long long)smith_rec_range(fn->first, fn->last),
                (unsigned long long)smith_rec_line_hash[fn->first]);
    }
    (void)smith_rec_write_outside(out, file);
}

static bool smith_rec_write(const char *dir, const char *name, const char *clean, bool pass) {
    const char *given = smith_cov_env("SMITH_RECORDS_DIR");
    char folder[PATH_MAX];
    char path[PATH_MAX + 8];
    char temporary[PATH_MAX + 40];
    snprintf(folder, sizeof folder, "%s", given != nullptr ? given : "");
    if (given == nullptr) snprintf(folder, sizeof folder, "%s/.smith/records", smith_cov_root);
    if (given == nullptr) {
        snprintf(path, sizeof path, "%s/.smith", smith_cov_root);
        (void)mkdir(path, 0755);
    }
    (void)mkdir(folder, 0755);
    snprintf(path, sizeof path, "%s/%s.rec", folder, clean);
    FILE *out = smith_cov_open(path, temporary, sizeof temporary);
    if (out == nullptr) return false;
    fprintf(out, "smith-record 2\ntest\t%s\nresult\t%s\ncpu\t%.3f\nopened\t1\n", name, pass ? "pass" : "fail",
            smith_cov_cpu_seconds);
    smith_rec_seen_count = 0;
    smith_rec_queue_count = 0;
    smith_rec_source_path[0] = '\0';
    for (size_t f = 0; f < smith_cov_function_count;) { // a run of functions of one file
        size_t to = f + 1;
        while (to < smith_cov_function_count && smith_cov_functions[to].file == smith_cov_functions[f].file)
            to++;
        smith_rec_write_file(out, f, to);
        smith_rec_write_includes(out, smith_cov_files[smith_cov_functions[f].file]);
        f = to;
    }
    for (size_t i = 0; i < smith_rec_queue_count; i++) // the includes of the included files, which may add more
        smith_rec_write_includes(out, smith_rec_queue[i]);
    smith_rec_write_input(out, "input", "build.c");
    smith_rec_write_reads(out, dir, clean);
    return smith_cov_close(out, temporary, path);
}

// ---- Reading records

constexpr size_t smith_rec_named_max = 1U << 18;

typedef struct {
    const char *name;
    bool stale;
    const char *why;
    size_t named_first; // its files in smith_rec_named: what its program was built from
    size_t named_count;
    double cpu;  // seconds the test took when it was recorded, 0 when unknown
    bool opened; // the record lists what the test opened (#739): a file it never opened is no input of it
} SmithRecResult;

static const char *smith_rec_named[smith_rec_named_max];
static size_t smith_rec_named_used;

// Remembers that the record's test touched `path` (one entry for each).
static void smith_rec_name(SmithRecResult *result, const char *path) {
    for (size_t i = 0; i < result->named_count; i++) { // bounded by the files of the record
        if (strcmp(smith_rec_named[result->named_first + i], path) == 0) return;
    }
    if (smith_rec_named_used >= smith_rec_named_max) return;
    smith_rec_named[smith_rec_named_used++] = smith_cov_copy(path);
    result->named_count++;
}

static SmithRecResult smith_rec_results[smith_rec_records_max];
static size_t smith_rec_result_count;
static const char *smith_rec_mentions[smith_rec_mention_slots];

static void smith_rec_mention(const char *path) {
    size_t slot = (size_t)smith_rec_fnv(path, strlen(path), smith_rec_seed) % smith_rec_mention_slots;
    for (size_t probe = 0; probe < smith_rec_mention_slots; probe++, slot = (slot + 1) % smith_rec_mention_slots) {
        if (smith_rec_mentions[slot] == nullptr) {
            smith_rec_mentions[slot] = smith_cov_copy(path);
            return;
        }
        if (strcmp(smith_rec_mentions[slot], path) == 0) return;
    }
}

static bool smith_rec_mentioned_exactly(const char *path) {
    size_t slot = (size_t)smith_rec_fnv(path, strlen(path), smith_rec_seed) % smith_rec_mention_slots;
    for (size_t probe = 0; probe < smith_rec_mention_slots && smith_rec_mentions[slot] != nullptr;
         probe++, slot = (slot + 1) % smith_rec_mention_slots) {
        if (strcmp(smith_rec_mentions[slot], path) == 0) return true;
    }
    return false;
}

// Whether some record names the path (an executed file, an input or a read), or the folder it is in (a listing).
static bool smith_rec_mentioned(const char *path) {
    if (smith_rec_mentioned_exactly(path)) return true;
    for (const char *slash = strchr(path, '/'); slash != nullptr; slash = strchr(slash + 1, '/')) { // each folder above
        char folder[PATH_MAX];
        snprintf(folder, sizeof folder, "%.*s/", (int)(slash - path), path);
        if (smith_rec_mentioned_exactly(folder)) return true;
    }
    return false;
}

// The outside hash of a file now. Most records name the same files, so a file is read once for all of them.
constexpr size_t smith_rec_outside_cache_max = 1024;
static const char *smith_rec_outside_path[smith_rec_outside_cache_max];
static uint64_t smith_rec_outside_hash[smith_rec_outside_cache_max];
static bool smith_rec_outside_ok[smith_rec_outside_cache_max];
static size_t smith_rec_outside_count;

static bool smith_rec_outside_now(const char *path, uint64_t *hash) {
    for (size_t i = 0; i < smith_rec_outside_count; i++) { // bounded by the files
        if (strcmp(smith_rec_outside_path[i], path) != 0) continue;
        *hash = smith_rec_outside_hash[i];
        return smith_rec_outside_ok[i];
    }
    bool ok = smith_rec_load_source(path);
    *hash = ok ? smith_rec_outside() : 0;
    if (smith_rec_outside_count < smith_rec_outside_cache_max) {
        smith_rec_outside_path[smith_rec_outside_count] = smith_cov_copy(path);
        smith_rec_outside_hash[smith_rec_outside_count] = *hash;
        smith_rec_outside_ok[smith_rec_outside_count++] = ok;
    }
    return ok;
}

static const char *smith_rec_stale_line(char *const *field, size_t count) {
    uint64_t hash = 0;
    if (count >= 2 && strcmp(field[0], "result") == 0)
        return strcmp(field[1], "pass") == 0 ? nullptr : "it failed last time";
    if (count >= 6 && strcmp(field[0], "fn") == 0) {
        smith_rec_mention(field[1]);
        if (!smith_rec_load_source(field[1])) return "a file it executed is gone";
        bool found = smith_rec_find(strtoull(field[5], nullptr, 16), strtoul(field[3], nullptr, 10),
                                    strtoull(field[4], nullptr, 16));
        if (found && smith_rec_touched(field[1], field[2])) return smith_cov_copy(field[2]);
        return found ? nullptr : smith_cov_copy(field[2]);
    }
    if (count >= 3 && strcmp(field[0], "outside") == 0) {
        smith_rec_mention(field[1]);
        uint64_t now = 0;
        if (!smith_rec_outside_now(field[1], &now)) return "a file it executed is gone";
        return now == strtoull(field[2], nullptr, 16) ? nullptr : "code outside functions changed";
    }
    if (count >= 3 && strcmp(field[0], "readdir") == 0) {
        smith_rec_mention(field[1]);
        bool same = smith_rec_dir_hash(field[1], &hash) && hash == strtoull(field[2], nullptr, 16);
        return same ? nullptr : smith_cov_copy(field[1]);
    }
    if (count >= 3 && (strcmp(field[0], "input") == 0 || strcmp(field[0], "read") == 0)) {
        smith_rec_mention(field[1]);
        if (smith_rec_ignored_input != nullptr && strcmp(field[1], smith_rec_ignored_input) == 0) return nullptr;
        bool same = smith_rec_file_hash(field[1], &hash) && hash == strtoull(field[2], nullptr, 16);
        smith_rec_source_path[0] = '\0';
        return same ? nullptr : smith_cov_copy(field[1]);
    }
    return nullptr;
}

static void smith_rec_record_line(char *line, SmithRecResult *result) {
    char *field[6] = {};
    size_t count = 0;
    for (char *at = line; count < 6 && at != nullptr;) {
        field[count++] = at;
        at = strchr(at, '\t');
        if (at != nullptr) *at++ = '\0';
    }
    if (count >= 2 && strcmp(field[0], "test") == 0) {
        result->name = smith_cov_copy(field[1]);
        return;
    }
    if (count >= 2 && strcmp(field[0], "cpu") == 0) {
        result->cpu = strtod(field[1], nullptr);
        return;
    }
    if (count >= 2 && strcmp(field[0], "opened") == 0) {
        result->opened = true;
        return;
    }
    if (count >= 2 && field[0][0] != 'r') smith_rec_name(result, field[1]);
    if (result->stale) {
        smith_rec_mention(count >= 2 ? field[1] : "");
        return;
    }
    const char *why = smith_rec_stale_line(field, count);
    if (why != nullptr) {
        result->stale = true;
        result->why = why;
    }
}

// Reads one record and says whether its test must run; every path it names is remembered as mentioned.
static bool smith_rec_check_file(const char *path) {
    if (smith_rec_result_count >= smith_rec_records_max) {
        static bool said;
        if (!said)
            fprintf(stderr, "smith test --changed: more than %zu records, the tests past them run\n",
                    smith_rec_records_max);
        said = true;
        return false;
    }
    FILE *file = fopen(path, "r");
    if (file == nullptr) return false;
    static char text[1U << 22];
    size_t size = fread(text, 1, sizeof text - 1, file);
    bool cut = size == sizeof text - 1 && fgetc(file) != EOF;
    fclose(file);
    text[size] = '\0';
    if (cut) { // the rest of the record would read as a test with nothing it depends on: stale is the safe answer
        fprintf(stderr, "smith test --changed: the record %s is over 4 MiB, its test runs\n", path);
        return false;
    }
    if (strncmp(text, "smith-record 2\n", 15) != 0) return false;
    SmithRecResult *result = &smith_rec_results[smith_rec_result_count];
    *result = (SmithRecResult){.named_first = smith_rec_named_used};
    char *line = text + 15;
    for (size_t guard = 0; *line != '\0' && guard < (size_t)1 << 20; guard++) { // bounded by the lines
        char *end = strchr(line, '\n');
        if (end != nullptr) *end = '\0';
        smith_rec_record_line(line, result);
        if (end == nullptr) break;
        line = end + 1;
    }
    if (result->name == nullptr) return false;
    smith_rec_result_count++;
    return true;
}

// Reads every record of a folder (smith_cov_root is the project). The count of records read.
static size_t smith_rec_check_folder(const char *folder) {
    DIR *dir = opendir(folder);
    if (dir == nullptr) return 0;
    struct dirent *entry = nullptr;
    for (size_t guard = 0; (entry = readdir(dir)) != nullptr && guard < (size_t)1 << 20; guard++) { // the folder
        char path[PATH_MAX];
        if (strstr(entry->d_name, ".rec") == nullptr) continue;
        snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
        (void)smith_rec_check_file(path);
    }
    closedir(dir);
    return smith_rec_result_count;
}

#ifdef SMITH_CORE
#include "smith_core_coverage.h"

// Forgets what an earlier call read or wrote, so the next starts from nothing (each `smith test` is a process).
static void smith_rec_reset(void) {
    smith_rec_source_path[0] = '\0';
    smith_rec_source_ok = false;
    smith_rec_line_count = 0;
    smith_rec_seen_count = 0;
    smith_rec_queue_count = 0;
    smith_rec_touched_count = 0;
    smith_rec_ignored_input = nullptr;
    smith_rec_named_used = 0;
    smith_rec_result_count = 0;
    smith_rec_outside_count = 0;
    memset(smith_rec_mentions, 0, sizeof smith_rec_mentions);
    smith_cov_pool_used = 0; // the names above were copied there
    smith_cov_file_count = 0;
    smith_cov_function_count = 0;
    smith_cov_overflow = false;
}

// include/smith_core_coverage.h: the record of a test that ran `ran`, written as `smith test --coverage merge` does.
bool smith_core_coverage_record(const char *folder, const char *test, bool pass, const SmithCoreFunction *ran,
                                size_t count) {
    smith_rec_reset();
    smith_cov_cpu_seconds = 0;
    snprintf(smith_cov_root, sizeof smith_cov_root, "%s", folder);
    bool ok = count <= smith_cov_functions_max;
    for (size_t i = 0; ok && i < count; i++) { // bounded by the argument
        int file = smith_cov_file_index(ran[i].path);
        ok = file >= 0;
        if (!ok) break;
        smith_cov_function_run[smith_cov_function_count] = true;
        smith_cov_functions[smith_cov_function_count++] =
            (SmithCovFunction){(uint32_t)file, ran[i].first, ran[i].last, smith_cov_copy(ran[i].name)};
    }
    char clean[256];
    char dir[PATH_MAX];
    smith_cov_clean_name(test, clean, sizeof clean);
    snprintf(dir, sizeof dir, "%s/.smith/cov", folder);
    ok = ok && !smith_cov_overflow && smith_rec_write(dir, test, clean, pass);
    smith_rec_reset();
    return ok;
}
#endif
