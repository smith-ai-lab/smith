// The cache of `smith test` (issue #2686, knowledge/build-tests.md): a test that passed is not run again while nothing
// it depends on has changed. Included by runtime/verbs.c, which dispatches `smith test --cache check|store <folder>`;
// the test runner (lib/std/test.smith) calls them, once before and once after its run.
//
// A test's KEY is the SHA-256 of what is known before it runs: this format, the runner's options and its place in the
// build (the profile), the environment (`SMITH_*`), the build files, the test's name, the content of its source file
// and of every file it imports or includes, transitively (the graph of runtime/changed.c), and, when the test runs
// smith (`scratch::`, `SMITH_SELF`), the smith under test itself. Under the key, `<cache>/tests/<xx>/<key>` is the
// MANIFEST of the last pass: the files of the project the test read (`files::open`, `map` and `list` log them, #739),
// each with its content digest. A test is a HIT when its manifest exists and every file in it is as it was. A failing
// test is never stored. A test the key cannot pin down is not cached: unknown means run it. That is a test with the
// marker `// smith-test: uncached`, one that reads the clock, the network, entropy or a file without `files`, one that
// asks git, and one whose source file is not found.
#include "test_cache.h"
#include "changed.h"
#include "core.h"
#include "package.h"

#include <dirent.h>
#include <ftw.h>
#include <sys/stat.h>

constexpr size_t smith_tc_tests_max = (size_t)1 << 14;
constexpr size_t smith_tc_name_max = 256;
constexpr size_t smith_tc_reads_max = (size_t)1 << 14;
constexpr size_t smith_tc_entries_max = 4096;
constexpr size_t smith_tc_text_max = (size_t)1 << 20;
constexpr size_t smith_tc_stack_max = smith_changed_files_max;
constexpr int smith_tc_prune_after = 2678400; // an entry unused for 31 days is removed
constexpr int smith_tc_prune_interval = 604800;
constexpr char smith_tc_header[] = "smith-test-cache 1\n";

typedef struct {
    char name[smith_tc_name_max];
    char key[smith_sha256_hex_size];
    bool cacheable;
    bool hit;
} SmithTcTest;

static SmithTcTest smith_tc_tests[smith_tc_tests_max];
static size_t smith_tc_test_count;
static char smith_tc_root[PATH_MAX];
static char smith_tc_store[PATH_MAX];
static char smith_tc_runner[PATH_MAX];
static char smith_tc_options[1024];
static char smith_tc_digest[smith_changed_files_max][smith_sha256_hex_size]; // of a file of the project, once
static bool smith_tc_digested[smith_changed_files_max];
static bool smith_tc_seen[smith_changed_files_max];
static uint32_t smith_tc_stack[smith_tc_stack_max];
static char smith_tc_self_hex[smith_sha256_hex_size];
static char smith_tc_base_hex[smith_sha256_hex_size];
static bool smith_tc_why; // SMITH_RESULT_CACHE=why: say why a test runs
static char smith_tc_text[smith_tc_text_max + 1];

// The digest of the content of a file; false when it cannot be read.
static bool smith_tc_hash_path(const char *path, char hex[smith_sha256_hex_size]) {
    static uint8_t buffer[65536];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    SmithSha256 sha;
    smith_sha256_init(&sha);
    bool ok = true;
    for (size_t guard = 0; guard < ((size_t)1 << 24); guard++) { // bounded by the file
        ssize_t got = read(fd, buffer, sizeof buffer);
        if (got < 0) ok = false;
        if (got <= 0) break;
        smith_sha256_update(&sha, buffer, (size_t)got);
    }
    close(fd);
    if (ok) smith_sha256_final(&sha, hex);
    return ok;
}

static int smith_tc_compare(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

// The digest of the names in a folder (a file added or removed changes it); false when it cannot be listed.
static bool smith_tc_hash_folder(const char *path, char hex[smith_sha256_hex_size]) {
    static char pool[(size_t)1 << 18];
    static const char *names[smith_tc_entries_max];
    DIR *folder = opendir(path);
    if (folder == nullptr) return false;
    size_t count = 0;
    size_t used = 0;
    for (size_t guard = 0; guard < smith_tc_entries_max; guard++) { // bounded by the entries kept
        struct dirent *entry = readdir(folder);
        if (entry == nullptr) break;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        size_t length = strlen(entry->d_name) + 1;
        if (used + length > sizeof pool) break;
        memcpy(pool + used, entry->d_name, length);
        names[count++] = pool + used;
        used += length;
    }
    closedir(folder);
    qsort(names, count, sizeof names[0], smith_tc_compare);
    SmithSha256 sha;
    smith_sha256_init(&sha);
    for (size_t i = 0; i < count; i++) { // bounded by the names
        smith_sha256_update(&sha, (const uint8_t *)names[i], strlen(names[i]) + 1);
    }
    smith_sha256_final(&sha, hex);
    return true;
}

static void smith_tc_feed(SmithSha256 *sha, const char *text) {
    smith_sha256_update(sha, (const uint8_t *)text, strlen(text) + 1);
}

// The digest of file `index` of the project, made once.
static const char *smith_tc_file_digest(size_t index) {
    if (!smith_tc_digested[index]) {
        smith_tc_digested[index] = true;
        if (!smith_tc_hash_path(smith_changed_files[index], smith_tc_digest[index])) smith_tc_digest[index][0] = '-';
        if (smith_tc_digest[index][0] == '-') smith_tc_digest[index][1] = '\0';
    }
    return smith_tc_digest[index];
}

// Whether `$SMITH_*` variable `entry` ("NAME=value") is part of the key: those that only say where things are in this
// checkout, or how loud the run is, are not.
static bool smith_tc_env_keyed(const char *entry) {
    static const char *const skipped[] = {
        "SMITH_SELF=",       "SMITH_REPO=",    "SMITH_ARGS=",        "SMITH_CORES=",  "SMITH_SLOTS=",
        "SMITH_PRIORITY=",   "SMITH_VERBOSE=", "SMITH_READS_LOG=",   "SMITH_ACCOUNT", "SMITH_COVERAGE",
        "SMITH_RECORDS",     "SMITH_OUTPUT=",  "SMITH_RESULT_CACHE", "SMITH_QUIET",   "SMITH_LINT_CACHE=",
        "SMITH_TEST_CACHE=", "SMITH_SLOT",     "SMITH_START",        nullptr};
    if (strncmp(entry, "SMITH_", 6) != 0) return false;
    for (size_t i = 0; skipped[i] != nullptr; i++) { // a short fixed list
        if (strncmp(entry, skipped[i], strlen(skipped[i])) == 0) return false;
    }
    return true;
}

// Feeds the `SMITH_*` variables, sorted, into `sha`.
static void smith_tc_feed_environment(SmithSha256 *sha) {
    static const char *kept[256];
    size_t count = 0;
    for (size_t i = 0; environ[i] != nullptr && count < 256; i++) { // bounded by the environment and the kept
        if (smith_tc_env_keyed(environ[i])) kept[count++] = environ[i];
    }
    qsort(kept, count, sizeof kept[0], smith_tc_compare);
    for (size_t i = 0; i < count; i++) { // bounded by the kept
        smith_tc_feed(sha, kept[i]);
        if (smith_tc_why) fprintf(stderr, "smith test cache: the key holds %s\n", kept[i]);
    }
}

// Feeds the content of the project's build files (the flags a test is compiled with) and of the cwd's.
static void smith_tc_feed_build_files(SmithSha256 *sha) {
    static const char *const files[] = {"build.c", "build.smith", ".clang-format", nullptr};
    char hex[smith_sha256_hex_size];
    for (size_t i = 0; files[i] != nullptr; i++) { // a short fixed list
        if (smith_tc_hash_path(files[i], hex)) smith_tc_feed(sha, hex);
    }
    char here[PATH_MAX];
    if (getcwd(here, sizeof here) == nullptr || strcmp(here, smith_tc_root) == 0) return;
    for (size_t i = 0; files[i] != nullptr; i++) { // the same in the folder the runner runs in
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", here, files[i]);
        if (smith_tc_hash_path(path, hex)) smith_tc_feed(sha, hex);
    }
}

// What every key holds: the options and the place of the runner, the environment and the build files.
static void smith_tc_make_base(void) {
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_tc_feed(&sha, smith_tc_header);
    smith_tc_feed(&sha, smith_tc_options);
    smith_tc_feed(&sha, smith_tc_runner);
    smith_tc_feed_environment(&sha);
    smith_tc_feed_build_files(&sha);
    smith_sha256_final(&sha, smith_tc_base_hex);
}

// The path of the runner as the project spells it: without the folder of this checkout, so a worktree shares keys.
static void smith_tc_runner_path(const char *given) {
    size_t root = strlen(smith_tc_root);
    if (strncmp(given, smith_tc_root, root) == 0 && given[root] == '/')
        snprintf(smith_tc_runner, sizeof smith_tc_runner, "%s", given + root + 1);
    else
        snprintf(smith_tc_runner, sizeof smith_tc_runner, "%s", given);
}

// The files of a project that is no git checkout, for the graph: a folder walk, without `.git`, `.smith` and `build`.
static int smith_tc_walk_file(const char *path, const struct stat *info, int kind, struct FTW *where) {
    (void)info;
    (void)where;
    if (kind != FTW_F || strncmp(path, "./", 2) != 0) return 0;
    static const char *const skipped[] = {"./.git/", "./.smith/", "./build/", "./node_modules/", nullptr};
    for (size_t i = 0; skipped[i] != nullptr; i++) { // a short fixed list
        if (strncmp(path, skipped[i], strlen(skipped[i])) == 0) return 0;
    }
    smith_changed_add_file(path + 2);
    return 0;
}

// The project root: SMITH_REPO when we are inside it (the units runner runs in a folder of the checkout), else where
// we are. Goes there and lists its files for the graph: git's, else the folder's.
static bool smith_tc_open_project(void) {
    char here[PATH_MAX];
    if (getcwd(here, sizeof here) == nullptr) return false;
    const char *repo = getenv("SMITH_REPO");
    size_t length = repo != nullptr ? strlen(repo) : 0;
    bool inside = length > 1 && strncmp(here, repo, length) == 0 && (here[length] == '/' || here[length] == '\0');
    snprintf(smith_tc_root, sizeof smith_tc_root, "%s", inside ? repo : here);
    static const char *const list[] = {"ls-files", "--cached", "--others", "--exclude-standard", nullptr};
    if (chdir(smith_tc_root) != 0) return false;
    if (!smith_changed_git(list, smith_changed_on_file) || smith_changed_file_count == 0) {
        smith_changed_file_count = 0;
        if (nftw(".", smith_tc_walk_file, 32, FTW_PHYS) != 0) return false;
    }
    smith_changed_build_graph();
    return !smith_changed_overflow;
}

// Whether the text of a test's source says it cannot be cached: its marker, or a use of what changes by itself.
static bool smith_tc_nondeterministic(const char *text) {
    static const char *const words[] = {"smith-test: uncached",
                                        "clock::now",
                                        "clock::monotonic",
                                        "clock::cpu",
                                        "net::",
                                        "http::",
                                        "entropy::",
                                        "fopen(",
                                        "opendir(",
                                        "getpid(",
                                        "\"git\"",
                                        "gettimeofday(",
                                        "clock_gettime(",
                                        "time(nullptr)",
                                        "time(NULL)",
                                        nullptr};
    for (size_t i = 0; words[i] != nullptr; i++) { // a short fixed list
        if (strstr(text, words[i]) != nullptr) return true;
    }
    return false;
}

// Whether `path` is the test file of module `module` (its last segment, `a::b_test` is `b_test`).
static bool smith_tc_names_module(const char *path, const char *module, size_t length) {
    const char *slash = strrchr(path, '/');
    const char *base = slash != nullptr ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    if (dot == nullptr || (size_t)(dot - base) != length || strncmp(base, module, length) != 0) return false;
    return smith_changed_is_test(path);
}

// The tests of a file that declares no module are named by their functions (`test_x` is `x`): which file defines
// which, found once from the sources.
constexpr size_t smith_tc_functions_max = (size_t)1 << 15;
static char smith_tc_function_pool[(size_t)1 << 21];
static const char *smith_tc_function_name[smith_tc_functions_max];
static uint32_t smith_tc_function_file[smith_tc_functions_max];
static size_t smith_tc_function_count;

static void smith_tc_index_text(uint32_t file) {
    static size_t used;
    if (strstr(smith_changed_text, "\nmodule ") != nullptr || strncmp(smith_changed_text, "module ", 7) == 0) return;
    for (const char *at = strstr(smith_changed_text, "void test_"); at != nullptr; at = strstr(at + 10, "void test_")) {
        size_t length = 0;
        while (smith_changed_word(at[10 + length]))
            length++;
        bool defined = at[10 + length] == '(' && (at == smith_changed_text || at[-1] == '\n' || at[-1] == ' ');
        if (!defined || length == 0 || smith_tc_function_count >= smith_tc_functions_max) continue;
        if (used + length + 1 > sizeof smith_tc_function_pool) return;
        memcpy(smith_tc_function_pool + used, at + 10, length);
        smith_tc_function_pool[used + length] = '\0';
        smith_tc_function_name[smith_tc_function_count] = smith_tc_function_pool + used;
        smith_tc_function_file[smith_tc_function_count++] = file;
        used += length + 1;
    }
}

// A file that may hold tests: `x_test.smith`, `x_test.c`, or the `test_x.smith` and `test_x.c` of a build's glob.
static bool smith_tc_test_file(const char *path) {
    if (smith_changed_is_test(path)) return true;
    const char *slash = strrchr(path, '/');
    const char *base = slash != nullptr ? slash + 1 : path;
    return strncmp(base, "test_", 5) == 0 && (smith_changed_ends(base, ".smith") || smith_changed_ends(base, ".c"));
}

static void smith_tc_index_functions(void) {
    for (size_t f = 0; f < smith_changed_file_count; f++) { // bounded by the files
        if (!smith_tc_test_file(smith_changed_files[f])) continue;
        if (smith_changed_read(smith_changed_files[f]) > 0) smith_tc_index_text((uint32_t)f);
    }
}

// The source files a test comes from: the test file of its module, or the files that define a function of its name.
static size_t smith_tc_sources(const char *name, uint32_t *out, size_t max) {
    size_t count = 0;
    const char *last = strrchr(name, ':');
    if (last != nullptr && last > name && last[-1] == ':') {
        const char *end = last - 1;
        const char *start = end;
        while (start > name && start[-1] != ':')
            start--;
        for (size_t f = 0; f < smith_changed_file_count && count < max; f++) { // bounded by the files
            if (smith_tc_names_module(smith_changed_files[f], start, (size_t)(end - start))) out[count++] = (uint32_t)f;
        }
        return count;
    }
    for (size_t i = 0; i < smith_tc_function_count && count < max; i++) { // bounded by the functions
        if (strcmp(smith_tc_function_name[i], name) == 0) out[count++] = smith_tc_function_file[i];
    }
    return count;
}

// Walks the files `first` imports or includes, transitively: marks them in smith_tc_seen and gives their count. The
// walk keeps its own stack, no recursion.
static size_t smith_tc_closure(size_t first) {
    memset(smith_tc_seen, 0, sizeof smith_tc_seen);
    size_t top = 0;
    size_t count = 0;
    smith_tc_stack[top++] = (uint32_t)first;
    smith_tc_seen[first] = true;
    while (top > 0) { // each file is pushed once
        uint32_t at = smith_tc_stack[--top];
        count++;
        for (size_t e = 0; e < smith_changed_edge_count; e++) { // bounded by the edges
            uint32_t to = smith_changed_to[e];
            if (smith_changed_from[e] != at || smith_tc_seen[to]) continue;
            smith_tc_seen[to] = true;
            if (top < smith_tc_stack_max) smith_tc_stack[top++] = to;
        }
    }
    return count;
}

// Feeds the files of the closure marked in smith_tc_seen; says whether one of the tests' own sources runs smith, and
// whether one cannot be cached. A `tests/` helper counts as a test's own source.
static void smith_tc_feed_closure(SmithSha256 *sha, bool *runs_smith, bool *uncached) {
    for (size_t f = 0; f < smith_changed_file_count; f++) { // bounded by the files
        if (!smith_tc_seen[f]) continue;
        smith_tc_feed(sha, smith_changed_files[f]);
        smith_tc_feed(sha, smith_tc_file_digest(f));
        bool own = smith_tc_test_file(smith_changed_files[f]) || smith_changed_starts(smith_changed_files[f], "tests/");
        if (!own || !smith_changed_is_source(smith_changed_files[f])) continue;
        size_t size = smith_changed_read(smith_changed_files[f]);
        if (size == 0) continue;
        if (smith_changed_runs_smith(smith_changed_text)) *runs_smith = true;
        if (smith_tc_nondeterministic(smith_changed_text)) *uncached = true;
    }
}

// The key of one test, or false when it cannot be cached (why says which).
static bool smith_tc_key(SmithTcTest *test, const char **why) {
    uint32_t sources[8];
    size_t found = smith_tc_sources(test->name, sources, 8);
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_tc_feed(&sha, smith_tc_base_hex);
    smith_tc_feed(&sha, test->name);
    bool runs_smith = false;
    bool uncached = false;
    for (size_t i = 0; i < found; i++) { // bounded by the files
        smith_tc_closure(sources[i]);
        smith_tc_feed_closure(&sha, &runs_smith, &uncached);
    }
    if (found == 0) *why = "no source file for its module or its function";
    if (uncached) *why = "its source reads the clock, the network, a file without files::, or asks to be uncached";
    if (found == 0 || uncached) return false;
    if (runs_smith) {
        if (smith_tc_self_hex[0] == '\0') {
            *why = "it runs smith and the smith under test (SMITH_SELF) cannot be read";
            return false;
        }
        smith_tc_feed(&sha, smith_tc_self_hex);
    }
    smith_sha256_final(&sha, test->key);
    return true;
}

static void smith_tc_entry_path(const char *key, char *out, size_t size) {
    snprintf(out, size, "%s/%.2s/%s", smith_tc_store, key, key);
}

// Whether every file a manifest lists is as it was. A line is `f <digest> <path>` for a file and `d <digest> <path>`
// for a folder, a path from the root of the project.
static bool smith_tc_manifest_holds(const char *key) {
    char path[PATH_MAX];
    smith_tc_entry_path(key, path, sizeof path);
    FILE *file = fopen(path, "r");
    if (file == nullptr) return false;
    char line[PATH_MAX + 80];
    bool ok = fgets(line, sizeof line, file) != nullptr && strcmp(line, smith_tc_header) == 0;
    for (size_t guard = 0; ok && guard < smith_tc_reads_max; guard++) { // bounded by the reads kept
        if (fgets(line, sizeof line, file) == nullptr) break;
        line[strcspn(line, "\n")] = '\0';
        char hex[smith_sha256_hex_size];
        bool same = line[0] != '\0' && line[1] == ' ' && strlen(line) > 67 && line[66] == ' ';
        if (!same) {
            ok = false;
        } else if (line[0] == 'f') {
            ok = smith_tc_hash_path(line + 67, hex) && strncmp(hex, line + 2, 64) == 0;
        } else {
            ok = smith_tc_hash_folder(line + 67, hex) && strncmp(hex, line + 2, 64) == 0;
        }
    }
    fclose(file);
    if (ok) (void)utimensat(AT_FDCWD, path, nullptr, 0); // used now: pruning goes by the time
    return ok;
}

static bool smith_tc_ensure_folder(const char *path) {
    char copy[PATH_MAX];
    snprintf(copy, sizeof copy, "%s", path);
    for (char *slash = strchr(copy + 1, '/'); slash != nullptr; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        if (mkdir(copy, 0777) != 0 && errno != EEXIST) return false;
        *slash = '/';
    }
    return mkdir(copy, 0777) == 0 || errno == EEXIST;
}

static bool smith_tc_store_folder(void) {
    SmithPackageFault fault = {};
    char base[PATH_MAX];
    if (!smith_cache_base(base, &fault)) return false;
    snprintf(smith_tc_store, sizeof smith_tc_store, "%s/tests", base);
    return true;
}

// Reads the file `path` into smith_tc_text; false when it cannot be read.
static bool smith_tc_slurp(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return false;
    size_t size = fread(smith_tc_text, 1, smith_tc_text_max, file);
    fclose(file);
    smith_tc_text[size] = '\0';
    return true;
}

// `<folder>/names` (one test a line) and `<folder>/options` (a line the runner writes: its options and its path) are
// read; `<folder>/hits` gets the names of the tests whose entry holds, and `<folder>/keys` `name<TAB>key` for each test
// that can be cached, for `store`.
static void smith_tc_load_names(const char *folder) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/names", folder);
    FILE *file = fopen(path, "r");
    if (file == nullptr) return;
    for (size_t guard = 0; guard < smith_tc_tests_max; guard++) { // bounded by the tests
        SmithTcTest *test = &smith_tc_tests[smith_tc_test_count];
        if (fgets(test->name, sizeof test->name, file) == nullptr) break;
        test->name[strcspn(test->name, "\n")] = '\0';
        if (test->name[0] != '\0') smith_tc_test_count++;
    }
    fclose(file);
}

static bool smith_tc_load_options(const char *folder) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/options", folder);
    if (!smith_tc_slurp(path)) return false;
    char *newline = strchr(smith_tc_text, '\n');
    if (newline == nullptr) return false;
    *newline = '\0';
    snprintf(smith_tc_options, sizeof smith_tc_options, "%s", smith_tc_text);
    smith_tc_runner_path(newline + 1);
    smith_tc_runner[strcspn(smith_tc_runner, "\n")] = '\0';
    return true;
}

static void smith_tc_say(const char *name, const char *why) {
    if (smith_tc_why) fprintf(stderr, "smith test cache: %s runs: %s\n", name, why);
}

static void smith_tc_write_results(const char *folder) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/hits", folder);
    FILE *hits = fopen(path, "w");
    snprintf(path, sizeof path, "%s/keys", folder);
    FILE *keys = fopen(path, "w");
    for (size_t i = 0; hits != nullptr && keys != nullptr && i < smith_tc_test_count; i++) { // bounded by the tests
        const SmithTcTest *test = &smith_tc_tests[i];
        if (test->hit) fprintf(hits, "%s\n", test->name);
        if (test->cacheable) fprintf(keys, "%s\t%s\n", test->name, test->key);
    }
    if (hits != nullptr) fclose(hits);
    if (keys != nullptr) fclose(keys);
}

// `smith test --cache check <folder>`: which of the tests would pass again. Exit 0 whatever happens: no cache is only
// a run of everything.
static int smith_tc_check(const char *folder) {
    smith_tc_why = getenv("SMITH_RESULT_CACHE") != nullptr && strcmp(getenv("SMITH_RESULT_CACHE"), "why") == 0;
    smith_tc_load_names(folder);
    if (!smith_tc_load_options(folder) || !smith_tc_store_folder() || !smith_tc_open_project()) {
        smith_tc_say("every test", "the project or the cache folder cannot be opened");
        return 0;
    }
    const char *self = getenv("SMITH_SELF");
    if (self != nullptr && self[0] != '\0' && !smith_tc_hash_path(self, smith_tc_self_hex)) smith_tc_self_hex[0] = '\0';
    smith_tc_make_base();
    smith_tc_index_functions();
    for (size_t i = 0; i < smith_tc_test_count; i++) { // bounded by the tests
        SmithTcTest *test = &smith_tc_tests[i];
        const char *why = "";
        test->cacheable = smith_tc_key(test, &why);
        if (!test->cacheable) smith_tc_say(test->name, why);
        test->hit = test->cacheable && smith_tc_manifest_holds(test->key);
        if (test->cacheable && !test->hit) smith_tc_say(test->name, "no entry, or a file it read has changed");
    }
    smith_tc_write_results(folder);
    return 0;
}

// The name a test's reads are logged under: letters, digits and `_`, as the runner writes it.
static void smith_tc_clean(const char *name, char *out, size_t size) {
    size_t used = 0;
    for (; name[used] != '\0' && used + 1 < size; used++) {
        char c = name[used];
        bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        out[used] = word ? c : '_';
    }
    out[used] = '\0';
}

// Writes the manifest of one passed test from the log of what it read: only paths inside the project count.
static void smith_tc_write_entry(const char *key, const char *reads_path) {
    char path[PATH_MAX];
    smith_tc_entry_path(key, path, sizeof path);
    char folder[PATH_MAX];
    snprintf(folder, sizeof folder, "%.*s", (int)(strrchr(path, '/') - path), path);
    if (!smith_tc_ensure_folder(folder)) return;
    char temp[PATH_MAX + 32];
    snprintf(temp, sizeof temp, "%s.%d", path, (int)getpid());
    FILE *out = fopen(temp, "w");
    if (out == nullptr) return;
    fputs(smith_tc_header, out);
    FILE *reads = fopen(reads_path, "r");
    size_t root = strlen(smith_tc_root);
    char line[PATH_MAX];
    for (size_t guard = 0; reads != nullptr && guard < smith_tc_reads_max; guard++) { // bounded by the reads kept
        if (fgets(line, sizeof line, reads) == nullptr) break;
        line[strcspn(line, "\n")] = '\0';
        if (strncmp(line, smith_tc_root, root) != 0 || line[root] != '/' || line[root + 1] == '\0') continue;
        char hex[smith_sha256_hex_size];
        size_t length = strlen(line);
        bool folder_read = line[length - 1] == '/';
        if (folder_read) line[length - 1] = '\0';
        bool ok = folder_read ? smith_tc_hash_folder(line, hex) : smith_tc_hash_path(line, hex);
        fprintf(out, "%c %s %s\n", folder_read ? 'd' : 'f', ok ? hex : "-", line + root + 1);
    }
    if (reads != nullptr) fclose(reads);
    bool good = fclose(out) == 0 && rename(temp, path) == 0;
    if (!good) (void)unlink(temp);
}

// Removes the entries unused for 31 days, at most once a week (the rule of the other caches).
static void smith_tc_prune(void) {
    char stamp[PATH_MAX];
    snprintf(stamp, sizeof stamp, "%s/prune.timestamp", smith_tc_store);
    struct stat info;
    time_t now = time(nullptr);
    if (stat(stamp, &info) == 0 && now - info.st_mtime < smith_tc_prune_interval) return;
    int touched = open(stamp, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
    if (touched >= 0) {
        (void)futimens(touched, nullptr);
        close(touched);
    }
    for (int shard = 0; shard < 256; shard++) { // the 256 folders
        char folder[PATH_MAX];
        snprintf(folder, sizeof folder, "%s/%02x", smith_tc_store, shard);
        DIR *dir = opendir(folder);
        for (size_t guard = 0; dir != nullptr && guard < ((size_t)1 << 20); guard++) { // bounded by the entries
            struct dirent *entry = readdir(dir);
            if (entry == nullptr) break;
            char path[PATH_MAX];
            snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
            if (entry->d_name[0] != '.' && stat(path, &info) == 0 && now - info.st_mtime > smith_tc_prune_after)
                (void)unlink(path);
        }
        if (dir != nullptr) closedir(dir);
    }
}

// The names in `<folder>/passed`, sorted, in a pool; their count.
static size_t smith_tc_load_passed(const char *folder, const char **names) {
    static char pool[(size_t)1 << 21];
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/passed", folder);
    FILE *file = fopen(path, "r");
    if (file == nullptr) return 0;
    size_t used = 0;
    size_t count = 0;
    char name[smith_tc_name_max];
    for (size_t guard = 0; guard < smith_tc_tests_max; guard++) { // bounded by the passed tests
        if (fgets(name, sizeof name, file) == nullptr) break;
        size_t length = strcspn(name, "\n");
        if (length == 0 || used + length + 1 > sizeof pool) continue;
        memcpy(pool + used, name, length);
        pool[used + length] = '\0';
        names[count++] = pool + used;
        used += length + 1;
    }
    fclose(file);
    qsort(names, count, sizeof names[0], smith_tc_compare);
    return count;
}

// `smith test --cache store <folder>`: for the tests of `<folder>/passed` (names), each with its key in `keys`, writes
// the manifest from `<folder>/reads/<clean name>.txt`.
static int smith_tc_store_main(const char *folder) {
    static const char *names[smith_tc_tests_max];
    char path[PATH_MAX];
    if (!smith_tc_store_folder() || !smith_tc_load_options(folder) || !smith_tc_open_project()) return 0;
    size_t count = smith_tc_load_passed(folder, names);
    snprintf(path, sizeof path, "%s/keys", folder);
    FILE *keys = fopen(path, "r");
    char row[smith_tc_name_max + 80];
    for (size_t guard = 0; keys != nullptr && guard < smith_tc_tests_max; guard++) { // bounded by the keys
        if (fgets(row, sizeof row, keys) == nullptr) break;
        char *tab = strchr(row, '\t');
        if (tab == nullptr) continue;
        *tab = '\0';
        const char *key = tab + 1;
        const char *name = row;
        tab[1 + strcspn(key, "\n")] = '\0';
        if (bsearch(&name, names, count, sizeof names[0], smith_tc_compare) == nullptr) continue;
        char clean[smith_tc_name_max];
        smith_tc_clean(name, clean, sizeof clean);
        snprintf(path, sizeof path, "%s/reads/%s.txt", folder, clean);
        smith_tc_write_entry(key, path);
    }
    if (keys != nullptr) fclose(keys);
    smith_tc_prune();
    return 0;
}

// `smith test --cache check|store <folder>` (the runner's calls).
int smith_tc_main(int count, char **args) {
    if (count != 2) {
        fputs("usage: smith test --cache check|store <folder>\n", stderr);
        return smith_exit_usage;
    }
    if (strcmp(args[0], "check") == 0) return smith_tc_check(args[1]);
    if (strcmp(args[0], "store") == 0) return smith_tc_store_main(args[1]);
    fputs("usage: smith test --cache check|store <folder>\n", stderr);
    return smith_exit_usage;
}
