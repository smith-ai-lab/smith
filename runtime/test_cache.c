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
//
// VERIFY MODE (#3164, knowledge/test-store.md). Two narrower keys are made beside each test's key: the same key with the
// smith under test replaced by what the test covers (the content of the files its `// smith-covers:` lines name, else of
// the files that generate code), and that key with `build.c` left out of the base. They are not used to skip a test.
// A passed test is stored under them too; a test whose key misses and whose narrower key holds is run all the same, and
// `store` says on stderr how many of those passed and names each that did not: a wrong hit of the narrower key, found
// before the key goes live.
#include "test_cache.h"
#include "changed.h"
#include "core.h"
#include "package.h"
#include "platform_linux.h"
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
constexpr int smith_tc_prune_after = 2678400; // an entry unused for 31 days is removed
constexpr int smith_tc_prune_interval = 604800;
constexpr char smith_tc_header[] = "smith-test-cache 1\n";

typedef struct {
    char name[smith_tc_name_max];
    char key[smith_sha256_hex_size];
    bool cacheable;
    bool hit;
    bool narrow_ok;                                 // the narrower keys exist (the test could be cached)
    char narrow[2][smith_sha256_hex_size];          // the narrower keys (verify mode, #3164)
    bool narrow_hit[2];
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
static char smith_tc_base_narrow_hex[smith_sha256_hex_size]; // the base without build.c, plus the LLVM pack's name
static bool smith_tc_base_quiet;                              // the second pass over the base makes no notes
static bool smith_tc_why; // SMITH_RESULT_CACHE=why: say why a test runs
static char smith_tc_text[smith_tc_text_max + 1];

// What the base of every key holds, as `<name>\t<value>` lines, so a miss can say which input differs (#2876). The base
// file `<store>/base/<digest of these lines>` keeps them for the next run to compare with.
constexpr size_t smith_tc_notes_max = 16384;
constexpr size_t smith_tc_bases_max = 64;
constexpr size_t smith_tc_report_max = 5; // miss lines a run prints
constexpr size_t smith_tc_value_max = 120;
static char smith_tc_notes[smith_tc_notes_max];
static size_t smith_tc_notes_used;
static char smith_tc_base_name[smith_sha256_hex_size];
static char smith_tc_stale[PATH_MAX]; // the first file of a manifest that is not as it was
static bool smith_tc_report;          // CI or SMITH_RESULT_CACHE=why: print the hit rate and the first misses

static void smith_tc_note(const char *name, const char *value) {
    if (smith_tc_base_quiet) return;
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
        if (smith_tc_why && !smith_tc_base_quiet) fprintf(stderr, "smith test cache: the key holds %s\n", kept[i]);
    }
}

// Feeds the content of the project's build files (the flags a test is compiled with) and of the cwd's.
static void smith_tc_feed_build_files(SmithSha256 *sha, bool with_build_c) {
    static const char *const files[] = {"build.c", "build.smith", ".clang-format", nullptr};
    char hex[smith_sha256_hex_size];
    for (size_t i = 0; files[i] != nullptr; i++) { // a short fixed list
        if (!with_build_c && strcmp(files[i], "build.c") == 0) continue;
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
        if (!with_build_c && strcmp(files[i], "build.c") == 0) continue;
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
static void smith_tc_base_hash(bool with_build_c, char hex[smith_sha256_hex_size]) {
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_tc_feed(&sha, smith_tc_header);
    smith_tc_feed(&sha, smith_tc_options);
    smith_tc_note("options of the run", smith_tc_options);
    smith_tc_feed(&sha, smith_tc_runner);
    smith_tc_note("runner", smith_tc_runner);
    smith_tc_feed_environment(&sha);
    smith_tc_feed_build_files(&sha, with_build_c);
    smith_tc_note("smith under test (SMITH_SELF)", smith_tc_self_hex);
    smith_sha256_final(&sha, hex);
}

// The default name of the LLVM pack, which `build.c` holds (`smith_env(b, "SMITH_LLVM_NAME", "<name>")`): the one thing
// of `build.c` that decides what the code generator is, kept in the base that leaves `build.c` out (#3164).
static void smith_tc_llvm_name(char *out, size_t size) {
    static const char marker[] = "\"SMITH_LLVM_NAME\", \"";
    out[0] = '\0';
    FILE *file = fopen("build.c", "r");
    char line[512];
    for (size_t guard = 0; file != nullptr && guard < ((size_t)1 << 20); guard++) { // bounded by the lines of build.c
        if (fgets(line, sizeof line, file) == nullptr) break;
        char *at = strstr(line, marker);
        if (at == nullptr) continue;
        at += sizeof marker - 1;
        snprintf(out, size, "%.*s", (int)strcspn(at, "\""), at);
        break;
    }
    if (file != nullptr) fclose(file);
}

// What every key holds: the options and the place of the runner, the environment and the build files. The second base
// is the first without `build.c` and with the pack's name (verify mode, #3164).
static void smith_tc_make_base(void) {
    smith_tc_notes_used = 0;
    smith_tc_notes[0] = '\0';
    smith_tc_base_quiet = false;
    smith_tc_base_hash(true, smith_tc_base_hex);
    smith_tc_base_quiet = true;
    smith_tc_base_hash(false, smith_tc_base_narrow_hex);
    smith_tc_base_quiet = false;
    char name[64];
    smith_tc_llvm_name(name, sizeof name);
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_tc_feed(&sha, smith_tc_base_narrow_hex);
    smith_tc_feed(&sha, name);
    smith_sha256_final(&sha, smith_tc_base_narrow_hex);
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
static void smith_tc_feed_closure(SmithSha256 *shas, size_t hashes, bool *runs_smith, bool *uncached) {
    for (size_t f = 0; f < smith_changed_file_count; f++) { // bounded by the files
        if (!smith_tc_seen[f]) continue;
        for (size_t h = 0; h < hashes; h++) { // the key and the narrower keys
            smith_tc_feed(&shas[h], smith_changed_files[f]);
            smith_tc_feed(&shas[h], smith_tc_file_digest(f));
        }
        bool own = smith_tc_test_file(smith_changed_files[f]) || smith_changed_starts(smith_changed_files[f], "tests/");
        if (!own || !smith_changed_is_source(smith_changed_files[f])) continue;
        size_t size = smith_changed_read(smith_changed_files[f]);
        if (size == 0) continue;
        if (smith_changed_runs_smith(smith_changed_text)) *runs_smith = true;
        if (smith_tc_nondeterministic(smith_changed_text)) *uncached = true;
    }
}

// ---- The narrower keys (verify mode, #3164)

// The files that generate code, for a test that declares no `// smith-covers:` (#3079, section 7). Wider than the
// compiler's own list on purpose: a file too many costs a miss, a file too few a wrong hit.
static const char *const smith_tc_codegen[] = {
    "shim/hook.cpp", "shim/clang.cpp", "shim/cimport*.cpp", "shim/affine.cpp", "shim/effects*.cpp", "shim/lld.cpp",
    "shim/*.h",      "runtime/lang*.c", "runtime/lang*.h",  "runtime/cc*.c",    "runtime/cc*.h",     "front/",
    "lib/std/*.smith", "include/smith/", "patches/",       "templates/",       nullptr};

static char smith_tc_codegen_hex[smith_sha256_hex_size];
static bool smith_tc_codegen_made;
static char smith_tc_cover_hex[smith_changed_files_max][smith_sha256_hex_size];
static uint8_t smith_tc_cover_state[smith_changed_files_max]; // 0 not read yet, 1 declares nothing, 2 declares

// Whether `path` is one of the files that generate code.
static bool smith_tc_is_codegen(const char *path) {
    if (smith_changed_ends(path, "_test.smith")) return false;
    for (size_t i = 0; smith_tc_codegen[i] != nullptr; i++) { // a short fixed list
        if (smith_changed_glob_match(smith_tc_codegen[i], path)) return true;
    }
    return false;
}

static const char *smith_tc_codegen_digest(void) {
    if (!smith_tc_codegen_made) {
        smith_tc_codegen_made = true;
        SmithSha256 sha;
        smith_sha256_init(&sha);
        for (size_t f = 0; f < smith_changed_file_count; f++) { // bounded by the files
            if (!smith_tc_is_codegen(smith_changed_files[f])) continue;
            smith_tc_feed(&sha, smith_changed_files[f]);
            smith_tc_feed(&sha, smith_tc_file_digest(f));
        }
        smith_sha256_final(&sha, smith_tc_codegen_hex);
    }
    return smith_tc_codegen_hex;
}

// The digest of the files test file `file` declares it covers (`// smith-covers:`), or nullptr when it declares none.
static const char *smith_tc_cover_digest(size_t file) {
    if (smith_tc_cover_state[file] != 0) return smith_tc_cover_state[file] == 2 ? smith_tc_cover_hex[file] : nullptr;
    smith_tc_cover_state[file] = 1;
    size_t size = smith_changed_read(smith_changed_files[file]);
    bool marked = false;
    if (size > 0) (void)smith_changed_covers_path(smith_changed_text, "", &marked);
    if (!marked) return nullptr;
    SmithSha256 sha;
    smith_sha256_init(&sha);
    for (size_t f = 0; f < smith_changed_file_count; f++) { // bounded by the files
        bool ignored = false;
        if (!smith_changed_covers_path(smith_changed_text, smith_changed_files[f], &ignored)) continue;
        smith_tc_feed(&sha, smith_changed_files[f]);
        smith_tc_feed(&sha, smith_tc_file_digest(f));
    }
    smith_sha256_final(&sha, smith_tc_cover_hex[file]);
    smith_tc_cover_state[file] = 2;
    return smith_tc_cover_hex[file];
}

// What the narrower keys hold for the smith under test: the files its test sources cover when they declare it, else
// the files that generate code.
static void smith_tc_narrow_self(const uint32_t *sources, size_t found, SmithSha256 *shas, size_t hashes) {
    bool declared = false;
    for (size_t i = 0; i < found; i++) { // bounded by the files
        const char *covers = smith_tc_cover_digest(sources[i]);
        if (covers == nullptr) continue;
        declared = true;
        for (size_t h = 0; h < hashes; h++)
            smith_tc_feed(&shas[h], covers);
    }
    if (declared) return;
    for (size_t h = 0; h < hashes; h++)
        smith_tc_feed(&shas[h], smith_tc_codegen_digest());
}

// The key of one test, or false when it cannot be cached (why says which). Fills the narrower keys too.
static bool smith_tc_key(SmithTcTest *test, const char **why) {
    uint32_t sources[8];
    size_t found = smith_tc_sources(test->name, sources, 8);
    SmithSha256 sha[3]; // the key, the narrower key, the narrower key without build.c
    for (size_t h = 0; h < 3; h++) {
        smith_sha256_init(&sha[h]);
        smith_tc_feed(&sha[h], h == 2 ? smith_tc_base_narrow_hex : smith_tc_base_hex);
        smith_tc_feed(&sha[h], test->name);
    }
    bool runs_smith = false;
    bool uncached = false;
    for (size_t i = 0; i < found; i++) { // bounded by the files
        smith_tc_closure(sources[i]);
        smith_tc_feed_closure(sha, 3, &runs_smith, &uncached);
    }
    if (found == 0) *why = "no source file for its module or its function";
    if (uncached) *why = "its source reads the clock, the network, a file without files::, or asks to be uncached";
    if (found == 0 || uncached) return false;
    if (runs_smith) {
        if (smith_tc_self_hex[0] == '\0') {
            *why = "it runs smith and the smith under test (SMITH_SELF) cannot be read";
            return false;
        }
        smith_tc_feed(&sha[0], smith_tc_self_hex);
        smith_tc_narrow_self(sources, found, &sha[1], 2);
    }
    smith_sha256_final(&sha[0], test->key);
    smith_sha256_final(&sha[1], test->narrow[0]);
    smith_sha256_final(&sha[2], test->narrow[1]);
    test->narrow_ok = true;
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
    smith_tc_stale[0] = '\0';
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
        if (!ok && line[0] != '\0' && strlen(line) > 67)
            snprintf(smith_tc_stale, sizeof smith_tc_stale, "%s", line + 67);
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

// Saves the notes of this run's base as `<store>/base/<digest>`, once; the next run reads them to name what differs.
static void smith_tc_save_base(void) {
    char folder[PATH_MAX];
    char path[PATH_MAX + 80];
    snprintf(folder, sizeof folder, "%s/base", smith_tc_store);
    snprintf(path, sizeof path, "%s/%s", folder, smith_tc_base_name);
    struct stat info;
    if (stat(path, &info) == 0 || !smith_tc_ensure_folder(folder)) return;
    FILE *out = fopen(path, "w");
    if (out == nullptr) return;
    fwrite(smith_tc_notes, 1, smith_tc_notes_used, out);
    fclose(out);
}

// How many lines of the stored text `other` this run's notes share.
static size_t smith_tc_shared(const char *other) {
    size_t shared = 0;
    for (const char *at = other; *at != '\0';) { // bounded by the text
        const char *end = strchr(at, '\n');
        if (end == nullptr) break;
        if (smith_tc_has_line(smith_tc_notes, at, (size_t)(end - at))) shared++;
        at = end + 1;
    }
    return shared;
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

// Why a test with no entry missed: the stored base that shares the most lines with this run's, and what differs. The
// answer is one line in `reason`; `exact` is true when a stored run had the same base (so the test's own files
// changed).
static void smith_tc_explain_base(char *reason, size_t size) {
    static char best[smith_tc_notes_max];
    char folder[PATH_MAX];
    snprintf(folder, sizeof folder, "%s/base", smith_tc_store);
    DIR *dir = opendir(folder);
    size_t best_shared = 0;
    bool found = false;
    for (size_t guard = 0; dir != nullptr && guard < smith_tc_bases_max; guard++) { // bounded by the bases kept
        struct dirent *entry = readdir(dir);
        if (entry == nullptr) break;
        if (entry->d_name[0] == '.') continue;
        if (strcmp(entry->d_name, smith_tc_base_name) == 0) {
            snprintf(reason, size,
                     "no entry, though a stored run had this environment: the test never passed there, or its files "
                     "changed");
            closedir(dir);
            return;
        }
        static char other[smith_tc_notes_max];
        char path[PATH_MAX + 300];
        snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
        FILE *file = fopen(path, "r");
        if (file == nullptr) continue;
        size_t got = fread(other, 1, sizeof other - 1, file);
        fclose(file);
        other[got] = '\0';
        size_t shared = smith_tc_shared(other);
        if (!found || shared > best_shared) {
            found = true;
            best_shared = shared;
            memcpy(best, other, got + 1);
        }
    }
    if (dir != nullptr) closedir(dir);
    if (!found) {
        snprintf(reason, size, "the store holds no run to compare with");
        return;
    }
    char diff[4][160];
    size_t count = smith_tc_base_diff(best, diff, 4);
    int used = snprintf(reason, size, "the nearest stored run differs in");
    for (size_t i = 0; i < count && used > 0 && (size_t)used < size; i++) // bounded by the diff
        used += snprintf(reason + used, size - (size_t)used, "%s %s", i == 0 ? "" : ",", diff[i]);
}

static void smith_tc_write_results(const char *folder) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/hits", folder);
    FILE *hits = fopen(path, "w");
    snprintf(path, sizeof path, "%s/keys", folder);
    FILE *keys = fopen(path, "w");
    snprintf(path, sizeof path, "%s/narrow", folder);
    FILE *narrow = fopen(path, "w"); // `name<TAB>key<TAB>key`: the narrower keys (verify mode, #3164)
    snprintf(path, sizeof path, "%s/verify", folder);
    FILE *verify = fopen(path, "w"); // `name<TAB>keys`: the tests that run and that a narrower key would have served
    for (size_t i = 0; hits != nullptr && keys != nullptr && i < smith_tc_test_count; i++) { // bounded by the tests
        const SmithTcTest *test = &smith_tc_tests[i];
        if (test->hit) fprintf(hits, "%s\n", test->name);
        if (test->cacheable) fprintf(keys, "%s\t%s\n", test->name, test->key);
        if (test->cacheable && narrow != nullptr)
            fprintf(narrow, "%s\t%s\t%s\n", test->name, test->narrow[0], test->narrow[1]);
        if (!test->hit && (test->narrow_hit[0] || test->narrow_hit[1]) && verify != nullptr)
            fprintf(verify, "%s\t%s\n", test->name,
                    test->narrow_hit[0] ? (test->narrow_hit[1] ? "1,2" : "1") : "2");
    }
    if (hits != nullptr) fclose(hits);
    if (keys != nullptr) fclose(keys);
    if (narrow != nullptr) fclose(narrow);
    if (verify != nullptr) fclose(verify);
}

// The digest of the smith under test, kept in the store by build-id (the step cache does the same, executor_cache.c):
// hashing a smith of hundreds of megabytes took 7 s in every run, hits included (#2997). The build-id names the code.
static unsigned smith_tc_self_hashed; // how many times a run hashed the whole smith (a test counts it)

static bool smith_tc_self_digest(const char *self, char hex[smith_sha256_hex_size]) {
    char id[2 * smith_platform_id_bytes + 1];
    char path[PATH_MAX];
    if (!smith_platform_file_id(self, id) ||
        snprintf(path, sizeof path, "%s/self/%s", smith_tc_store, id) >= (int)sizeof path) {
        smith_tc_self_hashed++;
        return smith_self_digest(self, hex);
    }
    FILE *in = fopen(path, "r");
    if (in != nullptr) {
        size_t size = fread(hex, 1, smith_sha256_hex_size - 1, in);
        bool whole = size == smith_sha256_hex_size - 1 && fgetc(in) == EOF;
        fclose(in);
        hex[whole ? size : 0] = '\0';
        if (whole) return true;
    }
    smith_tc_self_hashed++;
    if (!smith_self_digest(self, hex)) return false;
    char temporary[PATH_MAX + 32];
    snprintf(temporary, sizeof temporary, "%s.%d", path, (int)getpid());
    char folder[PATH_MAX];
    snprintf(folder, sizeof folder, "%s/self", smith_tc_store);
    FILE *out = smith_tc_ensure_folder(folder) ? fopen(temporary, "w") : nullptr;
    if (out == nullptr) return true;
    bool written = fwrite(hex, 1, smith_sha256_hex_size - 1, out) == smith_sha256_hex_size - 1;
    if (fclose(out) != 0 || !written || rename(temporary, path) != 0) unlink(temporary);
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
    smith_tc_make_base();
    smith_tc_index_functions();
    smith_tc_name_base();
    const char *mode = getenv("SMITH_RESULT_CACHE");
    smith_tc_report = smith_tc_why || getenv("CI") != nullptr || (mode != nullptr && strcmp(mode, "report") == 0);
    char base_reason[512] = "";
    size_t shown = 0;
    size_t hits = 0;
    size_t narrow_hits[2] = {0, 0};
    size_t would[2] = {0, 0};
    for (size_t i = 0; i < smith_tc_test_count; i++) { // bounded by the tests
        SmithTcTest *test = &smith_tc_tests[i];
        const char *why = "";
        test->cacheable = smith_tc_key(test, &why);
        if (!test->cacheable) smith_tc_say(test->name, why);
        char entry[PATH_MAX];
        smith_tc_entry_path(test->key, entry, sizeof entry);
        struct stat info;
        bool stored = test->cacheable && stat(entry, &info) == 0;
        test->hit = test->cacheable && smith_tc_manifest_holds(test->key);
        if (test->hit) hits++;
        for (size_t k = 0; k < 2; k++) { // bounded: the two narrower keys (verify mode, #3164)
            test->narrow_hit[k] = test->cacheable && smith_tc_manifest_holds(test->narrow[k]);
            if (test->narrow_hit[k]) narrow_hits[k]++;
            if (test->narrow_hit[k] && !test->hit) would[k]++;
        }
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
    if (smith_tc_report && smith_tc_test_count > 0)
        fprintf(stderr,
                "cache test-store: verify: the narrower key would serve %zu of %zu (%zu%%), without build.c %zu of %zu "
                "(%zu%%); %zu and %zu of them are run to be compared\n",
                narrow_hits[0], smith_tc_test_count, narrow_hits[0] * 100 / smith_tc_test_count, narrow_hits[1],
                smith_tc_test_count, narrow_hits[1] * 100 / smith_tc_test_count, would[0], would[1]);
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

// The names in `<folder>/<list>` (`passed` or `ran`), sorted, in `pool`; their count.
static size_t smith_tc_load_list(const char *folder, const char *list, char *pool, size_t pool_size,
                                 const char **names) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", folder, list);
    FILE *file = fopen(path, "r");
    if (file == nullptr) return 0;
    size_t used = 0;
    size_t count = 0;
    char name[smith_tc_name_max];
    for (size_t guard = 0; guard < smith_tc_tests_max; guard++) { // bounded by the passed tests
        if (fgets(name, sizeof name, file) == nullptr) break;
        size_t length = strcspn(name, "\n");
        if (length == 0 || used + length + 1 > pool_size) continue;
        memcpy(pool + used, name, length);
        pool[used + length] = '\0';
        names[count++] = pool + used;
        used += length + 1;
    }
    fclose(file);
    qsort((void *)names, count, sizeof names[0], smith_tc_compare);
    return count;
}

static size_t smith_tc_load_passed(const char *folder, const char **names) {
    static char pool[(size_t)1 << 21];
    return smith_tc_load_list(folder, "passed", pool, sizeof pool, names);
}

// Verify mode (#3164): the tests that ran although a narrower key held (`<folder>/verify`), counted by whether they
// passed. One that ran and did not pass is a wrong hit of that key, and is named on stderr, loudly. Only the run's
// report decides; nothing is stored or skipped by it.
static void smith_tc_verify(const char *folder, const char **passed, size_t passed_count) {
    static char ran_pool[(size_t)1 << 21];
    static const char *ran[smith_tc_tests_max];
    size_t ran_count = smith_tc_load_list(folder, "ran", ran_pool, sizeof ran_pool, ran);
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/verify", folder);
    FILE *file = fopen(path, "r");
    if (file == nullptr) return;
    size_t compared = 0;
    size_t agreed = 0;
    char row[smith_tc_name_max + 16];
    for (size_t guard = 0; guard < smith_tc_tests_max; guard++) { // bounded by the tests
        if (fgets(row, sizeof row, file) == nullptr) break;
        char *tab = strchr(row, '\t');
        if (tab == nullptr) continue;
        *tab = '\0';
        tab[1 + strcspn(tab + 1, "\n")] = '\0';
        const char *name = row;
        if (bsearch((const void *)&name, (const void *)ran, ran_count, sizeof ran[0], smith_tc_compare) == nullptr)
            continue;
        compared++;
        if (bsearch((const void *)&name, (const void *)passed, passed_count, sizeof passed[0], smith_tc_compare) !=
            nullptr) {
            agreed++;
            continue;
        }
        fprintf(stderr,
                "cache test-store: VERIFY DIFFERENCE %s: the narrower key %s held, so it would have been served, and it "
                "did not pass\n",
                name, tab + 1);
    }
    fclose(file);
    fprintf(stderr, "cache test-store: verify: %zu tests the narrower keys would have served ran, %zu passed, %zu differ\n",
            compared, agreed, compared - agreed);
}

// `smith test --cache store <folder>`: for the tests of `<folder>/passed` (names), each with its key in `keys`, writes
// the manifest from `<folder>/reads/<clean name>.txt`.
static int smith_tc_store_main(const char *folder) {
    static const char *names[smith_tc_tests_max];
    char path[PATH_MAX];
    if (!smith_tc_store_folder() || !smith_tc_load_options(folder) || !smith_tc_open_project()) return 0;
    size_t count = smith_tc_load_passed(folder, names);
    smith_tc_verify(folder, names, count);
    snprintf(path, sizeof path, "%s/narrow", folder);
    FILE *narrow = fopen(path, "r");
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
        char other[smith_tc_name_max + 2 * smith_sha256_hex_size + 4]; // the narrower keys of the same test
        while (narrow != nullptr && fgets(other, sizeof other, narrow) != nullptr) { // the same order as `keys`
            char *first = strchr(other, '\t');
            if (first == nullptr || strncmp(other, name, (size_t)(first - other)) != 0 || name[first - other] != '\0')
                continue;
            char *second = strchr(first + 1, '\t');
            if (second == nullptr) break;
            *second = '\0';
            second[1 + strcspn(second + 1, "\n")] = '\0';
            smith_tc_write_entry(first + 1, path);
            smith_tc_write_entry(second + 1, path);
            break;
        }
    }
    if (narrow != nullptr) fclose(narrow);
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
