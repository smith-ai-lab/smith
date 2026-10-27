// `smith test --changed` (issue #441): the tests a change can affect, found from what the compiler knows and never
// declared by a test. knowledge/changed-tests.md. Included by smith.c, before runtime/verbs.c.
//
// The reverse dependency graph, and nothing else: every source of the project (`.smith`, `.c`, `.h`, `.cpp`, `.hpp`,
// `.m`) is scanned for its `module`, `import` and `#include` lines, an edge goes to each project file they name, and
// the files that import a changed file, transitively, are changed too. A test file is selected when it is one of
// them, or when it runs smith (`scratch::`, `SMITH_SELF`) and a source of smith changed (an edge from the test to
// everything compiled into smith). A file the graph does not know selects nothing; the train's full run is the net.
// A few files select the whole suite (the full-check list below).
constexpr size_t smith_changed_files_max = 16384;
constexpr size_t smith_changed_edges_max = 1U << 17;
constexpr size_t smith_changed_pool_max = 1U << 21;
constexpr size_t smith_changed_text_max = 1U << 21;
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

// The files that select the whole suite: the build file, the driver, the lowering, the patches, CI, the shims and the
// runner's own build.
static const char *const smith_changed_full[] = {"build.c",  "smith.c", "runtime/lang", "patches/",
                                                 ".github/", "shim/",   "tests/units/", nullptr};
// What is compiled into smith: a test that runs smith depends on all of it.
static const char *const smith_changed_smith[] = {"smith.c",      "runtime/", "shim/",      "patches/",
                                                  "include/",     "lib/",     "templates/", "scripts/",
                                                  "tree-sitter/", "plugin/",  "editors/",   nullptr};

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

static bool smith_changed_runs_smith(const char *text) {
    return strstr(text, "scratch::") != nullptr || strstr(text, "SMITH_SELF") != nullptr;
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

// One test file: selected when the graph reaches it from a changed file, or when it runs smith and a source of smith
// changed (an edge from the test to everything compiled into smith).
static void smith_changed_judge(size_t test) {
    int dependency = smith_changed_dependency(test);
    if (dependency >= 0)
        smith_changed_choose(test, dependency == (int)test ? "it changed" : "it depends on %s",
                             smith_changed_files[dependency]);
    if (smith_changed_chosen[test] || smith_changed_read(smith_changed_files[test]) == 0) return;
    if (!smith_changed_runs_smith(smith_changed_text)) return;
    for (size_t p = 0; p < smith_changed_path_count; p++) {
        if (smith_changed_in_smith(smith_changed_paths[p])) {
            smith_changed_choose(test, "it runs smith, and %s is part of smith", smith_changed_paths[p]);
            return;
        }
    }
}

// ---- The filter

static void smith_changed_filter_add(char *filter, size_t size, const char *name) {
    size_t used = strlen(filter);
    if (used + strlen(name) + 2 >= size) {
        smith_changed_overflow = true;
        return;
    }
    snprintf(filter + used, size - used, "%s%s", used > 0 ? "," : "", name);
}

// What `--test` needs to run the tests of a file: `module::` for a module, else the names of its `test_` functions.
static void smith_changed_filter_file(const char *path, char *filter, size_t size) {
    if (smith_changed_read(path) == 0) return;
    char *module = strstr(smith_changed_text, "\nmodule ");
    if (module != nullptr || smith_changed_starts(smith_changed_text, "module ")) {
        module = module != nullptr ? module + 8 : smith_changed_text + 7;
        char name[256];
        snprintf(name, sizeof name, "%.*s::", (int)strcspn(module, " ;\n"), module);
        smith_changed_filter_add(filter, size, name);
        return;
    }
    char *at = smith_changed_text;
    for (size_t guard = 0; (at = strstr(at, "void test_")) != nullptr && guard < smith_changed_text_max; guard++) {
        at += 10;
        char name[256];
        snprintf(name, sizeof name, "%.*s", (int)strcspn(at, "("), at);
        smith_changed_filter_add(filter, size, name);
    }
}

// ---- The selection

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
        if (smith_changed_in(smith_changed_paths[p], smith_changed_full)) return p + 1;
    }
    return 0;
}

static void smith_changed_report(size_t selected, size_t tests, const char *base) {
    fprintf(stderr, "smith test --changed: %zu changed file%s since %.12s: %zu of %zu test files selected\n",
            smith_changed_path_count, smith_changed_path_count == 1 ? "" : "s", base, selected, tests);
    for (size_t i = 0; i < smith_changed_file_count; i++) {
        if (smith_changed_chosen[i])
            fprintf(stderr, "smith test --changed:   %s: %s\n", smith_changed_files[i], smith_changed_why[i]);
    }
}

// Reads the changes since `base_name` (or where the branch left main) in the current folder's git repository and
// decides which tests to run. `filter` gets the value of `--test` for SmithChanged_Some.
static SmithChangedResult smith_changed_select(const char *base_name, char *filter, size_t size) {
    char base[256];
    filter[0] = '\0';
    if (!smith_changed_base(base_name, base, sizeof base)) {
        fprintf(stderr, "smith test --changed: no base: pass --base <ref> (no origin/main or main to compare with)\n");
        return SmithChanged_Failed;
    }
    static const char *const list[] = {"ls-files", "--cached", "--others", "--exclude-standard", nullptr};
    static const char *const others[] = {"ls-files", "--others", "--exclude-standard", nullptr};
    const char *const diff[] = {"diff", "--name-status", "--no-renames", base, "--", nullptr};
    bool listed = smith_changed_git(list, smith_changed_on_file);
    bool diffed = smith_changed_git(diff, smith_changed_on_diff);
    (void)smith_changed_git(others, smith_changed_on_untracked);
    if (!listed || !diffed) {
        fprintf(stderr, "smith test --changed: git could not compare with %s\n", base);
        return SmithChanged_Failed;
    }
    if (smith_changed_overflow) return SmithChanged_Full;
    size_t full = smith_changed_full_path();
    if (full > 0) {
        fprintf(stderr, "smith test --changed: %s selects the whole suite\n", smith_changed_paths[full - 1]);
        return SmithChanged_Full;
    }
    smith_changed_build_graph();
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
    if (selected == 0) return SmithChanged_None;
    for (size_t i = 0; i < smith_changed_file_count; i++) {
        if (smith_changed_chosen[i]) smith_changed_filter_file(smith_changed_files[i], filter, size);
    }
    return smith_changed_overflow ? SmithChanged_Full : selected == tests ? SmithChanged_Full : SmithChanged_Some;
}
