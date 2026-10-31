// `smith test --changed` (issue #441): the tests a change can affect, found from what the compiler knows and never
// declared by a test. knowledge/changed-tests.md. Included by smith.c, before runtime/verbs.c.
//
// The reverse dependency graph, and nothing else: every source of the project (`.smith`, `.c`, `.h`, `.cpp`, `.hpp`,
// `.m`) is scanned for its `module`, `import` and `#include` lines, an edge goes to each project file they name, and
// the files that import a changed file, transitively, are changed too. A test file is selected when it is one of
// them, or when it runs smith (`scratch::`, `SMITH_SELF`) and a source of smith changed (an edge from the test to
// everything compiled into smith). // A path the graph does not know (nothing imports it, no test is it, and it is not
// part of smith) selects the whole suite: unknown means run it. A few files select the whole suite too (the full-check
// list below).
#include <sys/mman.h>

constexpr size_t smith_changed_files_max = 16384;
constexpr size_t smith_changed_edges_max = 1U << 17;
constexpr size_t smith_changed_pool_max = 1U << 21;
constexpr size_t smith_changed_text_max = 1U << 21;
constexpr size_t smith_changed_argument_max =
    12000; // the runner's arguments travel as -DSMITH_ARGS (16384 bytes in all)
constexpr size_t smith_changed_paths_max = 4096;

static char smith_changed_pool[smith_changed_pool_max];
static size_t smith_changed_pool_used;
static const char *smith_changed_files[smith_changed_files_max];
static size_t smith_changed_file_count;
static uint32_t smith_changed_from[smith_changed_edges_max];
static uint32_t smith_changed_to[smith_changed_edges_max];
static size_t smith_changed_edge_count;
static bool smith_changed_hit[smith_changed_files_max];          // the file is changed or depends on a changed one
static bool smith_changed_chosen[smith_changed_files_max];       // a test file selected
static const char *smith_changed_why[smith_changed_files_max];   // and the first reason
static const char *smith_changed_paths[smith_changed_paths_max]; // the changed paths
static char smith_changed_kinds[smith_changed_paths_max];        // A added, M modified, D deleted
static size_t smith_changed_path_count;
static bool smith_changed_overflow;
static char smith_changed_text[smith_changed_text_max + 1];
static FILE *smith_changed_log; // where the selection says what it did: nullptr is stderr, the core's is a buffer

static FILE *smith_changed_out(void) {
    return smith_changed_log != nullptr ? smith_changed_log : stderr;
}

// The files that select the whole suite: the patches, the shims that no record sees and the runner's own build. The
// driver (smith.c) and the lowering (runtime/lang*) are not here: they run counted in the records of the train, so a
// change to them is judged by function like any other (#638). CI's files (.github/) select the tests that name them.
static const char *const smith_changed_full[] = {"patches/", "shim/", "tests/units/", nullptr};
// Under the whole-suite folders, the files that are counted too (build.c compiles them with the counters in a run that
// records): their functions are in the records, so they are judged by function.
static const char *const smith_changed_counted[] = {"shim/hook.cpp", nullptr};
// What is compiled into smith: a test that runs smith depends on all of it.
static const char *const smith_changed_smith[] = {"smith.c",      "runtime/", "shim/",      "patches/",
                                                  "include/",     "lib/",     "templates/", "scripts/",
                                                  "tree-sitter/", "plugin/",  "editors/",   nullptr};

// Text that grows as names are added, in memory mapped from the OS (smith's own C has no malloc): the filter of a
// selection has no size at which it silently becomes "every test".
typedef struct {
    char *data;
    size_t used;
    size_t capacity;
} SmithChangedBuffer;

static void smith_changed_buffer_free(SmithChangedBuffer *buffer) {
    if (buffer->data != nullptr) munmap(buffer->data, buffer->capacity);
    *buffer = (SmithChangedBuffer){};
}

static const char *smith_changed_buffer_text(const SmithChangedBuffer *buffer) {
    return buffer->data != nullptr ? buffer->data : "";
}

static const char *smith_changed_copy(const char *text, size_t length) {
    if (smith_changed_pool_used + length + 1 > smith_changed_pool_max) {
        smith_changed_overflow = true;
        return "";
    }
    char *at = smith_changed_pool + smith_changed_pool_used;
    memcpy(at, text, length);
    at[length] = '\0';
    smith_changed_pool_used += length + 1;
    return at;
}

static bool smith_changed_starts(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool smith_changed_ends(const char *text, const char *suffix) {
    size_t length = strlen(text);
    size_t tail = strlen(suffix);
    return length >= tail && strcmp(text + length - tail, suffix) == 0;
}

static bool smith_changed_in(const char *path, const char *const *prefixes) {
    for (size_t i = 0; prefixes[i] != nullptr; i++) { // a short fixed list
        if (smith_changed_starts(path, prefixes[i])) return true;
    }
    return false;
}

static bool smith_changed_is_test(const char *path) {
    return smith_changed_ends(path, "_test.smith") || smith_changed_ends(path, "_test.c");
}

static bool smith_changed_is_source(const char *path) {
    static const char *const extensions[] = {".smith", ".c", ".h", ".cpp", ".hpp", ".cc", ".m", nullptr};
    for (size_t i = 0; extensions[i] != nullptr; i++) { // a short fixed list
        if (smith_changed_ends(path, extensions[i])) return true;
    }
    return false;
}

// Whether a file of the project ends in `suffix` at a folder boundary.
static bool smith_changed_suffix(const char *path, const char *suffix) {
    size_t length = strlen(path);
    size_t tail = strlen(suffix);
    if (tail == 0 || length < tail || strcmp(path + length - tail, suffix) != 0) return false;
    return length == tail || path[length - tail - 1] == '/';
}

static bool smith_changed_word(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static size_t smith_changed_read(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return 0;
    size_t size = fread(smith_changed_text, 1, smith_changed_text_max, file);
    fclose(file);
    smith_changed_text[size] = '\0';
    return size;
}

static int smith_changed_find(const char *path) {
    for (size_t i = 0; i < smith_changed_file_count; i++) {
        if (strcmp(smith_changed_files[i], path) == 0) return (int)i;
    }
    return -1;
}

static void smith_changed_add_file(const char *path) {
    if (smith_changed_file_count >= smith_changed_files_max || smith_changed_find(path) >= 0) {
        if (smith_changed_find(path) < 0) smith_changed_overflow = true;
        return;
    }
    smith_changed_files[smith_changed_file_count++] = smith_changed_copy(path, strlen(path));
}

// Runs git with `argv` (after "git") and gives each line it prints (cut at its end) to `line`; false when git fails.
static bool smith_changed_git(const char *const *arguments, void (*line)(char *)) {
    const char *argv[16] = {"git"};
    size_t used = 1;
    for (size_t i = 0; arguments[i] != nullptr && used < 14; i++)
        argv[used++] = arguments[i];
    argv[used] = nullptr;
    int fds[2];
    if (pipe(fds) != 0) return false;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int started = posix_spawnp(&pid, "git", &actions, nullptr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    FILE *out = fdopen(fds[0], "r");
    if (started != 0 || out == nullptr) {
        close(fds[0]);
        return false;
    }
    char text[PATH_MAX + 8];
    while (fgets(text, sizeof text, out) != nullptr) { // bounded by git's output
        text[strcspn(text, "\n")] = '\0';
        if (text[0] != '\0') line(text);
    }
    fclose(out);
    int status = 0;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void smith_changed_on_file(char *text) {
    smith_changed_add_file(text);
}

static void smith_changed_add_path(char kind, const char *path) {
    if (smith_changed_path_count >= smith_changed_paths_max) {
        smith_changed_overflow = true;
        return;
    }
    smith_changed_kinds[smith_changed_path_count] = kind;
    smith_changed_paths[smith_changed_path_count++] = smith_changed_copy(path, strlen(path));
    if (kind == 'D') smith_changed_add_file(path); // a deleted file is still named by the files that import it
}

static void smith_changed_on_diff(char *text) {
    char *tab = strchr(text, '\t');
    if (tab == nullptr) return;
    *tab = '\0';
    char kind = 'M';
    if (text[0] == 'A') kind = 'A';
    if (text[0] == 'D') kind = 'D';
    smith_changed_add_path(kind, tab + 1);
}

static void smith_changed_on_untracked(char *text) {
    smith_changed_add_path('A', text);
}

// ---- The graph

// One edge from file `from` to every project file that `suffix` names.
static void smith_changed_edge(size_t from, const char *suffix) {
    for (size_t j = 0; j < smith_changed_file_count; j++) { // bounded by the files
        if (j == from || !smith_changed_suffix(smith_changed_files[j], suffix)) continue;
        if (smith_changed_edge_count >= smith_changed_edges_max) {
            smith_changed_overflow = true;
            return;
        }
        smith_changed_from[smith_changed_edge_count] = (uint32_t)from;
        smith_changed_to[smith_changed_edge_count++] = (uint32_t)j;
    }
}

// The name an `import` or `#include` line gives, as a path suffix: `a::b` is `a/b.smith`, `<smith/x.h>` is
// `smith/x.h`; false for a line that names nothing.
static bool smith_changed_target(const char *rest, char *out, size_t size) {
    while (*rest == ' ' || *rest == '\t')
        rest++;
    char close = '\0';
    if (*rest == '<') close = '>';
    if (*rest == '"') close = '"';
    size_t length = close != '\0' ? strcspn(rest + 1, close == '>' ? ">" : "\"") : strcspn(rest, " ;\t\r");
    const char *name = close != '\0' ? rest + 1 : rest;
    if (length == 0 || length + 8 >= size) return false;
    size_t used = 0;
    for (size_t i = 0; i < length; i++) {
        if (close == '\0' && name[i] == ':' && i + 1 < length && name[i + 1] == ':') {
            out[used++] = '/';
            i++;
        } else {
            out[used++] = name[i];
        }
    }
    if (close == '\0') used += (size_t)snprintf(out + used, size - used, ".smith");
    out[used] = '\0';
    return true;
}

// Reads the lines of the file in `smith_changed_text` that name another file.
static void smith_changed_scan_text(size_t from) {
    char target[512];
    char *line = smith_changed_text;
    for (size_t guard = 0; *line != '\0' && guard < smith_changed_text_max; guard++) { // bounded by the lines
        char *end = strchr(line, '\n');
        const char *start = line;
        while (*start == ' ' || *start == '\t')
            start++;
        const char *rest = nullptr;
        if (smith_changed_starts(start, "import ")) rest = start + 7;
        if (smith_changed_starts(start, "#include")) rest = start + 8;
        if (rest != nullptr && smith_changed_target(rest, target, sizeof target)) smith_changed_edge(from, target);
        if (end == nullptr) break;
        line = end + 1;
    }
}

static void smith_changed_build_graph(void) {
    for (size_t i = 0; i < smith_changed_file_count; i++) { // bounded by the files
        if (!smith_changed_is_source(smith_changed_files[i])) continue;
        if (smith_changed_read(smith_changed_files[i]) > 0) smith_changed_scan_text(i);
    }
    for (size_t p = 0; p < smith_changed_path_count; p++) {
        int at = smith_changed_find(smith_changed_paths[p]);
        if (at >= 0) smith_changed_hit[at] = true;
    }
    bool grew = true;
    for (size_t pass = 0; grew && pass < smith_changed_file_count + 1; pass++) { // a chain is as long as the files
        grew = false;
        for (size_t e = 0; e < smith_changed_edge_count; e++) {
            if (smith_changed_hit[smith_changed_to[e]] && !smith_changed_hit[smith_changed_from[e]]) {
                smith_changed_hit[smith_changed_from[e]] = true;
                grew = true;
            }
        }
    }
}

// ---- The rules

// A test runs smith when it starts it (`scratch::`, `SMITH_SELF`) or links its core as a library (include/smith_core.h,
// issue #646): the sources of smith are compiled into the runner that runs it.
static bool smith_changed_runs_smith(const char *text) {
    return strstr(text, "scratch::") != nullptr || strstr(text, "SMITH_SELF") != nullptr ||
           strstr(text, "smith_core.h") != nullptr;
}

static bool smith_changed_in_smith(const char *path) {
    return !smith_changed_is_test(path) && smith_changed_in(path, smith_changed_smith);
}

// The first changed file `test` (index) depends on through the graph, or -1.
static int smith_changed_dependency(size_t test) {
    if (smith_changed_find(smith_changed_files[test]) >= 0) {
        for (size_t p = 0; p < smith_changed_path_count; p++) {
            if (strcmp(smith_changed_paths[p], smith_changed_files[test]) == 0) return (int)test;
        }
    }
    for (size_t e = 0; e < smith_changed_edge_count; e++) {
        if (smith_changed_from[e] == test && smith_changed_hit[smith_changed_to[e]]) return (int)smith_changed_to[e];
    }
    return -1;
}

static void smith_changed_choose(size_t test, const char *format, const char *path) {
    if (smith_changed_chosen[test]) return;
    char reason[PATH_MAX + 64];
    snprintf(reason, sizeof reason, format, path);
    smith_changed_chosen[test] = true;
    smith_changed_why[test] = smith_changed_copy(reason, strlen(reason));
}

// ---- Records (runtime/record.c, issue #456)

// A test that ran under coverage keeps a record in `.smith/records/<test>.rec` of what it executed, its inputs and what
// it read, all by content hash. `--changed` reruns a test whose record is stale (a hash differs, it failed, or it has
// no record), and runs every test when a changed file is named by no record names and the graph does not place either
// (a fixture, a new file): unknown means run it. A changed file no record names but the graph places selects the tests
// whose recorded program is built from it. With no records the graph decides.
static const char *smith_changed_self;
static bool smith_changed_covered; // records are read and every changed source of smith is named by one
static const char *smith_changed_names[smith_changed_files_max]; // the tests of a file that records selected

// Set when a changed file is named by no record and no file imports it: every test runs.
static const char *smith_changed_unplaced;

// ---- Changes inside one function (issue #456)
//
// Two files select fewer tests than their path says, by what in them changed. A file another file `#embed`s (the
// build program's runtime, embedded into smith.c) reaches only the functions of the includer that use the embedded
// array: the tests whose records ran one of them. And build.c, which every record has as an input: its diff is read
// by function. A change outside any function, in a function a task's `.test` step runs, in one that registration
// calls, or in a task another task names, selects every test (the safe answer); a change only in functions that run
// when a task of no `smith test` runs (`.run = F` of a step without `.test`, and what only those call) selects the
// tests whose source names the function or the task, because no test can run the task unless it says so.

constexpr size_t smith_changed_functions_max = 512;
constexpr size_t smith_changed_hunks_max = 512;
constexpr size_t smith_changed_local_max = 32;

static size_t smith_changed_hunk_first[smith_changed_hunks_max];
static size_t smith_changed_hunk_lines[smith_changed_hunks_max];
static size_t smith_changed_hunk_count;
static bool smith_changed_hunk_overflow;

// The names (function, task) of a build.c diff confined to functions no test runs; the tests naming one are selected.
static char smith_changed_local_names[smith_changed_local_max][64];
static size_t smith_changed_local_count;
static bool smith_changed_build_local; // the diff of build.c was judged local

static void smith_changed_on_hunk(char *text) {
    if (strncmp(text, "@@ ", 3) != 0) return;
    const char *plus = strchr(text, '+');
    if (plus == nullptr) return;
    char *end = nullptr;
    size_t first = strtoul(plus + 1, &end, 10);
    size_t lines = *end == ',' ? strtoul(end + 1, nullptr, 10) : 1;
    if (smith_changed_hunk_count >= smith_changed_hunks_max) {
        smith_changed_hunk_overflow = true;
        return;
    }
    smith_changed_hunk_first[smith_changed_hunk_count] = first;
    smith_changed_hunk_lines[smith_changed_hunk_count++] = lines;
}

typedef struct {
    size_t open[smith_changed_functions_max];  // first line of each function
    size_t close[smith_changed_functions_max]; // last line
    char name[smith_changed_functions_max][64];
    char task[smith_changed_functions_max][64]; // the task whose step runs it, when one does
    size_t callers[smith_changed_functions_max][8];
    uint8_t caller_count[smith_changed_functions_max];
    uint8_t run_refs[smith_changed_functions_max];
    bool bad[smith_changed_functions_max]; // referenced in a way that is not a run-only use
    size_t count;
} SmithChangedFunctions;

static SmithChangedFunctions smith_changed_functions;
static size_t smith_changed_line_offset[smith_cov_lines_max + 2]; // where each line of the loaded file starts

// The functions of the loaded source: a function opens at brace depth 0 on a line ending in `) {` and closes before
// the next line at depth 0.
static void smith_changed_find_functions(SmithChangedFunctions *all) {
    all->count = 0;
    size_t offset = 0;
    for (size_t line = 1; line <= smith_rec_line_count; line++) { // bounded by the lines
        smith_changed_line_offset[line] = offset;
        const char *end = strchr(smith_rec_source + offset, '\n');
        offset = end != nullptr ? (size_t)(end - smith_rec_source) + 1 : strlen(smith_rec_source);
    }
    smith_changed_line_offset[smith_rec_line_count + 1] = offset;
    for (size_t line = 1; line <= smith_rec_line_count && all->count < smith_changed_functions_max; line++) {
        if (!smith_rec_line_opens[line]) continue;
        size_t last = line;
        while (last < smith_rec_line_count && smith_rec_line_inside[last + 1] &&
               !smith_rec_line_opens[last + 1]) // bounded by the lines
            last++;
        const char *text = smith_rec_source + smith_changed_line_offset[line];
        const char *paren = memchr(text, '(', smith_changed_line_offset[line + 1] - smith_changed_line_offset[line]);
        if (paren == nullptr) continue;
        const char *start = paren;
        while (start > text && smith_changed_word(start[-1])) // the identifier before the parenthesis
            start--;
        size_t at = all->count++;
        all->open[at] = line;
        all->close[at] = last;
        snprintf(all->name[at], sizeof all->name[at], "%.*s", (int)(paren - start), start);
        all->task[at][0] = '\0';
        all->caller_count[at] = 0;
        all->run_refs[at] = 0;
        all->bad[at] = false;
        line = last;
    }
}

static size_t smith_changed_line_of(size_t offset) {
    size_t low = 1;
    size_t high = smith_rec_line_count;
    for (size_t step = 0; low < high && step < 32; step++) { // a binary search over at most 2^15 lines
        size_t middle = (low + high + 1) / 2;
        if (smith_changed_line_offset[middle] <= offset)
            low = middle;
        else
            high = middle - 1;
    }
    return low;
}

static int smith_changed_function_at(const SmithChangedFunctions *all, size_t line) {
    for (size_t i = 0; i < all->count; i++) { // bounded by the functions
        if (line >= all->open[i] && line <= all->close[i]) return (int)i;
    }
    return -1;
}

// Whether the text before `offset` is `.run = ` (a task's step names its function).
static bool smith_changed_is_run_ref(size_t offset) {
    static const char tail[] = ".run = ";
    return offset >= sizeof tail - 1 &&
           memcmp(smith_rec_source + offset - (sizeof tail - 1), tail, sizeof tail - 1) == 0;
}

// For the step initializer around `offset` (`SmithStep x = {... .run = F ...};` then `smith_task(b, "name", &x)`):
// writes the task's name and returns true when the step is no part of `smith test`: no `.test`, no `.outputs`, and the
// task's name is written nowhere else in the file (nothing depends on it).
static bool smith_changed_task_of(size_t offset, char *name, size_t size) {
    size_t back = offset > 1500 ? offset - 1500 : 0;
    const char *begin = nullptr;
    for (const char *at = smith_rec_source + back;
         (at = strstr(at, "SmithStep ")) != nullptr && at < smith_rec_source + offset; at++)
        begin = at;
    if (begin == nullptr) return false;
    const char *end = strstr(smith_rec_source + offset, "};");
    if (end == nullptr) return false;
    char block[2048];
    size_t length = (size_t)(end - begin);
    if (length >= sizeof block) return false;
    memcpy(block, begin, length);
    block[length] = '\0';
    if (strstr(block, ".test") != nullptr || strstr(block, ".outputs") != nullptr) return false;
    char variable[64];
    const char *named = begin + strlen("SmithStep ");
    snprintf(variable, sizeof variable, "&%.*s)", (int)strcspn(named, " ="), named);
    const char *registered = strstr(end, variable);
    if (registered == nullptr || registered - end > 400) return false;
    const char *quote = registered;
    while (quote > end && *quote != '"') // the closing quote of the name
        quote--;
    const char *open = quote - 1;
    while (open > end && *open != '"')
        open--;
    if (quote <= open + 1 || (size_t)(quote - open) >= size) return false;
    snprintf(name, size, "%.*s", (int)(quote - open - 1), open + 1);
    char quoted[80];
    snprintf(quoted, sizeof quoted, "\"%s\"", name);
    size_t seen = 0;
    for (const char *at = smith_rec_source; (at = strstr(at, quoted)) != nullptr && seen < 3; at++)
        seen++;
    return seen == 1;
}

// How each function is mentioned: as a task's step (`.run = F`, of a step that is no part of `smith test`), in the body
// of another function (its callers), or any other way (a global, a table, a test step: bad).
static void smith_changed_mentions(SmithChangedFunctions *all) {
    for (size_t f = 0; f < all->count; f++) { // bounded by the functions
        size_t length = strlen(all->name[f]);
        for (const char *at = smith_rec_source; (at = strstr(at, all->name[f])) != nullptr; at += length) {
            bool word = !smith_changed_word(at[length]) && (at == smith_rec_source || !smith_changed_word(at[-1]));
            if (!word) continue;
            size_t offset = (size_t)(at - smith_rec_source);
            size_t line = smith_changed_line_of(offset);
            if (line == all->open[f]) continue; // its own definition
            int caller = smith_changed_function_at(all, line);
            char task[64];
            if (smith_changed_is_run_ref(offset) && smith_changed_task_of(offset, task, sizeof task)) {
                all->run_refs[f]++;
                snprintf(all->task[f], sizeof all->task[f], "%s", task);
            } else if (caller >= 0 && caller != (int)f && all->caller_count[f] < 8) {
                all->callers[f][all->caller_count[f]++] = (size_t)caller;
            } else if (caller != (int)f) {
                all->bad[f] = true;
            }
        }
        if (all->run_refs[f] == 0 && all->caller_count[f] == 0) all->bad[f] = true; // registration or an entry
    }
}

// The functions that run only when a task of no `smith test` runs: not bad, and every caller is one too.
static void smith_changed_run_only(SmithChangedFunctions *all) {
    for (size_t pass = 0; pass <= all->count; pass++) { // a chain of callers is as long as the functions
        bool changed = false;
        for (size_t f = 0; f < all->count; f++) {
            for (size_t c = 0; !all->bad[f] && c < all->caller_count[f]; c++) {
                if (all->bad[all->callers[f][c]]) {
                    all->bad[f] = true;
                    changed = true;
                }
            }
        }
        if (!changed) break;
    }
}

static void smith_changed_local_name(const char *name) {
    if (name[0] == '\0' || smith_changed_local_count >= smith_changed_local_max) return;
    for (size_t i = 0; i < smith_changed_local_count; i++) { // bounded by the names
        if (strcmp(smith_changed_local_names[i], name) == 0) return;
    }
    snprintf(smith_changed_local_names[smith_changed_local_count++], sizeof smith_changed_local_names[0], "%s", name);
}

// Whether the diff of build.c (`base`..`head`, or the working tree) is confined to functions no test can run.
static bool smith_changed_build_c_local(const char *base, const char *head) {
    if (getcwd(smith_cov_root, sizeof smith_cov_root) == nullptr) return false;
    smith_changed_hunk_count = 0;
    smith_changed_hunk_overflow = false;
    const char *const diff[] = {"diff", "-U0", "--no-color", "--no-ext-diff", base, "--", "build.c", nullptr};
    const char *const range[] = {"diff", "-U0", "--no-color", "--no-ext-diff", base, head, "--", "build.c", nullptr};
    if (!smith_changed_git(head != nullptr ? range : diff, smith_changed_on_hunk) || smith_changed_hunk_overflow ||
        smith_changed_hunk_count == 0)
        return false;
    smith_rec_source_path[0] = '\0';
    if (!smith_rec_load_source("build.c")) return false;
    SmithChangedFunctions *all = &smith_changed_functions;
    smith_changed_find_functions(all);
    smith_changed_mentions(all);
    smith_changed_run_only(all);
    smith_changed_local_count = 0;
    for (size_t h = 0; h < smith_changed_hunk_count; h++) {
        size_t first = smith_changed_hunk_first[h];
        size_t last = smith_changed_hunk_lines[h] == 0 ? first + 1 : first + smith_changed_hunk_lines[h] - 1;
        for (size_t line = first; line <= last; line++) { // bounded by the hunk
            int f = smith_changed_function_at(all, line);
            if (f < 0 || all->bad[f]) return false;
            smith_changed_local_name(all->name[f]);
            smith_changed_local_name(all->task[f]);
        }
    }
    return true;
}

// The functions of `includer` that use the array an `#embed "path"` fills, as touched ones for the records. True when
// the file is embedded somewhere and every use is in a function (else the caller keeps it unplaced).
static bool smith_changed_embed_touch(const char *includer, const char *path) {
    smith_rec_source_path[0] = '\0';
    if (!smith_rec_load_source(includer)) return false;
    char want[PATH_MAX + 16];
    snprintf(want, sizeof want, "#embed \"%s\"", path);
    const char *embed = strstr(smith_rec_source, want);
    if (embed == nullptr) return false;
    const char *open = embed;
    while (open > smith_rec_source && open[-1] != '[') // the `[]` of `unsigned char name[] = {`
        open--;
    if (open == smith_rec_source) return false;
    const char *start = open - 1;
    while (start > smith_rec_source && smith_changed_word(start[-1]))
        start--;
    char symbol[64];
    snprintf(symbol, sizeof symbol, "%.*s", (int)(open - 1 - start), start);
    SmithChangedFunctions *all = &smith_changed_functions;
    smith_changed_find_functions(all);
    size_t found = 0;
    size_t length = strlen(symbol);
    for (const char *at = smith_rec_source; length > 0 && (at = strstr(at, symbol)) != nullptr; at += length) {
        if (smith_changed_word(at[length]) || (at > smith_rec_source && smith_changed_word(at[-1]))) continue;
        size_t line = smith_changed_line_of((size_t)(at - smith_rec_source));
        int f = smith_changed_function_at(all, line);
        if (at >= start && at < embed) continue; // the declaration itself
        if (f < 0) return false;                 // used outside a function: not understood
        if (smith_rec_touched_count >= smith_rec_touched_max) return false;
        smith_rec_touched_file[smith_rec_touched_count] = smith_cov_copy(includer);
        smith_rec_touched_name[smith_rec_touched_count++] = smith_cov_copy(all->name[f]);
        found++;
    }
    return found > 0;
}

constexpr size_t smith_changed_embedded_max = 16;
static const char *smith_changed_embedded[smith_changed_embedded_max]; // changed files placed through their includer
static size_t smith_changed_embedded_count;

// For each changed source of smith some file `#embed`s: the includer's functions that use it are touched, and the file
// is placed (the tests whose records ran one of those functions are the ones it reaches).
static void smith_changed_embeds(void) {
    for (size_t p = 0; p < smith_changed_path_count; p++) {
        const char *path = smith_changed_paths[p];
        if (!smith_changed_is_source(path) || smith_changed_is_test(path) || !smith_changed_in_smith(path)) continue;
        for (size_t i = 0; i < smith_changed_file_count && smith_changed_embedded_count < smith_changed_embedded_max;
             i++) {
            if (!smith_changed_ends(smith_changed_files[i], ".c") || strcmp(smith_changed_files[i], path) == 0)
                continue;
            if (smith_changed_embed_touch(smith_changed_files[i], path))
                smith_changed_embedded[smith_changed_embedded_count++] = path;
        }
    }
}

static bool smith_changed_records_folder(char *out, size_t size) {
    const char *given = getenv("SMITH_RECORDS_DIR");
    if (given != nullptr && given[0] != '\0')
        snprintf(out, size, "%s", given);
    else
        snprintf(out, size, "%s/.smith/records", smith_cov_root);
    return access(out, R_OK) == 0;
}

// Runs the project's task `coverage-fetch` when its build.c defines one: it brings shared records into the folder (from
// CI, say). smith itself never talks to the network.
static void smith_changed_fetch(void) {
    if (smith_changed_self == nullptr || getenv("SMITH_COVERAGE_NO_FETCH") != nullptr) return;
    if (smith_changed_read("build.c") == 0 || strstr(smith_changed_text, "\"coverage-fetch\"") == nullptr) return;
    const char *const fetch[] = {smith_changed_self, "build", "coverage-fetch", nullptr};
    (void)smith_cov_spawn(fetch, nullptr, true);
}

static bool smith_changed_unnamed_seed[smith_changed_files_max];  // changed files no record names
static bool smith_changed_unnamed_reach[smith_changed_files_max]; // and the files built from them

static bool smith_changed_read_by_a_test(const char *path);

// A file no test can be affected by unless its source names it: CI's own configuration (the workflow is exercised by
// CI running) and the profiles of a run under coverage (generated, never committed). Placed, and selecting nothing.
static bool smith_changed_inert(const char *path) {
    if (smith_changed_build_local && strcmp(path, "build.c") == 0) return true; // judged by function
    for (size_t i = 0; i < smith_changed_embedded_count; i++) { // placed through the file that embeds it
        if (strcmp(smith_changed_embedded[i], path) == 0) return true;
    }
    return smith_changed_starts(path, ".github/") || smith_changed_ends(path, ".profraw") ||
           smith_changed_ends(path, ".profdata");
}

// Marks the changed files no record names, and everything that imports or includes them, transitively (the graph:
// whatever is built from them). Returns the first such file that no file imports or includes either (the graph does
// not place it: unknown means run every test), or nullptr.
static const char *smith_changed_unnamed(void) {
    const char *unknown = nullptr;
    for (size_t p = 0; p < smith_changed_path_count; p++) {
        const char *name = smith_changed_paths[p];
        if (smith_changed_is_test(name) || smith_rec_mentioned(name) || smith_changed_ends(name, ".md") ||
            smith_changed_inert(name) || smith_changed_read_by_a_test(name))
            continue;
        int at = smith_changed_find(name);
        bool imported = false;
        for (size_t e = 0; at >= 0 && e < smith_changed_edge_count && !imported; e++)
            imported = smith_changed_to[e] == (uint32_t)at;
        if (at >= 0) smith_changed_unnamed_seed[at] = true;
        if (!imported && unknown == nullptr) unknown = name;
    }
    memcpy(smith_changed_unnamed_reach, smith_changed_unnamed_seed, sizeof smith_changed_unnamed_reach);
    bool grew = true;
    for (size_t pass = 0; grew && pass < smith_changed_file_count + 1; pass++) { // a chain is as long as the files
        grew = false;
        for (size_t e = 0; e < smith_changed_edge_count; e++) {
            if (smith_changed_unnamed_reach[smith_changed_to[e]] &&
                !smith_changed_unnamed_reach[smith_changed_from[e]]) {
                smith_changed_unnamed_reach[smith_changed_from[e]] = true;
                grew = true;
            }
        }
    }
    return unknown;
}

// The first file of the record's program that is built from a changed file no record names, or nullptr.
static const char *smith_changed_built_from(const SmithRecResult *record) {
    for (size_t i = 0; i < record->named_count; i++) { // bounded by the files of the record
        int at = smith_changed_find(smith_rec_named[record->named_first + i]);
        if (at >= 0 && smith_changed_unnamed_reach[at]) return smith_rec_named[record->named_first + i];
    }
    return nullptr;
}

// Whether `smith test --changed` makes the records itself in this project (issue #456): it builds the tests with
// smith_tests, or by convention (no build.c). A project whose build.c makes its tests some other way (smith's own: the
// train records) is judged by the records it was given and by the graph for the rest.
static bool smith_changed_makes_records;

static void smith_changed_find_recording(void) {
    smith_changed_makes_records = smith_changed_read("build.c") == 0 ||
                                  strstr(smith_changed_text, "smith_tests(") != nullptr ||
                                  strstr(smith_changed_text, "smith_app_tests(") != nullptr;
}

// Reads the records and decides whether they judge this change. Prints why not.
static void smith_changed_records(void) {
    char folder[PATH_MAX];
    smith_changed_find_recording();
    if (getcwd(smith_cov_root, sizeof smith_cov_root) == nullptr) return;
    smith_changed_embeds();
    if (!smith_changed_records_folder(folder, sizeof folder)) {
        smith_changed_fetch();
        if (!smith_changed_records_folder(folder, sizeof folder)) {
            fputs("smith test --changed: no records of tests, the graph decides\n", smith_changed_out());
            return;
        }
    }
    if (smith_rec_check_folder(folder) == 0) {
        fputs("smith test --changed: no records of tests, the graph decides\n", smith_changed_out());
        return;
    }
    smith_changed_unplaced = smith_changed_unnamed();
    smith_changed_covered = smith_changed_unplaced == nullptr;
}

// The `module` a test file declares, as a prefix of its tests' names (`name::`); false for a file with none.
static bool smith_changed_module_of(const char *text, char *out, size_t size) {
    const char *module = strstr(text, "\nmodule ");
    if (module != nullptr)
        module += 8;
    else if (strncmp(text, "module ", 7) == 0)
        module = text + 7;
    if (module == nullptr) return false;
    snprintf(out, size, "%.*s::", (int)strcspn(module, " ;\n"), module);
    return true;
}

static const SmithRecResult *smith_changed_record_of(const char *name) {
    for (size_t r = 0; r < smith_rec_result_count; r++) { // bounded by the records
        if (strcmp(smith_rec_results[r].name, name) == 0) return &smith_rec_results[r];
    }
    return nullptr;
}

static void smith_changed_filter_add(SmithChangedBuffer *filter, const char *name);

// The rule by records, for a test file (any, not only a test that runs smith: a user's end-to-end test is as unseen by
// the graph): its tests with a stale record, with a program built from a changed file no record names, and the ones
// with none (a new test). True when the records decided, even for no test; false when none of its tests has a record.
static bool smith_changed_coverage_judge(size_t test) {
    SmithChangedBuffer names = {};
    char module[256] = "";
    if (!smith_changed_covered) return false;
    (void)smith_changed_module_of(smith_changed_text, module, sizeof module); // a file with no module: bare names
    char reason[400] = "";
    bool known = false;
    size_t counted = 0; // the tests of this file the records select
    char *at = smith_changed_text;
    for (size_t guard = 0; (at = strstr(at, "void test_")) != nullptr && guard < smith_changed_text_max; guard++) {
        at += 10; // the runner's name for a test has no test_ prefix
        char full[300];
        snprintf(full, sizeof full, "%s%.*s", module, (int)strcspn(at, "("), at);
        const SmithRecResult *record = smith_changed_record_of(full);
        if (record == nullptr) {
            smith_changed_filter_add(&names, full);
            counted++;
            if (reason[0] == '\0') snprintf(reason, sizeof reason, "no record of %.300s", full);
            continue;
        }
        known = true;
        const char *built = smith_changed_built_from(record);
        if (!record->stale && built == nullptr) continue;
        smith_changed_filter_add(&names, record->name);
        counted++;
        if (reason[0] != '\0') continue;
        if (record->stale)
            snprintf(reason, sizeof reason, "its record is stale: %.150s, %.150s", record->name, record->why);
        else
            snprintf(reason, sizeof reason,
                     "%.150s: its program is built from a changed file no record names (through %.150s)", record->name,
                     built);
    }
    if ((!known && !smith_changed_makes_records) || reason[0] == '\0') { // no record of any, and none is made here
        smith_changed_buffer_free(&names);
        return reason[0] == '\0' && known;
    }
    char counted_reason[480];
    snprintf(counted_reason, sizeof counted_reason, "%s (%zu test%s)", reason, counted, counted == 1 ? "" : "s");
    smith_changed_choose(test, "%s", counted_reason);
    smith_changed_names[test] = smith_changed_buffer_text(&names); // kept: the selection reads it
    return true;
}

// Whether the source of a test names a file that is data to it (a workflow, a fixture, a corpus): its name (when it has
// an extension, so `stdout` is not one) or a folder it is in, two names deep at least (`tests/smith-fmt`, `.github`).
static bool smith_changed_names_path(const char *text, const char *path) {
    const char *base = strrchr(path, '/');
    base = base != nullptr ? base + 1 : path;
    if (strchr(base, '.') != nullptr && strlen(base) >= 5 && strstr(text, base) != nullptr) return true;
    for (const char *slash = strchr(path, '/'); slash != nullptr; slash = strchr(slash + 1, '/')) {
        char folder[PATH_MAX];
        snprintf(folder, sizeof folder, "%.*s", (int)(slash - path), path);
        bool generic = strcmp(folder, "tests") == 0 || strcmp(folder, "lib") == 0 || strcmp(folder, "src") == 0 ||
                       strcmp(folder, "runtime") == 0 || strcmp(folder, "docs") == 0;
        if (!generic && strstr(text, folder) != nullptr) return true;
    }
    return false;
}

// A changed path that is data to the tests: not a test, not part of smith, not Markdown, and no file imports it.
static bool smith_changed_is_data(const char *path) {
    if (smith_changed_is_test(path) || smith_changed_in_smith(path) || smith_changed_ends(path, ".md")) return false;
    int at = smith_changed_find(path);
    for (size_t e = 0; at >= 0 && e < smith_changed_edge_count; e++) {
        if (smith_changed_to[e] == (uint32_t)at) return false;
    }
    return true;
}

// Whether some test names the changed data path: then the tests that name it are its readers, and it is placed.
static bool smith_changed_read_by_a_test(const char *path) {
    if (!smith_changed_is_data(path)) return false;
    for (size_t i = 0; i < smith_changed_file_count; i++) {
        if (!smith_changed_is_test(smith_changed_files[i]) || smith_changed_read(smith_changed_files[i]) == 0) continue;
        if (smith_changed_names_path(smith_changed_text, path)) return true;
    }
    return false;
}

// Whether the records say what every test of the file opened (#739): then a changed data file is an input of the tests
// that opened it, which are stale by their records, and of no other, whatever the source names.
static bool smith_changed_opens_recorded(void) {
    char module[256] = "";
    size_t tests = 0;
    if (!smith_changed_covered) return false;
    (void)smith_changed_module_of(smith_changed_text, module, sizeof module);
    char *at = smith_changed_text;
    for (size_t guard = 0; (at = strstr(at, "void test_")) != nullptr && guard < smith_changed_text_max; guard++) {
        at += 10;
        char full[300];
        snprintf(full, sizeof full, "%s%.*s", module, (int)strcspn(at, "("), at);
        const SmithRecResult *record = smith_changed_record_of(full);
        if (record == nullptr || !record->opened) return false;
        tests++;
    }
    return tests > 0;
}

// One test file: selected when the graph reaches it from a changed file, or when it runs smith and a source of smith
// changed (an edge from the test to everything compiled into smith).
static void smith_changed_judge(size_t test) {
    int dependency = smith_changed_dependency(test);
    if (dependency >= 0)
        smith_changed_choose(test, dependency == (int)test ? "it changed" : "it depends on %s",
                             smith_changed_files[dependency]);
    if (smith_changed_chosen[test] || smith_changed_read(smith_changed_files[test]) == 0) return;
    if (strstr(smith_changed_text, ".md") != nullptr) { // a test that names Markdown files reads them (skills, docs)
        for (size_t p = 0; p < smith_changed_path_count; p++) {
            if (!smith_changed_ends(smith_changed_paths[p], ".md")) continue;
            smith_changed_choose(test, "it reads Markdown, and %s changed", smith_changed_paths[p]);
            return;
        }
    }
    for (size_t n = 0; smith_changed_build_local && n < smith_changed_local_count; n++) { // a task it names
        if (strstr(smith_changed_text, smith_changed_local_names[n]) == nullptr) continue;
        smith_changed_choose(test, "it names %s, which build.c changed", smith_changed_local_names[n]);
        return;
    }
    bool opens = smith_changed_opens_recorded();
    for (size_t p = 0; !opens && p < smith_changed_path_count; p++) { // a test that names a data file reads it
        if (!smith_changed_is_data(smith_changed_paths[p]) ||
            !smith_changed_names_path(smith_changed_text, smith_changed_paths[p]))
            continue;

        smith_changed_choose(test, "it names %s, and it changed", smith_changed_paths[p]);
        return;
    }
    if (smith_changed_coverage_judge(test) || !smith_changed_runs_smith(smith_changed_text)) return;
    for (size_t p = 0; p < smith_changed_path_count; p++) {
        if (smith_changed_in_smith(smith_changed_paths[p])) {
            smith_changed_choose(test, "it runs smith, and %s is part of smith", smith_changed_paths[p]);
            return;
        }
    }
}

// The first changed path the graph does not know, or -1: not a test, not part of smith, and no file imports or includes
// it.
static int smith_changed_unknown(void) {
    for (size_t p = 0; p < smith_changed_path_count; p++) {
        const char *path = smith_changed_paths[p];
        if (smith_changed_is_test(path) || smith_changed_in_smith(path) || smith_changed_ends(path, ".md")) continue;
        if (smith_changed_covered && smith_rec_mentioned(path)) continue; // a record names it: its tests judge it
        if (smith_changed_inert(path) || smith_changed_read_by_a_test(path)) continue; // placed: its readers judge it
        int at = smith_changed_find(path);
        bool imported = false;
        for (size_t e = 0; at >= 0 && e < smith_changed_edge_count && !imported; e++)
            imported = smith_changed_to[e] == (uint32_t)at;
        if (!imported) return (int)p;
    }
    return -1;
}

// ---- The filter

static void smith_changed_filter_add(SmithChangedBuffer *filter, const char *name) {
    size_t length = strlen(name);
    size_t needed = filter->used + length + 2;
    if (needed > filter->capacity) {
        size_t capacity = filter->capacity > 0 ? filter->capacity : 1U << 14;
        for (size_t doubled = 0; capacity < needed && doubled < 64; doubled++) // a size_t has 64 doublings
            capacity *= 2;
        void *memory = mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (memory == MAP_FAILED) { // the OS refuses: the whole suite is the safe answer
            smith_changed_overflow = true;
            return;
        }
        if (filter->used > 0) memcpy(memory, filter->data, filter->used);
        if (filter->data != nullptr) munmap(filter->data, filter->capacity);
        filter->data = memory;
        filter->capacity = capacity;
    }
    if (filter->used > 0) filter->data[filter->used++] = ',';
    memcpy(filter->data + filter->used, name, length);
    filter->used += length;
    filter->data[filter->used] = '\0';
}

// The exact names coverage chose (comma separated) onto the filter.
static void smith_changed_filter_names(const char *names, SmithChangedBuffer *filter) {
    char name[320];
    for (const char *at = names; *at != '\0';) { // bounded by the names
        size_t length = strcspn(at, ",");
        snprintf(name, sizeof name, "%.*s", (int)length, at);
        smith_changed_filter_add(filter, name);
        at += length + (at[length] == ',' ? 1 : 0);
    }
}

// What `--test` needs to run the tests of a file: `module::` for a module, else the names of its `test_` functions.
static void smith_changed_filter_file(const char *path, SmithChangedBuffer *filter) {
    if (smith_changed_read(path) == 0) return;
    char *module = strstr(smith_changed_text, "\nmodule ");
    if (module != nullptr || smith_changed_starts(smith_changed_text, "module ")) {
        module = module != nullptr ? module + 8 : smith_changed_text + 7;
        char name[256];
        snprintf(name, sizeof name, "%.*s::", (int)strcspn(module, " ;\n"), module);
        smith_changed_filter_add(filter, name);
        return;
    }
    char *at = smith_changed_text;
    for (size_t guard = 0; (at = strstr(at, "void test_")) != nullptr && guard < smith_changed_text_max; guard++) {
        at += 10;
        char name[256];
        snprintf(name, sizeof name, "%.*s", (int)strcspn(at, "("), at);
        smith_changed_filter_add(filter, name);
    }
}

// ---- The selection

// --cheapest-first: the CPU seconds of each test the records know, as the runner's --costs file (`cpu<TAB>seconds<TAB>
// name`) in .smith/costs.tsv. False when there is nothing to write (no records: the runner keeps its order).
static bool smith_changed_write_costs(char *path, size_t size) {
    char root[PATH_MAX];
    char folder[PATH_MAX];
    if (getcwd(root, sizeof root) == nullptr) return false;
    if (smith_rec_result_count == 0) { // a selection that never read the records (the whole suite)
        snprintf(smith_cov_root, sizeof smith_cov_root, "%s", root);
        if (!smith_changed_records_folder(folder, sizeof folder)) {
            smith_changed_fetch();
            if (!smith_changed_records_folder(folder, sizeof folder)) return false;
        }
        if (smith_rec_check_folder(folder) == 0) return false;
    }
    snprintf(path, size, "%s/.smith/costs.tsv", root);
    FILE *out = fopen(path, "w");
    if (out == nullptr) return false;
    for (size_t r = 0; r < smith_rec_result_count; r++) {
        if (smith_rec_results[r].cpu > 0)
            fprintf(out, "cpu\t%.3f\t%s\n", smith_rec_results[r].cpu, smith_rec_results[r].name);
    }
    return fclose(out) == 0;
}

typedef enum { SmithChanged_Failed, SmithChanged_Full, SmithChanged_None, SmithChanged_Some } SmithChangedResult;

static bool smith_changed_name_ok(const char *base) {
    for (const char *c = base; *c != '\0'; c++) {
        if (!smith_changed_word(*c) && *c != '/' && *c != '.' && *c != '~' && *c != '^' && *c != '-' && *c != '@')
            return false;
    }
    return base[0] != '\0' && base[0] != '-';
}

static void smith_changed_on_base(char *text) {
    snprintf(smith_changed_text, 256, "%s", text);
}

// The base: the given ref, else where the branch left origin/main (or main).
static bool smith_changed_base(const char *given, char *base, size_t size) {
    if (given != nullptr) {
        if (!smith_changed_name_ok(given)) return false;
        snprintf(base, size, "%s", given);
        return true;
    }
    static const char *const origin[] = {"merge-base", "HEAD", "origin/main", nullptr};
    static const char *const local[] = {"merge-base", "HEAD", "main", nullptr};
    const char *const *commands[] = {origin, local};
    for (size_t i = 0; i < 2; i++) {
        smith_changed_text[0] = '\0';
        if (smith_changed_git(commands[i], smith_changed_on_base) && smith_changed_text[0] != '\0') {
            snprintf(base, size, "%.255s", smith_changed_text);
            return true;
        }
    }
    return false;
}

static size_t smith_changed_full_path(void) {
    for (size_t p = 0; p < smith_changed_path_count; p++) {
        if (smith_changed_in(smith_changed_paths[p], smith_changed_full) &&
            !smith_changed_in(smith_changed_paths[p], smith_changed_counted))
            return p + 1;
    }
    return 0;
}

static void smith_changed_report(size_t selected, size_t tests, const char *base) {
    fprintf(smith_changed_out(),
            "smith test --changed: %zu changed file%s since %.12s: %zu of %zu test files selected\n",
            smith_changed_path_count, smith_changed_path_count == 1 ? "" : "s", base, selected, tests);
    for (size_t i = 0; i < smith_changed_file_count; i++) {
        if (smith_changed_chosen[i])
            fprintf(smith_changed_out(), "smith test --changed:   %s: %s\n", smith_changed_files[i],
                    smith_changed_why[i]);
    }
}

// build.c by the functions its diff touches: false when it changed where any test can run (the whole suite).
static bool smith_changed_build_c_gate(const char *base, const char *head) {
    for (size_t p = 0; p < smith_changed_path_count; p++) { // bounded by the changed paths
        if (strcmp(smith_changed_paths[p], "build.c") != 0) continue;
        if (!smith_changed_build_c_local(base, head)) {
            fputs("smith test --changed: build.c changed outside the functions of tasks no test runs, so every test "
                  "runs\n",
                  smith_changed_out());
            return false;
        }
        smith_changed_build_local = true;
        smith_rec_ignored_input = "build.c";
        fprintf(smith_changed_out(),
                "smith test --changed: build.c changed only in %zu name%s no test runs (%s%s): the tests that name "
                "one run\n",
                smith_changed_local_count, smith_changed_local_count == 1 ? "" : "s", smith_changed_local_names[0],
                smith_changed_local_count > 1 ? ", ..." : "");
    }
    return true;
}

// Reads the changes since `base_name` (or where the branch left main) in the current folder's git repository into the
// file list and the changed paths: the files git sees (tracked and untracked), and the paths that differ from `base`
// (the working tree, or `head` when given: then no untracked file counts). `who` names the verb in what it prints;
// `relative` makes the paths those below the current folder (a project that is a folder of a repository). `base`
// gets the ref that was compared. False when there is nothing to compare with.
static bool smith_changed_collect(const char *base_name, const char *head, bool relative, const char *who, char *base,
                                  size_t size) {
    if (!smith_changed_base(base_name, base, size) || (head != nullptr && !smith_changed_name_ok(head))) {
        fprintf(smith_changed_out(), "%s: no base: pass --base <ref> (no origin/main or main to compare with)\n", who);
        return false;
    }
    static const char *const list[] = {"ls-files", "--cached", "--others", "--exclude-standard", nullptr};
    static const char *const others[] = {"ls-files", "--others", "--exclude-standard", nullptr};
    const char *flag = relative ? "--relative" : "--no-color"; // a flag git takes twice, or one that changes nothing
    const char *const diff[] = {"diff", "--name-status", "--no-renames", flag, base, "--", nullptr};
    const char *const range[] = {"diff", "--name-status", "--no-renames", flag, base, head, "--", nullptr};
    bool listed = smith_changed_git(list, smith_changed_on_file);
    bool diffed = smith_changed_git(head != nullptr ? range : diff, smith_changed_on_diff);
    if (head == nullptr) (void)smith_changed_git(others, smith_changed_on_untracked);
    if (!listed || !diffed) {
        fprintf(smith_changed_out(), "%s: git could not compare with %s\n", who, base);
        return false;
    }
    return true;
}

// The rest of the decision, from the files and the changed paths already read (by git, or given to the core). `gate`
// judges build.c by the functions of its diff, which asks git for them.
static SmithChangedResult smith_changed_decide(const char *base, const char *head, SmithChangedBuffer *filter,
                                               bool gate) {
    if (smith_changed_overflow) return SmithChanged_Full;
    size_t full = smith_changed_full_path();
    if (full > 0) {
        fprintf(smith_changed_out(), "smith test --changed: %s selects the whole suite\n",
                smith_changed_paths[full - 1]);
        return SmithChanged_Full;
    }
    if (gate && !smith_changed_build_c_gate(base, head)) return SmithChanged_Full;
    smith_changed_build_graph();
    smith_changed_records();
    if (smith_changed_unplaced != nullptr) {
        fprintf(smith_changed_out(),
                "smith test --changed: %s is named by no record and no file imports it, so every test runs\n",
                smith_changed_unplaced);
        return SmithChanged_Full;
    }
    int unknown = smith_changed_unknown();
    if (unknown >= 0) {
        fprintf(smith_changed_out(),
                "smith test --changed: %s is an input of no test the graph knows, so every test runs\n",
                smith_changed_paths[unknown]);
        return SmithChanged_Full;
    }
    size_t tests = 0;
    for (size_t i = 0; i < smith_changed_file_count; i++) {
        if (!smith_changed_is_test(smith_changed_files[i])) continue;
        smith_changed_judge(i);
        tests++;
    }
    size_t selected = 0;
    for (size_t i = 0; i < smith_changed_file_count; i++)
        selected += smith_changed_chosen[i] ? 1 : 0;
    smith_changed_report(selected, tests, base);
    if (selected == 0 && smith_rec_result_count == 0 && smith_changed_makes_records && tests > 0) {
        fputs("smith test --changed: no records yet, so every test runs once to make them\n", smith_changed_out());
        return SmithChanged_Full;
    }
    if (selected == 0) return SmithChanged_None;
    bool narrowed = false; // a file where records chose some of its tests
    for (size_t i = 0; i < smith_changed_file_count; i++) {
        if (!smith_changed_chosen[i]) continue;
        narrowed = narrowed || smith_changed_names[i] != nullptr;
        if (smith_changed_names[i] != nullptr)
            smith_changed_filter_names(smith_changed_names[i], filter);
        else
            smith_changed_filter_file(smith_changed_files[i], filter);
    }
    bool all = selected == tests && !narrowed;
    return smith_changed_overflow ? SmithChanged_Full : all ? SmithChanged_Full : SmithChanged_Some;
}

// Decides which tests to run from the changes `smith_changed_collect` reads. With `head`, the changes are those of
// base..head (no untracked files); the files themselves (imports, records) are still read from the working tree.
// `filter` gets the value of `--test` for SmithChanged_Some.
static SmithChangedResult smith_changed_select(const char *base_name, const char *head, SmithChangedBuffer *filter) {
    char base[256];
    if (!smith_changed_collect(base_name, head, false, "smith test --changed", base, sizeof base))
        return SmithChanged_Failed;
    return smith_changed_decide(base, head, filter, true);
}

// ---- `smith lint --changed`
//
// The files to lint after a change: those that changed since the merge-base with a ref (default origin/main, as for the
// tests; the working tree counts, untracked files too) and the files that include or import one that did, transitively,
// by the same graph as the tests'. A file the graph does not know is linted. Some paths change what clang-tidy says of
// every file, so they select all: a `.clang-tidy` anywhere, the build file (it moves the flags of a compile), and the
// shims and patches that are smith's own checks.

static bool smith_changed_lints_all(const char *path) {
    static const char *const names[] = {".clang-tidy", "build.c", "build.smith", "Makefile", nullptr};
    static const char *const folders[] = {"shim/", "patches/", nullptr};
    const char *base = strrchr(path, '/');
    base = base != nullptr ? base + 1 : path;
    for (size_t i = 0; names[i] != nullptr; i++) { // a short fixed list
        if (strcmp(base, names[i]) == 0) return true;
    }
    return smith_changed_in(path, folders);
}

static void smith_changed_on_merge_base(char *text) {
    snprintf(smith_changed_text, 256, "%s", text);
}

// Reads the changes and, unless one of them selects every file (false), the graph. `base` gets the ref compared with;
// `given` is the one asked for, whose merge-base with HEAD is used when git has one. Returns SmithChanged_Failed,
// SmithChanged_Full (every file; `why` names the path) or SmithChanged_Some (ask `smith_changed_lint_wants`).
static SmithChangedResult smith_changed_lint_select(const char *given, char *base, size_t size, const char **why) {
    char merged[256];
    if (given != nullptr && smith_changed_name_ok(given)) {
        const char *const command[] = {"merge-base", "HEAD", given, nullptr};
        smith_changed_text[0] = '\0';
        if (smith_changed_git(command, smith_changed_on_merge_base) && smith_changed_text[0] != '\0') {
            snprintf(merged, sizeof merged, "%.255s", smith_changed_text);
            given = merged;
        }
    }
    if (!smith_changed_collect(given, nullptr, true, "smith lint --changed", base, size)) return SmithChanged_Failed;
    if (smith_changed_overflow) return SmithChanged_Full;
    for (size_t p = 0; p < smith_changed_path_count; p++) {
        if (!smith_changed_lints_all(smith_changed_paths[p])) continue;
        *why = smith_changed_paths[p];
        return SmithChanged_Full;
    }
    smith_changed_build_graph();
    return SmithChanged_Some;
}

static size_t smith_changed_lint_paths(void) {
    return smith_changed_path_count;
}

// Whether `path` changed or includes or imports a file that did; a path the graph does not know is wanted.
static bool smith_changed_lint_wants(const char *path) {
    int at = smith_changed_find(path);
    return at < 0 || smith_changed_hit[at];
}

#ifdef SMITH_CORE
#include "smith_core_changed.h"

// Forgets a selection, so the next one starts from nothing.
static void smith_changed_reset(void) {
    smith_changed_pool_used = 0;
    smith_changed_file_count = 0;
    smith_changed_edge_count = 0;
    smith_changed_path_count = 0;
    smith_changed_overflow = false;
    memset(smith_changed_hit, 0, sizeof smith_changed_hit);
    memset(smith_changed_chosen, 0, sizeof smith_changed_chosen);
    memset(smith_changed_why, 0, sizeof smith_changed_why);
    memset(smith_changed_names, 0, sizeof smith_changed_names);
    memset(smith_changed_unnamed_seed, 0, sizeof smith_changed_unnamed_seed);
    memset(smith_changed_unnamed_reach, 0, sizeof smith_changed_unnamed_reach);
    smith_changed_covered = false;
    smith_changed_unplaced = nullptr;
    smith_changed_local_count = 0;
    smith_changed_build_local = false;
    smith_changed_embedded_count = 0;
    smith_changed_makes_records = false;
}

// Gives each line of `text` to `line`.
static void smith_changed_each_line(const char *text, void (*line)(char *)) {
    char copy[PATH_MAX + 8];
    for (const char *at = text; *at != '\0';) { // bounded by the text
        size_t length = strcspn(at, "\n");
        snprintf(copy, sizeof copy, "%.*s", (int)length, at);
        if (copy[0] != '\0') line(copy);
        at += length + (at[length] == '\n' ? 1 : 0);
    }
}

// include/smith_core_changed.h: the selection of `smith test --changed`, in process.
bool smith_core_changed_select(const char *folder, const char *files, const char *changes, char *out, size_t out_size) {
    int home = open(".", O_RDONLY);
    FILE *log = fmemopen(out, out_size, "w");
    bool ok = home >= 0 && log != nullptr && chdir(folder) == 0;
    if (ok) {
        smith_changed_reset();
        smith_changed_log = log;
        smith_changed_each_line(files, smith_changed_on_file);
        smith_changed_each_line(changes, smith_changed_on_diff);
        SmithChangedBuffer filter = {};
        SmithChangedResult result = smith_changed_decide("HEAD", nullptr, &filter, false);
        if (result == SmithChanged_None) {
            fputs("smith test --changed: no test is affected\n", log);
        } else {
            fprintf(log, "smith test --changed: %s\n",
                    result == SmithChanged_Some ? "--dry-run: the tests run with --test (above)"
                                                : "--dry-run: every test runs");
            if (result == SmithChanged_Some)
                fprintf(log, "smith test --changed: --test %s\n", smith_changed_buffer_text(&filter));
        }
        smith_changed_buffer_free(&filter);
        smith_changed_log = nullptr;
        ok = fchdir(home) == 0;
    }
    if (log != nullptr) fclose(log);
    if (home >= 0) close(home);
    return ok;
}
#endif
