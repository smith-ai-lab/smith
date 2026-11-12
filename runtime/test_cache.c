// The cache of `smith test` (issue #2686, #3177, knowledge/build-tests.md): a test that passed is not run again while
// nothing it depends on has changed. Included by runtime/verbs.c, which dispatches `smith test --cache check|store
// <folder>`; the test runner (lib/std/test.smith) calls them, once before and once after its run.
//
// A test's KEY is the SHA-256 of what is known before it runs: this format, the runner's options and its place in the
// build (the profile), the environment (`SMITH_*`), the build files, the test's name, the content of its source file
// and of every file it imports or includes, transitively (the graph of runtime/changed.c), and, when the test runs
// smith (`scratch::`, `SMITH_SELF`), the smith under test itself. Under the key, an answer in the one store
// (runtime/executor_cache.c, `steps/a/`) is the MANIFEST of the last pass: the files of the project the test read
// (`files::open`, `map` and `list` log them, #739), each with its content digest. A test is a HIT when its manifest
// exists and every file in it is as it was. A failing test is never stored. A test the key cannot pin down is not
// cached: unknown means run it. That is a test with the marker `// smith-test: uncached`, one that reads the clock, the
// network, entropy or a file without `files`, one that asks git, and one whose source file is not found.
#include "test_cache.h"
#include "changed.h"
#include "core.h"
#include "executor_cache.h"
#include "package.h"
#include "platform_linux.h"
#include "record.h"
#include "self_digest.h"

#include <dirent.h>
#include <ftw.h>
#include <sys/stat.h>

constexpr size_t smith_tc_tests_max = (size_t)1 << 14;
constexpr size_t smith_tc_name_max = 256;
constexpr size_t smith_tc_reads_max = (size_t)1 << 14;
constexpr size_t smith_tc_entries_max = 4096;
constexpr size_t smith_tc_text_max = (size_t)1 << 20;
constexpr size_t smith_tc_stack_max = smith_changed_files_max;
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
static char smith_tc_manifest[smith_tc_text_max + 1]; // a stored manifest, read whole
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

// What the base of every key holds, as `<name>\t<value>` lines, so a miss can say which input differs (#2876). The base
// is an answer named by the digest of these lines, and the last base stored is another, for the next run to compare
// with.
constexpr size_t smith_tc_notes_max = 16384;
constexpr size_t smith_tc_report_max = 5; // miss lines a run prints
constexpr size_t smith_tc_value_max = 120;
static char smith_tc_notes[smith_tc_notes_max];
static size_t smith_tc_notes_used;
static char smith_tc_base_name[smith_sha256_hex_size];
static char smith_tc_stale[PATH_MAX]; // the first file of a manifest that is not as it was
static bool smith_tc_report;          // CI or SMITH_RESULT_CACHE=why: print the hit rate and the first misses

static void smith_tc_note(const char *name, const char *value) {
    int put = snprintf(smith_tc_notes + smith_tc_notes_used, smith_tc_notes_max - smith_tc_notes_used, "%s\t%.*s\n",
                       name, (int)smith_tc_value_max, value);
    if (put > 0 && smith_tc_notes_used + (size_t)put < smith_tc_notes_max) smith_tc_notes_used += (size_t)put;
}

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
    qsort((void *)names, count, sizeof names[0], smith_tc_compare);
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
        "SMITH_SELF=",       "SMITH_REPO=",    "SMITH_ARGS=",        "SMITH_CORES=",    "SMITH_SLOTS=",
        "SMITH_PRIORITY=",   "SMITH_VERBOSE=", "SMITH_READS_LOG=",   "SMITH_ACCOUNT",   "SMITH_COVERAGE",
        "SMITH_RECORDS",     "SMITH_OUTPUT=",  "SMITH_RESULT_CACHE", "SMITH_QUIET",     "SMITH_LINT_CACHE=",
        "SMITH_TEST_CACHE=", "SMITH_SLOT",     "SMITH_START",        "SMITH_BUILD_ID=", nullptr};
    if (strncmp(entry, "SMITH_", 6) != 0) return false;
    // where the import cache counts its hits (#2891), set by CI's test-cache action in every job
    static const char import_log[] = "SMITH_IMPORT_CACHE_LOG=";
    if (strncmp(entry, import_log, sizeof import_log - 1) == 0) return false;
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
    qsort((void *)kept, count, sizeof kept[0], smith_tc_compare);
    for (size_t i = 0; i < count; i++) { // bounded by the kept
        smith_tc_feed(sha, kept[i]);
        const char *equals = strchr(kept[i], '=');
        char name[96];
        snprintf(name, sizeof name, "environment %.*s", equals != nullptr ? (int)(equals - kept[i]) : 80, kept[i]);
        smith_tc_note(name, equals != nullptr ? equals + 1 : "");
        if (smith_tc_why) fprintf(stderr, "smith test cache: the key holds %s\n", kept[i]);
    }
}

// Feeds the content of the project's build files (the flags a test is compiled with) and of the cwd's.
static void smith_tc_feed_build_files(SmithSha256 *sha) {
    static const char *const files[] = {"build.c", "build.smith", ".clang-format", nullptr};
    char hex[smith_sha256_hex_size];
    for (size_t i = 0; files[i] != nullptr; i++) { // a short fixed list
        if (smith_tc_hash_path(files[i], hex)) {
            smith_tc_feed(sha, hex);
            char name[96];
            snprintf(name, sizeof name, "build file %s", files[i]);
            smith_tc_note(name, hex);
        }
    }
    char here[PATH_MAX];
    if (getcwd(here, sizeof here) == nullptr || strcmp(here, smith_tc_root) == 0) return;
    for (size_t i = 0; files[i] != nullptr; i++) { // the same in the folder the runner runs in
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", here, files[i]);
        if (smith_tc_hash_path(path, hex)) {
            smith_tc_feed(sha, hex);
            char name[96];
            snprintf(name, sizeof name, "build file %s (in the runner's folder)", files[i]);
            smith_tc_note(name, hex);
        }
    }
}

// What every key holds: the options and the place of the runner, the environment and the build files.
static void smith_tc_make_base(void) {
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_tc_notes_used = 0;
    smith_tc_notes[0] = '\0';
    smith_tc_feed(&sha, smith_tc_header);
    smith_tc_feed(&sha, smith_tc_options);
    smith_tc_note("options of the run", smith_tc_options);
    smith_tc_feed(&sha, smith_tc_runner);
    smith_tc_note("runner", smith_tc_runner);
    smith_tc_feed_environment(&sha);
    smith_tc_feed_build_files(&sha);
    smith_tc_note("smith under test (SMITH_SELF)", smith_tc_self_hex);
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

// Records (#456, #3387): a test that runs smith is keyed by the smith functions its child executed (its record in
// `.smith/records`, made only by a run with COVERAGE=1), not by the whole smith. The record is valid only against the
// digests it was made with: every function, outside and input line is checked against the tree now, and a record with
// one that differs (a function changed or gone, the test failed, a file it read changed) is stale, which keys the test
// by the whole smith as before, until a coverage run writes the record again. A plain run never writes one.
static bool smith_tc_records_read;
static char smith_tc_toolchain_hex[smith_sha256_hex_size];

// Reads the records of the project (cwd is its root, smith_tc_open_project).
static void smith_tc_load_records(void) {
    char folder[PATH_MAX];
    const char *given = getenv("SMITH_RECORDS_DIR");
    snprintf(smith_cov_root, sizeof smith_cov_root, "%s", smith_tc_root);
    snprintf(folder, sizeof folder, "%s/.smith/records", smith_tc_root);
    smith_rec_result_count = 0;
    (void)smith_rec_check_folder(given != nullptr && given[0] != '\0' ? given : folder);
    smith_tc_records_read = true;
}

// The digest of what a record cannot name, because it is no function smith ran in its own process: the files
// smith.c embeds (the children compile them), shim/, patches/ and templates/. A change to one is a change to every
// test that runs smith.
static void smith_tc_toolchain_digest(void) {
    SmithSha256 sha;
    smith_sha256_init(&sha);
    char hex[smith_sha256_hex_size];
    for (size_t i = 0; i < smith_changed_file_count; i++) { // bounded by the files
        const char *path = smith_changed_files[i];
        if (strncmp(path, "shim/", 5) != 0 && strncmp(path, "patches/", 8) != 0 && strncmp(path, "templates/", 10) != 0)
            continue;
        smith_tc_feed(&sha, path);
        smith_tc_feed(&sha, smith_tc_file_digest(i));
    }
    FILE *source = fopen("smith.c", "r");
    char line[PATH_MAX + 16];
    for (size_t guard = 0; source != nullptr && guard < (size_t)1 << 16 && fgets(line, sizeof line, source) != nullptr;
         guard++) { // bounded by the lines
        if (strncmp(line, "#embed \"", 8) != 0) continue;
        char *end = strchr(line + 8, '"');
        if (end == nullptr) continue;
        *end = '\0';
        smith_tc_feed(&sha, line + 8);
        smith_tc_feed(&sha, smith_tc_hash_path(line + 8, hex) ? hex : "-");
    }
    if (source != nullptr) fclose(source);
    smith_sha256_final(&sha, smith_tc_toolchain_hex);
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
        const SmithRecResult *record = smith_tc_records_read ? smith_rec_result_of(test->name) : nullptr;
        if (record != nullptr && !record->stale && record->functions > 0) {
            char digest[20];
            snprintf(digest, sizeof digest, "%016llx", (unsigned long long)record->digest);
            if (smith_tc_toolchain_hex[0] == '\0') smith_tc_toolchain_digest();
            smith_tc_feed(&sha, "record");
            smith_tc_feed(&sha, digest);
            smith_tc_feed(&sha, smith_tc_toolchain_hex);
        } else {
            smith_tc_feed(&sha, smith_tc_self_hex);
        }
    }
    smith_sha256_final(&sha, test->key);
    return true;
}

// The address of an answer that is not a test's key: the SHA-256 of a tag and a name.
static void smith_tc_address(const char *tag, const char *name, char hex[smith_sha256_hex_size]) {
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_sha256_update(&sha, (const uint8_t *)tag, strlen(tag) + 1);
    smith_sha256_update(&sha, (const uint8_t *)name, strlen(name) + 1);
    smith_sha256_final(&sha, hex);
}

// Whether an answer is in the store.
static bool smith_tc_stored(const char *key) {
    char path[PATH_MAX];
    struct stat info;
    return smith_stepcache_answer_file(key, path) && stat(path, &info) == 0;
}

// Whether every file a manifest lists is as it was. A line is `f <digest> <path>` for a file and `d <digest> <path>`
// for a folder, a path from the root of the project.
static bool smith_tc_manifest_holds(const char *key) {
    size_t size = 0;
    if (!smith_stepcache_answer_get(key, smith_tc_manifest, smith_tc_text_max, &size)) return false;
    smith_tc_manifest[size] = '\0';
    smith_tc_stale[0] = '\0';
    size_t header = sizeof smith_tc_header - 1;
    bool ok = strncmp(smith_tc_manifest, smith_tc_header, header) == 0;
    char *at = smith_tc_manifest + header;
    for (size_t guard = 0; ok && *at != '\0' && guard < smith_tc_reads_max; guard++) { // bounded by the reads kept
        char *end = strchr(at, '\n');
        if (end == nullptr) break;
        *end = '\0';
        char *line = at;
        at = end + 1;
        char hex[smith_sha256_hex_size];
        bool same = line[0] != '\0' && line[1] == ' ' && strlen(line) > 67 && line[66] == ' ';
        if (!same) {
            ok = false;
        } else if (line[0] == 'f') {
            ok = smith_tc_hash_path(line + 67, hex) && strncmp(hex, line + 2, 64) == 0;
        } else {
            ok = smith_tc_hash_folder(line + 67, hex) && strncmp(hex, line + 2, 64) == 0;
        }
        if (!ok && line[0] != '\0' && strlen(line) > 67)
            snprintf(smith_tc_stale, sizeof smith_tc_stale, "%s", line + 67);
    }
    return ok;
}

// Whether the store can be had at all (the cache folder is known and the store is not off).
static bool smith_tc_store_folder(void) {
    SmithPackageFault fault = {};
    char base[PATH_MAX];
    char path[PATH_MAX];
    char hex[smith_sha256_hex_size];
    smith_tc_address("smith-test-base-last", "", hex);
    return smith_cache_base(base, &fault) && smith_stepcache_answer_file(hex, path);
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
static void smith_tc_load_names_file(const char *path) {
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

static void smith_tc_load_names(const char *folder) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/names", folder);
    smith_tc_load_names_file(path);
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

// Whether `line` (no newline) is one of the lines of `text`.
static bool smith_tc_has_line(const char *text, const char *line, size_t length) {
    for (const char *at = text; *at != '\0';) { // bounded by the text
        const char *end = strchr(at, '\n');
        size_t size = end != nullptr ? (size_t)(end - at) : strlen(at);
        if (size == length && strncmp(at, line, length) == 0) return true;
        if (end == nullptr) break;
        at = end + 1;
    }
    return false;
}

static void smith_tc_name_base(void) {
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_sha256_update(&sha, (const uint8_t *)smith_tc_notes, smith_tc_notes_used);
    smith_sha256_final(&sha, smith_tc_base_name);
}

// Whether `text` (the notes of a stored base) holds the options and the runner of a run with this `macos`; copies them.
static bool smith_tc_base_of(const char *text, int macos, char *options, char *runner) {
    char tail[32];
    snprintf(tail, sizeof tail, " macos=%d", macos);
    const char *at = strstr(text, "options of the run\t");
    const char *place = strstr(text, "runner\t");
    if (at == nullptr || place == nullptr) return false;
    at += strlen("options of the run\t");
    place += strlen("runner\t");
    size_t length = strcspn(at, "\n");
    size_t tail_length = strlen(tail);
    if (length < tail_length || strncmp(at + length - tail_length, tail, tail_length) != 0) return false;
    snprintf(options, sizeof smith_tc_options, "%.*s", (int)length, at);
    snprintf(runner, PATH_MAX, "%.*s", (int)strcspn(place, "\n"), place);
    return true;
}

// The notes of this run's base are also the newest of the runs with its `macos` (--macos-only, --skip-macos or
// neither), for the dealing of `--shard` (#3283): the options and the runner of the last run of its kind.
static void smith_tc_save_base_macos(void) {
    for (int macos = 0; macos < 3; macos++) { // the three kinds
        char options[sizeof smith_tc_options];
        char runner[PATH_MAX];
        char hex[smith_sha256_hex_size];
        char tag[32];
        snprintf(tag, sizeof tag, "%d", macos);
        if (!smith_tc_base_of(smith_tc_notes, macos, options, runner)) continue;
        smith_tc_address("smith-test-base-macos", tag, hex);
        smith_stepcache_answer_put(hex, smith_tc_notes, smith_tc_notes_used);
    }
}

// Saves the notes of this run's base as an answer named by their digest, once, and as the last base stored; the next
// run reads them to name what differs.
static void smith_tc_save_base(void) {
    char last[smith_sha256_hex_size];
    smith_tc_address("smith-test-base-last", "", last);
    if (!smith_tc_stored(smith_tc_base_name))
        smith_stepcache_answer_put(smith_tc_base_name, smith_tc_notes, smith_tc_notes_used);
    smith_stepcache_answer_put(last, smith_tc_notes, smith_tc_notes_used);
    smith_tc_save_base_macos();
}

// The name of a line of notes: what is before the tab.
static size_t smith_tc_name_size(const char *line) {
    const char *tab = strchr(line, '\t');
    return tab != nullptr ? (size_t)(tab - line) : strlen(line);
}

// What differs between the stored base `other` and this run's: `<name> differs` for a line of this run that is not
// there, `<name> is new` and `<name> is gone` when only one side has the name. Written to `out`, at most `max` of them.
static size_t smith_tc_base_diff(const char *other, char out[][160], size_t max) {
    size_t count = 0;
    for (const char *at = smith_tc_notes; *at != '\0' && count < max;) { // bounded by the notes
        const char *end = strchr(at, '\n');
        if (end == nullptr) break;
        size_t length = (size_t)(end - at);
        size_t name = smith_tc_name_size(at);
        if (!smith_tc_has_line(other, at, length)) {
            bool named = false;
            for (const char *o = other; *o != '\0' && !named;) { // bounded by the stored lines
                const char *oend = strchr(o, '\n');
                if (oend == nullptr) break;
                named = smith_tc_name_size(o) == name && strncmp(o, at, name) == 0;
                o = oend + 1;
            }
            snprintf(out[count++], 160, "%.*s %s", (int)name, at, named ? "differs" : "is new");
        }
        at = end + 1;
    }
    for (const char *o = other; *o != '\0' && count < max;) { // a name only the stored run had
        const char *oend = strchr(o, '\n');
        if (oend == nullptr) break;
        size_t name = smith_tc_name_size(o);
        bool kept = false;
        for (const char *at = smith_tc_notes; *at != '\0' && !kept;) { // bounded by the notes
            const char *end = strchr(at, '\n');
            if (end == nullptr) break;
            kept = smith_tc_name_size(at) == name && strncmp(o, at, name) == 0;
            at = end + 1;
        }
        if (!kept) snprintf(out[count++], 160, "%.*s is gone", (int)name, o);
        o = oend + 1;
    }
    return count;
}

// Why a test with no entry missed: what differs from the last base stored. The answer is one line in `reason`; when a
// stored run had the same base the test's own files changed, or it never passed there.
static void smith_tc_explain_base(char *reason, size_t size) {
    static char other[smith_tc_notes_max];
    char last[smith_sha256_hex_size];
    size_t got = 0;
    if (smith_tc_stored(smith_tc_base_name)) {
        snprintf(
            reason, size,
            "no entry, though a stored run had this environment: the test never passed there, or its files changed");
        return;
    }
    smith_tc_address("smith-test-base-last", "", last);
    if (!smith_stepcache_answer_get(last, other, sizeof other - 1, &got)) {
        snprintf(reason, size, "the store holds no run to compare with");
        return;
    }
    other[got] = '\0';
    char diff[4][160];
    size_t count = smith_tc_base_diff(other, diff, 4);
    int used = snprintf(reason, size, "the last stored run differs in");
    for (size_t i = 0; i < count && used > 0 && (size_t)used < size; i++) // bounded by the diff
        used += snprintf(reason + used, size - (size_t)used, "%s %s", i == 0 ? "" : ",", diff[i]);
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

// The digest of the smith under test, kept in the store by build-id (the step cache does the same, executor_cache.c):
// hashing a smith of hundreds of megabytes took 7 s in every run, hits included (#2997). The build-id names the code.
static unsigned smith_tc_self_hashed; // how many times a run hashed the whole smith (a test counts it)

static bool smith_tc_self_digest(const char *self, char hex[smith_sha256_hex_size]) {
    char id[2 * smith_platform_id_bytes + 1];
    char address[smith_sha256_hex_size];
    size_t size = 0;
    if (!smith_platform_file_id(self, id)) {
        smith_tc_self_hashed++;
        return smith_self_digest(self, hex);
    }
    smith_tc_address("smith-test-self", id, address);
    if (smith_stepcache_answer_get(address, hex, smith_sha256_hex_size, &size) && size == smith_sha256_hex_size - 1) {
        hex[size] = '\0';
        return true;
    }
    smith_tc_self_hashed++;
    if (!smith_self_digest(self, hex)) return false;
    smith_stepcache_answer_put(address, hex, smith_sha256_hex_size - 1);
    return true;
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
    if (self != nullptr && self[0] != '\0' && !smith_tc_self_digest(self, smith_tc_self_hex))
        smith_tc_self_hex[0] = '\0';
    smith_tc_load_records();
    smith_tc_make_base();
    smith_tc_index_functions();
    smith_tc_name_base();
    const char *mode = getenv("SMITH_RESULT_CACHE");
    smith_tc_report = smith_tc_why || getenv("CI") != nullptr || (mode != nullptr && strcmp(mode, "report") == 0);
    char base_reason[512] = "";
    size_t shown = 0;
    size_t hits = 0;
    for (size_t i = 0; i < smith_tc_test_count; i++) { // bounded by the tests
        SmithTcTest *test = &smith_tc_tests[i];
        const char *why = "";
        test->cacheable = smith_tc_key(test, &why);
        if (!test->cacheable) smith_tc_say(test->name, why);
        bool stored = test->cacheable && smith_tc_stored(test->key);
        test->hit = test->cacheable && smith_tc_manifest_holds(test->key);
        if (test->hit) hits++;
        if (test->cacheable && !test->hit) smith_tc_say(test->name, "no entry, or a file it read has changed");
        if (!smith_tc_report || !test->cacheable || test->hit || shown >= smith_tc_report_max) continue;
        if (stored) {
            fprintf(stderr, "cache test-store: miss %s: %s changed since the entry was stored\n", test->name,
                    smith_tc_stale[0] != '\0' ? smith_tc_stale : "a file it read");
        } else {
            if (base_reason[0] == '\0') smith_tc_explain_base(base_reason, sizeof base_reason);
            fprintf(stderr, "cache test-store: miss %s: %s\n", test->name, base_reason);
        }
        shown++;
    }
    if (smith_tc_report && smith_tc_test_count > 0)
        fprintf(stderr, "cache test-store: %zu of %zu (%zu%%)\n", hits, smith_tc_test_count,
                hits * 100 / smith_tc_test_count);
    if (smith_tc_report) fprintf(stderr, "cache test-store: smith hashed whole %u times\n", smith_tc_self_hashed);
    smith_tc_save_base();
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

// Stores the manifest of one passed test from the log of what it read: only paths inside the project count.
static void smith_tc_write_entry(const char *key, const char *reads_path) {
    static char manifest[smith_tc_text_max + 1];
    size_t used = (size_t)snprintf(manifest, sizeof manifest, "%s", smith_tc_header);
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
        int added = snprintf(manifest + used, sizeof manifest - used, "%c %s %s\n", folder_read ? 'd' : 'f',
                             ok ? hex : "-", line + root + 1);
        if (added < 0 || (size_t)added >= sizeof manifest - used) {
            if (reads != nullptr) fclose(reads);
            return; // too long to keep whole: no entry, so the test runs again
        }
        used += (size_t)added;
    }
    if (reads != nullptr) fclose(reads);
    smith_stepcache_answer_put(key, manifest, used);
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
    qsort((void *)names, count, sizeof names[0], smith_tc_compare);
    return count;
}

// The record of how long a test took the last time it ran (#3283): an answer in the one store at the address of the
// test's name, holding the CPU seconds. It is by name, not by key, because the test that runs is the one whose key is
// new, and one that is never stored (it failed, or it cannot be cached) has no entry to put a time in.
static void smith_tc_took_address(const char *name, char hex[smith_sha256_hex_size]) {
    smith_tc_address("smith-test-took", name, hex);
}

// The seconds the record of `name` holds; -1 when there is none.
static double smith_tc_took(const char *name) {
    char hex[smith_sha256_hex_size];
    char text[64];
    size_t size = 0;
    smith_tc_took_address(name, hex);
    if (!smith_stepcache_answer_get(hex, text, sizeof text - 1, &size)) return -1;
    text[size] = '\0';
    double seconds = strtod(text, nullptr);
    return seconds >= 0 ? seconds : -1;
}

// `<folder>/times`: `seconds<TAB>name` for each test the run finished. A record is rewritten only when the time moved
// by more than a tenth, so a part adds the tests whose time changed and not all of them.
static void smith_tc_store_times(const char *folder) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/times", folder);
    FILE *times = fopen(path, "r");
    if (times == nullptr) return;
    char row[smith_tc_name_max + 64];
    for (size_t guard = 0; guard < smith_tc_tests_max; guard++) { // bounded by the tests
        if (fgets(row, sizeof row, times) == nullptr) break;
        char *tab = strchr(row, '\t');
        if (tab == nullptr) continue;
        *tab = '\0';
        char *name = tab + 1;
        name[strcspn(name, "\n")] = '\0';
        double seconds = strtod(row, nullptr);
        double before = smith_tc_took(name);
        if (before >= 0 && seconds <= before * 1.1 + 0.01 && seconds >= before * 0.9 - 0.01) continue;
        char hex[smith_sha256_hex_size];
        char text[64];
        smith_tc_took_address(name, hex);
        int size = snprintf(text, sizeof text, "%.3f\n", seconds);
        smith_stepcache_answer_put(hex, text, (size_t)size);
    }
    fclose(times);
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
        if (bsearch((const void *)&name, (const void *)names, count, sizeof names[0], smith_tc_compare) == nullptr)
            continue;
        char clean[smith_tc_name_max];
        smith_tc_clean(name, clean, sizeof clean);
        snprintf(path, sizeof path, "%s/reads/%s.txt", folder, clean);
        smith_tc_write_entry(key, path);
    }
    if (keys != nullptr) fclose(keys);
    smith_tc_store_times(folder);
    return 0;
}

// ---- `smith test --cache weights <names file> <macos>` (#3283)
//
// What each test weighs when `--shard` deals the tests out, as `cpu<TAB>seconds<TAB>name` lines like a --costs file: a
// test the store will serve weighs next to nothing, one that will run weighs the seconds it took last time, and one
// with no record weighs the mean of the others'. It is a function of the commit and the store: the options of the run
// and the runner's place are those of the newest stored run with the same `macos` (the notes of its base, saved with
// it), the keys are the keys the runner will make. The names file holds the tests, one a line. The first line is `#
// summary`.
constexpr double smith_tc_served_weight = 0.02;
constexpr double smith_tc_unknown_weight = 0.4; // when the store knows no time at all, as --shard assumed

// The options and the runner of the newest stored run with this `macos` (saved with the base, `smith_tc_save_base`).
static bool smith_tc_last_run(int macos) {
    char hex[smith_sha256_hex_size];
    char tag[32];
    size_t size = 0;
    snprintf(tag, sizeof tag, "%d", macos);
    smith_tc_address("smith-test-base-macos", tag, hex);
    if (!smith_stepcache_answer_get(hex, smith_tc_text, smith_tc_text_max - 1, &size)) return false;
    smith_tc_text[size] = '\0';
    char options[sizeof smith_tc_options];
    char runner[PATH_MAX];
    if (!smith_tc_base_of(smith_tc_text, macos, options, runner)) return false;
    snprintf(smith_tc_options, sizeof smith_tc_options, "%s", options);
    snprintf(smith_tc_runner, sizeof smith_tc_runner, "%s", runner);
    return true;
}

static void smith_tc_weigh(double *weight, size_t *served) {
    double total = 0;
    size_t known = 0;
    for (size_t i = 0; i < smith_tc_test_count; i++) { // bounded by the tests
        SmithTcTest *test = &smith_tc_tests[i];
        test->hit = test->cacheable && smith_tc_manifest_holds(test->key);
        weight[i] = -1;
        if (test->hit) {
            weight[i] = smith_tc_served_weight;
            (*served)++;
            continue;
        }
        double took = smith_tc_took(test->name);
        if (took < 0) continue;
        weight[i] = took;
        total += took;
        known++;
    }
    double mean = known > 0 ? total / (double)known : smith_tc_unknown_weight;
    for (size_t i = 0; i < smith_tc_test_count; i++)
        if (weight[i] < 0) weight[i] = mean;
}

// A hook for the test of the one deal (tests/test_runner_shard_test.smith): with SMITH_WEIGHS_FLIP=<file>, the file
// counts the asks, and every second ask answers with other weights, as a store that changed between two asks would. A
// build that asks twice and deals twice then deals twice differently. Returns whether it answered.
static bool smith_tc_flip_weights(void) {
    const char *file = getenv("SMITH_WEIGHS_FLIP");
    if (file == nullptr || file[0] == '\0') return false;
    long asked = 0;
    FILE *in = fopen(file, "r");
    if (in != nullptr) {
        char text[32] = "";
        if (fgets(text, sizeof text, in) != nullptr) asked = strtol(text, nullptr, 10);
        fclose(in);
    }
    FILE *out = fopen(file, "w");
    if (out != nullptr) {
        fprintf(out, "%ld\n", asked + 1);
        fclose(out);
    }
    if (asked % 2 == 0) return false;
    printf("# flipped by SMITH_WEIGHS_FLIP\n");
    for (size_t i = 0; i < smith_tc_test_count; i++) // bounded by the tests
        printf("cpu\t%.3f\t%s\n", 1.0 + (double)((i * 7) % 5), smith_tc_tests[i].name);
    return true;
}

static int smith_tc_weights_main(const char *names_file, int macos) {
    static double weight[smith_tc_tests_max];
    char here[PATH_MAX];
    smith_tc_load_names_file(names_file); // the file of the tests, one a line
    if (smith_tc_flip_weights()) return 0;
    if (getcwd(here, sizeof here) == nullptr || !smith_tc_store_folder() || !smith_tc_last_run(macos) ||
        !smith_tc_open_project()) {
        printf("# no stored run to weigh by: every test weighs the same\n");
        return 0;
    }
    char self_path[PATH_MAX];
    const char *self = getenv("SMITH_SELF");
    ssize_t linked =
        (self == nullptr || self[0] == '\0') ? readlink("/proc/self/exe", self_path, sizeof self_path - 1) : -1;
    if (linked > 0) {
        self_path[linked] = '\0';
        self = self_path;
    }
    if (self != nullptr && self[0] != '\0' && !smith_tc_self_digest(self, smith_tc_self_hex))
        smith_tc_self_hex[0] = '\0';
    smith_tc_load_records();
    smith_tc_make_base();
    smith_tc_index_functions();
    for (size_t i = 0; i < smith_tc_test_count; i++) { // bounded by the tests
        const char *why = "";
        smith_tc_tests[i].cacheable = smith_tc_key(&smith_tc_tests[i], &why);
    }
    size_t served = 0;
    smith_tc_weigh(weight, &served);
    if (chdir(here) != 0) return 0;
    printf("# %zu tests, %zu served by the store\n", smith_tc_test_count, served);
    for (size_t i = 0; i < smith_tc_test_count; i++)
        printf("cpu\t%.3f\t%s\n", weight[i], smith_tc_tests[i].name);
    return 0;
}

// `smith test --cache check|store <folder>` (the runner's calls), and `weights <names file> <macos>` (the dealing's).
int smith_tc_main(int count, char **args) {
    if (count == 3 && strcmp(args[0], "weights") == 0)
        return smith_tc_weights_main(args[1], (int)strtol(args[2], nullptr, 10));
    if (count != 2) {
        fputs("usage: smith test --cache check|store <folder>\n", stderr);
        return smith_exit_usage;
    }
    if (strcmp(args[0], "check") == 0) return smith_tc_check(args[1]);
    if (strcmp(args[0], "store") == 0) return smith_tc_store_main(args[1]);
    fputs("usage: smith test --cache check|store <folder>\n", stderr);
    return smith_exit_usage;
}
