// The cache of `smith lint` (issue #621, #3177, knowledge/caches.md): clang-tidy's result for one file, its output text
// and exit status, kept as answers in the one store (runtime/executor_cache.c, `steps/a/`: its prune and cap) and
// replayed when nothing it read has changed. Linked into smith before runtime/verbs.c, which asks it about each file
// and runs clang-tidy only on a miss.
//
// Two levels, as a compiler cache with a manifest does. The FIRST key is the SHA-256 of what is known before the run:
// the lint identity (the LLVM pack and the sources of smith's own checks, hashed by `smith build`; the binary's
// build-id or LC_UUID when smith has none), the environment that moves a compile, the file's name and compile command,
// and the chain of `.clang-tidy` files from its folder up to /. Under it is the MANIFEST of the last run: the files
// clang-tidy read (its own dependency file, written by a miss through `-Xclang -dependency-file`). The SECOND key adds
// the content of those files, and under it is the RESULT: the exit status and the output. A
// .smith file's first key also holds the generated headers of the modules (.smith/include), which its imports read
// without the preprocessor. So a header edited, a file edited, a
// `.clang-tidy` edited or another smith make another key, and a hit needs no build and no object. The project's root is
// replaced by a fixed word in the command and the output, so a clone in another folder shares the entries. A result is
// stored only when the run completed (exit status 0 or 1) and no file it read was changed during or just before the
// run.
#include "lint_cache.h"
#include "core.h"
#include "executor_cache.h"
#include "mcp_unit.h"
#include "package.h"
#include "platform_linux.h"

#include <sys/stat.h>

constexpr size_t smith_lc_files_max = (size_t)1 << 15;
constexpr size_t smith_lc_file_slots = (size_t)1 << 16; // the digests' table (a power of two)
constexpr size_t smith_lc_pool_max = (size_t)4 << 20;   // the names of files
constexpr size_t smith_lc_manifests_max = (size_t)16 << 20;
constexpr size_t smith_lc_jobs_max = (size_t)1 << 14;
constexpr size_t smith_lc_output_max = (size_t)1 << 20;
constexpr size_t smith_lc_depth_max = 256; // folders from a file up to /
constexpr size_t smith_lc_deps_max = (size_t)1 << 14;
constexpr int64_t smith_lc_margin_ns = 20000000; // a file this close to the start of a run may have changed in it
constexpr char smith_lc_root_word[] = "\x01R\x01";
constexpr char smith_lc_header[] = "smith-lint 2\n";

// The digest of one file, made once for the run.
typedef struct {
    const char *path;
    int64_t mtime;
    bool ok;
    char hex[smith_sha256_hex_size];
} SmithLcFile;

// What a file to lint needs: its first key, the manifest of the last run (names, each ended by a 0 byte) and the second
// key those make.
typedef struct {
    bool usable;
    bool keyed; // key is the second key: there was a manifest to read
    char first[smith_sha256_hex_size];
    char key[smith_sha256_hex_size];
    const char *deps;
    uint32_t count;
} SmithLcJob;

typedef struct {
    bool enabled;
    bool explain;  // SMITH_LINT_CACHE=why: say why a file runs
    int64_t began; // the clock of the run, in ns: a file changed after it (less the margin) is not trusted
    char root[PATH_MAX];
    size_t root_length;
    SmithSha256 base; // the parts of every key that do not depend on the file
    bool headers_made;
    bool headers_ok;                     // headers is the digest of the generated headers
    char headers[smith_sha256_hex_size]; // the headers of the modules a .smith file may import
} SmithLc;

static SmithLc smith_lc;
static SmithLcFile smith_lc_files[smith_lc_files_max];
static uint32_t smith_lc_file_table[smith_lc_file_slots];
static size_t smith_lc_file_count;
static char smith_lc_pool[smith_lc_pool_max];
static size_t smith_lc_pool_used;
static char smith_lc_manifests[smith_lc_manifests_max];
static size_t smith_lc_manifests_used;
static SmithLcJob smith_lc_jobs[smith_lc_jobs_max];
static char smith_lc_entry[smith_lc_output_max + 256];
static char smith_lc_scratch[smith_lc_output_max + 256];
static char smith_lc_flags[smith_lc_output_max + 256];

static uint64_t smith_lc_hash(const char *text) {
    uint64_t hash = 14695981039346656037ULL;
    for (const char *c = text; *c != '\0'; c++) { // bounded by the string
        hash ^= (unsigned char)*c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int64_t smith_lc_nanoseconds(const struct stat *info) {
    struct timespec modified = smith_platform_mtime(info);
    return (int64_t)modified.tv_sec * 1000000000 + modified.tv_nsec;
}

static void smith_lc_why(const char *path, const char *reason) {
    if (smith_lc.explain) fprintf(stderr, "smith lint: %s: %s\n", path, reason);
}

// ---- Digests of files

// The digest of the file at `path`, made once for the run; nullptr when the tables are full. ok is false for a file
// that cannot be read.
static const SmithLcFile *smith_lc_file(const char *path) {
    size_t slot = smith_lc_hash(path) & (smith_lc_file_slots - 1);
    for (size_t probe = 0; probe < smith_lc_file_slots; probe++) { // bounded by the table
        uint32_t found = smith_lc_file_table[slot];
        if (found == 0) break;
        if (strcmp(smith_lc_files[found - 1].path, path) == 0) return &smith_lc_files[found - 1];
        slot = (slot + 1) & (smith_lc_file_slots - 1);
    }
    size_t length = strlen(path) + 1;
    if (smith_lc_file_count >= smith_lc_files_max || smith_lc_pool_used + length > smith_lc_pool_max) return nullptr;
    SmithLcFile *entry = &smith_lc_files[smith_lc_file_count++];
    memcpy(smith_lc_pool + smith_lc_pool_used, path, length);
    entry->path = smith_lc_pool + smith_lc_pool_used;
    smith_lc_pool_used += length;
    struct stat info;
    entry->ok = stat(path, &info) == 0 && S_ISREG(info.st_mode) && smith_sha256_file(path, entry->hex);
    entry->mtime = entry->ok ? smith_lc_nanoseconds(&info) : 0;
    smith_lc_file_table[slot] = (uint32_t)smith_lc_file_count;
    return entry;
}

// The path as the key writes it: relative to the root when it is inside it.
static const char *smith_lc_relative(const char *path) {
    while (path[0] == '.' && path[1] == '/')
        path += 2;
    if (strncmp(path, smith_lc.root, smith_lc.root_length) == 0 && path[smith_lc.root_length] == '/')
        return path + smith_lc.root_length + 1;
    return path;
}

static void smith_lc_text_add(SmithSha256 *sha, const char *text) {
    smith_sha256_update(sha, (const uint8_t *)text, strlen(text) + 1);
}

// What smith's lint is, apart from the files and the environment: the lint identity `smith build` hashed (the LLVM pack
// and the sources of smith's own checks, #2236), or nullptr when this smith has none (a build with no LLVM install, a
// test of the core), and the key then holds the binary's own id.
#ifdef SMITH_LINT_IDENTITY_LINKED
extern const char smith_lint_identity[];
#endif
#ifdef SMITH_CORE
static const char
    *smith_lc_core_identity; // the units runner's identity: a test sets what `smith build` would compile in
#endif
static const char *smith_lc_lint_identity(void) {
#ifdef SMITH_CORE
    if (smith_lc_core_identity != nullptr) return smith_lc_core_identity;
#endif
#ifdef SMITH_LINT_IDENTITY_LINKED
    if (smith_lint_identity[0] != '\0') return smith_lint_identity;
#endif
    return nullptr;
}

// Adds a file's name and digest to a key (a file that cannot be read adds "-": no stored key has that).
static void smith_lc_add_file(SmithSha256 *sha, const char *path) {
    const SmithLcFile *file = smith_lc_file(path);
    smith_lc_text_add(sha, smith_lc_relative(path));
    smith_lc_text_add(sha, file != nullptr && file->ok ? file->hex : "-");
}

// ---- The first key

// Leaves the folder of clang's module cache out of a command being keyed: `-fmodules-cache-path=<cache>/modules/<id>`
// names the compiler's build (a smith rebuilt with another smith has another), where the compiled modules are kept,
// not what they say, so a finding cannot move with it (#3062: a rebuilt smith reran every file with a module flag).
static void smith_lc_drop_cache_path(char *text) {
    static const char flag[] = "-fmodules-cache-path=";
    for (char *at = strstr(text, flag); at != nullptr; at = strstr(at, flag)) { // bounded by the text
        char *value = at + sizeof flag - 1;
        size_t length = strcspn(value, "\t\"");
        memmove(value, value + length, strlen(value + length) + 1);
        at = value;
    }
}

// Whether the byte can continue a name: the root of a project is replaced only where its path ends.
static bool smith_lc_name_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' ||
           c == '.';
}

// Copies `text` with the project's root replaced by the root word (to_word) or the word by the root. Returns the
// length, or SIZE_MAX when it does not fit.
static size_t smith_lc_swap(const char *text, size_t size, char *out, size_t capacity, bool to_word) {
    const char *from = to_word ? smith_lc.root : smith_lc_root_word;
    const char *into = to_word ? smith_lc_root_word : smith_lc.root;
    size_t from_length = to_word ? smith_lc.root_length : sizeof smith_lc_root_word - 1;
    size_t into_length = to_word ? sizeof smith_lc_root_word - 1 : smith_lc.root_length;
    size_t used = 0;
    for (size_t at = 0; at < size;) { // bounded by the text
        bool here = size - at >= from_length && memcmp(text + at, from, from_length) == 0 &&
                    (!to_word || at + from_length == size || !smith_lc_name_char(text[at + from_length]));
        size_t piece = here ? into_length : 1;
        if (used + piece >= capacity) return SIZE_MAX;
        memcpy(out + used, here ? into : text + at, piece);
        used += piece;
        at += here ? from_length : 1;
    }
    out[used] = '\0';
    return used;
}

// Whether the text holds the root word already (then the swap could not be undone).
static bool smith_lc_has_word(const char *text, size_t size) {
    constexpr size_t length = sizeof smith_lc_root_word - 1;
    for (size_t at = 0; at + length <= size; at++) // bounded by the text
        if (memcmp(text + at, smith_lc_root_word, length) == 0) return true;
    return false;
}

// The `.clang-tidy` files from the folder of the file up to /, by content: the nearest one governs and the others may
// be inherited.
static bool smith_lc_add_configs(SmithSha256 *sha, const char *file) {
    char folder[PATH_MAX];
    int written = snprintf(folder, sizeof folder, "%s/%s", smith_lc.root, file);
    if (written <= 0 || (size_t)written >= sizeof folder) return false;
    for (size_t depth = 0; depth < smith_lc_depth_max; depth++) { // bounded by the folders
        char *slash = strrchr(folder, '/');
        if (slash == nullptr) break;
        *slash = '\0';
        char config[PATH_MAX];
        written = snprintf(config, sizeof config, "%s/.clang-tidy", folder);
        struct stat info;
        if (written > 0 && (size_t)written < sizeof config && stat(config, &info) == 0) smith_lc_add_file(sha, config);
        if (folder[0] == '\0') break;
    }
    return true;
}

constexpr size_t smith_lc_walk_max = (size_t)1 << 20;
constexpr size_t smith_lc_headers_max = (size_t)1 << 13;
constexpr size_t smith_lc_header_head = (size_t)1 << 14; // the lines of a header's head that name its module
static char smith_lc_walk[smith_lc_walk_max];

// A generated header (or a module's `.body` beside it) and what its head says: the module, the source it was made
// from and the modules it imports (`// smith-imports:`, the list the lowering walks, runtime/lang_names.c). nullptr
// when the head has no such line.
typedef struct {
    const char *path;
    const char *module;
    const char *source;
    const char *imports;
} SmithLcHeader;

static SmithLcHeader smith_lc_header_list[smith_lc_headers_max];
static size_t smith_lc_header_count;
static uint32_t smith_lc_header_seen[smith_lc_headers_max]; // the walk that last took each header
static uint32_t smith_lc_header_walk;
static char smith_lc_header_pool[(size_t)1 << 20];
static size_t smith_lc_header_pool_used;
static char smith_lc_header_text[smith_lc_header_head + 1];

static int smith_lc_compare_headers(const void *a, const void *b) {
    return strcmp(((const SmithLcHeader *)a)->path, ((const SmithLcHeader *)b)->path);
}

// Appends a folder's entries to the queue of the walk; false when the queue is full.
static bool smith_lc_walk_folder(const char *folder, size_t *used) {
    DIR *dir = opendir(folder);
    if (dir == nullptr) return true;
    bool fits = true;
    for (struct dirent *entry = readdir(dir); entry != nullptr && fits; entry = readdir(dir)) { // bounded by the folder
        if (entry->d_name[0] == '.') continue;
        int written = snprintf(smith_lc_walk + *used, smith_lc_walk_max - *used, "%s/%s", folder, entry->d_name);
        fits = written > 0 && (size_t)written + 1 < smith_lc_walk_max - *used;
        if (fits) *used += (size_t)written + 1;
    }
    closedir(dir);
    return fits;
}

// A copy of the rest of the head's line that starts with `marker` (up to `end`, or the line's end), in the pool;
// nullptr when no line starts so, the line is cut or the pool is full.
static const char *smith_lc_head_line(const char *text, const char *marker, const char *end) {
    size_t length = strlen(marker);
    for (const char *line = text; line != nullptr && *line != '\0'; line = strchr(line, '\n')) { // bounded by the head
        if (*line == '\n') line += 1;
        if (strncmp(line, marker, length) != 0) continue;
        const char *start = line + length;
        const char *stop = strchr(start, '\n');
        if (stop == nullptr) return nullptr;
        const char *word = end != nullptr ? strstr(start, end) : nullptr;
        if (word != nullptr && word < stop) stop = word;
        size_t size = (size_t)(stop - start);
        if (smith_lc_header_pool_used + size + 1 > sizeof smith_lc_header_pool) return nullptr;
        char *copy = smith_lc_header_pool + smith_lc_header_pool_used;
        memcpy(copy, start, size);
        copy[size] = '\0';
        smith_lc_header_pool_used += size + 1;
        return copy;
    }
    return nullptr;
}

// Reads what the head of a generated header says into its entry.
static void smith_lc_read_head(SmithLcHeader *header) {
    FILE *file = fopen(header->path, "rb");
    if (file == nullptr) return;
    size_t size = fread(smith_lc_header_text, 1, smith_lc_header_head, file);
    fclose(file);
    smith_lc_header_text[size] = '\0';
    header->module = smith_lc_head_line(smith_lc_header_text, "// smith-module: ", nullptr);
    header->source = smith_lc_head_line(smith_lc_header_text, "// Generated by smith from ", ". Do not edit.");
    header->imports = smith_lc_head_line(smith_lc_header_text, "// smith-imports:", nullptr);
}

static bool smith_lc_ends(const char *text, const char *suffix) {
    size_t length = strlen(text);
    size_t tail = strlen(suffix);
    return length > tail && strcmp(text + length - tail, suffix) == 0;
}

// The generated headers and bodies under .smith/include, which `smith build` writes for the modules, sorted by path,
// and the digest of all of them. Made once.
static bool smith_lc_list_headers(void) {
    if (smith_lc.headers_made) return smith_lc.headers_ok;
    smith_lc.headers_made = true;
    smith_lc_header_count = 0;
    smith_lc_header_pool_used = 0;
    bool fits = snprintf(smith_lc_walk, smith_lc_walk_max, ".smith/include") > 0;
    size_t used = strlen(smith_lc_walk) + 1;
    for (size_t at = 0; at < used && fits;) { // bounded by the queue
        const char *path = smith_lc_walk + at;
        struct stat info;
        if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) {
            fits = smith_lc_walk_folder(path, &used);
        } else if (smith_lc_ends(path, ".h") || smith_lc_ends(path, ".body")) {
            fits = smith_lc_header_count < smith_lc_headers_max;
            if (fits) smith_lc_header_list[smith_lc_header_count++] = (SmithLcHeader){.path = path};
        }
        at += strlen(path) + 1;
    }
    if (fits) {
        qsort(smith_lc_header_list, smith_lc_header_count, sizeof smith_lc_header_list[0], smith_lc_compare_headers);
        SmithSha256 all;
        smith_sha256_init(&all);
        for (size_t i = 0; i < smith_lc_header_count; i++) { // bounded by the headers
            smith_lc_add_file(&all, smith_lc_header_list[i].path);
            if (smith_lc_ends(smith_lc_header_list[i].path, ".h")) smith_lc_read_head(&smith_lc_header_list[i]);
        }
        smith_sha256_final(&all, smith_lc.headers);
    }
    smith_lc.headers_ok = fits;
    return fits;
}

// The entry of a header by its path, or nullptr.
static SmithLcHeader *smith_lc_header_at(const char *path) {
    SmithLcHeader probe = {.path = path};
    return bsearch(&probe, smith_lc_header_list, smith_lc_header_count, sizeof smith_lc_header_list[0],
                   smith_lc_compare_headers);
}

// The header of a module, as the lowering finds it (smith_lang_module_to_path): a::b::c is a/b/c.h, and the standard
// library's std::x is smith/x.h. `suffix` is ".h" or ".body".
static bool smith_lc_module_path(const char *module, size_t length, const char *suffix, char *out, size_t capacity) {
    size_t used = (size_t)snprintf(out, capacity, ".smith/include/%s",
                                   length > 5 && strncmp(module, "std::", 5) == 0 ? "smith/" : "");
    size_t i = length > 5 && strncmp(module, "std::", 5) == 0 ? 5 : 0;
    for (; i < length && used + 8 < capacity; i++) { // bounded by the name
        bool colons = module[i] == ':' && i + 1 < length && module[i + 1] == ':';
        out[used++] = colons ? '/' : module[i];
        i += colons ? 1 : 0;
    }
    return snprintf(out + used, capacity - used, "%s", suffix) < (int)(capacity - used) && i == length;
}

// Takes a header (and its body, when there is one) into the walk once; false when the module has no header here.
static bool smith_lc_take_module(const char *module, size_t length, size_t *queue, size_t *count) {
    char path[PATH_MAX];
    if (!smith_lc_module_path(module, length, ".h", path, sizeof path)) return false;
    SmithLcHeader *header = smith_lc_header_at(path);
    if (header == nullptr || header->imports == nullptr) return false;
    size_t index = (size_t)(header - smith_lc_header_list);
    if (smith_lc_header_seen[index] == smith_lc_header_walk) return true;
    smith_lc_header_seen[index] = smith_lc_header_walk;
    queue[(*count)++] = index;
    if (smith_lc_module_path(module, length, ".body", path, sizeof path)) {
        SmithLcHeader *body = smith_lc_header_at(path);
        if (body != nullptr) smith_lc_header_seen[body - smith_lc_header_list] = smith_lc_header_walk;
    }
    return true;
}

static size_t smith_lc_walk_queue[smith_lc_headers_max];

// The headers a .smith file reads through its imports (clang-tidy's dependency file never names them, as they are read
// without the preprocessor): its own module's header and each one its `// smith-imports:` lead to, transitively, with
// their bodies, each by its name and digest in the order of their paths. False when the file has no header of its own
// or an import has none here: the caller then keys on all of them.
static bool smith_lc_add_import_headers(SmithSha256 *sha, const char *file) {
    const SmithLcHeader *own = nullptr;
    for (size_t i = 0; i < smith_lc_header_count && own == nullptr; i++) // bounded by the headers
        if (smith_lc_header_list[i].source != nullptr && strcmp(smith_lc_header_list[i].source, file) == 0)
            own = &smith_lc_header_list[i];
    if (own == nullptr || own->module == nullptr) return false;
    smith_lc_header_walk += 1;
    size_t count = 0;
    if (!smith_lc_take_module(own->module, strlen(own->module), smith_lc_walk_queue, &count)) return false;
    for (size_t at = 0; at < count; at++) { // each header is taken once
        const char *word = smith_lc_header_list[smith_lc_walk_queue[at]].imports;
        for (size_t guard = 0; *word != '\0' && guard < smith_lc_headers_max; guard++) { // bounded by the list
            while (*word == ' ')
                word += 1;
            size_t length = strcspn(word, " ");
            if (length > 0 && !smith_lc_take_module(word, length, smith_lc_walk_queue, &count)) return false;
            word += length;
        }
    }
    for (size_t i = 0; i < smith_lc_header_count; i++) // in the order of their paths
        if (smith_lc_header_seen[i] == smith_lc_header_walk) smith_lc_add_file(sha, smith_lc_header_list[i].path);
    return true;
}

// A .smith file's generated headers: those of its imports, else the digest of every one (#3062: an export changed in
// one module then reran every .smith file). The reason, for SMITH_LINT_CACHE=why.
static bool smith_lc_add_generated_headers(SmithSha256 *sha, const char *file, bool *all) {
    if (!smith_lc_list_headers()) return false;
    *all = !smith_lc_add_import_headers(sha, file);
    if (*all) smith_lc_text_add(sha, smith_lc.headers);
    return true;
}

// The environment that changes a compile or the tool's answer.
static void smith_lc_add_environment(SmithSha256 *sha) {
    static const char *const names[] = {"SDKROOT",
                                        "DEVELOPER_DIR",
                                        "CPATH",
                                        "C_INCLUDE_PATH",
                                        "CPLUS_INCLUDE_PATH",
                                        "OBJC_INCLUDE_PATH",
                                        "OBJCPLUS_INCLUDE_PATH",
                                        "MACOSX_DEPLOYMENT_TARGET",
                                        "SMITH_CLANG",
                                        "SMITH_CLANG_TIDY",
                                        "CLANG_NO_DEFAULT_CONFIG",
                                        nullptr};
    for (size_t i = 0; names[i] != nullptr; i++) { // bounded by the table
        const char *value = getenv(names[i]);
        smith_lc_text_add(sha, names[i]);
        smith_lc_text_add(sha, value != nullptr ? value : "");
    }
}

// The cache folders and the parts of the key that do not depend on the file. False: nothing is cached.
#ifdef SMITH_CORE
static const char *smith_lc_core_cache; // the units runner's cache folder and SMITH_LINT_CACHE: no setenv in a test
static const char *smith_lc_core_setting;
void smith_core_package_environment(bool given, const char *xdg, const char *home); // package.c
#endif

static bool smith_lc_start(void) {
    const char *setting = getenv("SMITH_LINT_CACHE");
#ifdef SMITH_CORE
    if (smith_lc_core_cache != nullptr) setting = smith_lc_core_setting;
#endif
    if (setting != nullptr && strcmp(setting, "0") == 0) return false;
    smith_lc.explain = setting != nullptr && strcmp(setting, "why") == 0;
    SmithPackageFault error;
    char base[PATH_MAX];
    if (!smith_cache_base(base, &error) || getcwd(smith_lc.root, sizeof smith_lc.root) == nullptr) return false;
    smith_lc.root_length = strlen(smith_lc.root);
    if (smith_lc.root_length < 2) return false; // a root of / would be replaced everywhere
    if (!smith_package_make_folders(base)) return false;
    char identity[2 * smith_platform_id_bytes + 1] = "";
    const char *lint_identity = smith_lc_lint_identity();
    if (lint_identity == nullptr) smith_platform_self_id(identity, sizeof identity);
    smith_sha256_init(&smith_lc.base);
    smith_lc_text_add(&smith_lc.base, smith_lc_header);
    smith_lc_text_add(&smith_lc.base, lint_identity != nullptr ? lint_identity : identity);
    smith_lc_add_environment(&smith_lc.base);
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    smith_lc.began = (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
    return true;
}

// The digest of .smith/compile_commands.json with the root replaced: the command of a file that has no compile of its
// own (a .smith source the build does not compile is linted with the nearest compile's).
static bool smith_lc_add_database(SmithSha256 *sha) {
    FILE *file = fopen(".smith/compile_commands.json", "rb");
    if (file == nullptr) return false;
    size_t size = fread(smith_lc_entry, 1, smith_lc_output_max, file);
    bool whole = feof(file) != 0;
    fclose(file);
    size_t swapped =
        whole ? smith_lc_swap(smith_lc_entry, size, smith_lc_scratch, sizeof smith_lc_scratch, true) : SIZE_MAX;
    if (swapped == SIZE_MAX) return false;
    smith_lc_drop_cache_path(smith_lc_scratch);
    smith_lc_text_add(sha, smith_lc_scratch);
    return true;
}

// ---- The entries

// Reads the manifest under the job's first key into the pool: one name per line, each ended by a 0 byte here.
static void smith_lc_read_manifest(SmithLcJob *job) {
    size_t room = smith_lc_manifests_max - smith_lc_manifests_used;
    char *text = smith_lc_manifests + smith_lc_manifests_used;
    size_t size = 0;
    if (!smith_stepcache_answer_get(job->first, text, room, &size) || size == 0 || text[size - 1] != '\n') return;
    uint32_t count = 0;
    for (size_t i = 0; i < size; i++) { // bounded by the size
        if (text[i] != '\n') continue;
        text[i] = '\0';
        count++;
    }
    smith_lc_manifests_used += size;
    job->deps = text;
    job->count = count;
}

// The second key: the first and the content of what the manifest names.
static void smith_lc_second_key(SmithLcJob *job, char key[smith_sha256_hex_size]) {
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_lc_text_add(&sha, job->first);
    const char *dep = job->deps;
    for (uint32_t i = 0; i < job->count; i++) { // bounded by the manifest
        smith_lc_add_file(&sha, dep);
        dep += strlen(dep) + 1;
    }
    smith_sha256_final(&sha, key);
}

// SMITH_LINT_CACHE=why: the first key by its inputs, each as the first 8 hex digits of its digest, so the key of the
// same file in two runs (a job that saves the cache and one that restores it) can be compared (#2995).
static void smith_lc_say_inputs(const char *file, const char *own_flags, const char *headers) {
    char whole[smith_sha256_hex_size];
    char database[smith_sha256_hex_size] = "-";
    char configs[smith_sha256_hex_size] = "-";
    SmithSha256 copy = smith_lc.base;
    smith_sha256_final(&copy, whole);
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_lc_text_add(&sha, own_flags != nullptr ? own_flags : "");
    smith_sha256_final(&sha, database); // own_flags is the compile of the file, or the whole compile database
    smith_sha256_init(&sha);
    smith_lc_add_configs(&sha, smith_lc_relative(file));
    smith_sha256_final(&sha, configs);
    fprintf(stderr, "smith lint: %s: key inputs: smith+env %.8s, name %.8s, %s %.8s, .clang-tidy %.8s, headers %.8s\n",
            file, whole, smith_lc_relative(file), own_flags != smith_lc_scratch ? "flags" : "database", database,
            configs, headers);
}

// Finds the keys of a file with the flags of its compile (tab separated; nullptr: it has none of its own).
void smith_lc_plan(size_t number, const char *file, const char *flags) {
    SMITH_ASSERT(number < smith_lc_jobs_max);
    SmithLcJob *job = &smith_lc_jobs[number];
    *job = (SmithLcJob){};
    if (!smith_lc.enabled) return;
    SmithSha256 sha = smith_lc.base;
    smith_lc_text_add(&sha, smith_lc_relative(file));
    size_t length =
        flags != nullptr ? smith_lc_swap(flags, strlen(flags), smith_lc_flags, sizeof smith_lc_flags, true) : SIZE_MAX;
    if (flags != nullptr && length != SIZE_MAX) {
        smith_lc_drop_cache_path(smith_lc_flags);
        smith_lc_text_add(&sha, smith_lc_flags);
    } else if (!smith_lc_add_database(&sha)) {
        smith_lc_why(file, "no compile command to key on");
        return;
    }
    if (!smith_lc_add_configs(&sha, smith_lc_relative(file))) return;
    bool smith_source = smith_lc_ends(file, ".smith");
    bool all = false;
    if (smith_source && !smith_lc_add_generated_headers(&sha, smith_lc_relative(file), &all)) {
        smith_lc_why(file, "too many generated headers to key on");
        return;
    }
    smith_sha256_final(&sha, job->first);
    job->usable = true;
    smith_lc_read_manifest(job);
    if (job->deps == nullptr) {
        smith_lc_why(file, "runs: no manifest of an earlier run");
        if (smith_lc.explain)
            smith_lc_say_inputs(file, flags != nullptr && length != SIZE_MAX ? smith_lc_flags : smith_lc_scratch,
                                !smith_source ? "-" : (all ? smith_lc.headers : "imports"));
        return;
    }
    smith_lc_second_key(job, job->key);
    job->keyed = true;
}

static bool smith_lc_parse(const char *text, int *status, unsigned long long *size, int *header);

// Looks for the job's result: true and its exit status when it is there (its output printed on `out`, as the child's
// log would be), false when it is not or is damaged.
static bool smith_lc_replay_to(size_t number, int *status, FILE *out) {
    const SmithLcJob *job = &smith_lc_jobs[number];
    size_t size = 0;
    if (!job->keyed) return false;
    if (!smith_stepcache_answer_get(job->key, smith_lc_entry, sizeof smith_lc_entry - 1, &size)) {
        smith_lc_why(job->first, "runs: no result for the content of the files it read (one of them changed)");
        return false;
    }
    bool whole = true;
    smith_lc_entry[size] = '\0';
    constexpr size_t prefix = sizeof smith_lc_header - 1;
    int code = 0;
    int header = 0;
    unsigned long long length = 0;
    if (!whole || strncmp(smith_lc_entry, smith_lc_header, prefix) != 0 ||
        !smith_lc_parse(smith_lc_entry + prefix, &code, &length, &header) ||
        smith_lc_entry[prefix + (size_t)header] != '\n' || size - prefix - (size_t)header - 1 != length)
        return false;
    const char *body = smith_lc_entry + prefix + (size_t)header + 1;
    size_t swapped = smith_lc_swap(body, length, smith_lc_scratch, sizeof smith_lc_scratch, false);
    if (swapped == SIZE_MAX) return false;
    fwrite(smith_lc_scratch, 1, swapped, out);
    fflush(out);
    *status = code;
    return true;
}

bool smith_lc_replay(size_t number, int *status) {
    return smith_lc_replay_to(number, status, stdout);
}

// ---- Misses

// The dependency file a miss writes: <folder>/t/<pid>-<job>.d.
static bool smith_lc_depfile(size_t number, char *out) {
    const char *temporary = getenv("TMPDIR");
    int written = snprintf(out, PATH_MAX, "%s/smith-lint-%d-%zu.d",
                           temporary != nullptr && temporary[0] == '/' ? temporary : "/tmp", (int)getpid(), number);
    return written > 0 && written < PATH_MAX;
}

// The argument that makes clang-tidy write its dependency file: `-Wp,-MMD,<file>`, which clang-tooling's stripping of
// -MD, -MMD, -MF and -MT leaves alone. It goes to `extra`; returns how many arguments that is (0 for a job that is not
// cached).
size_t smith_lc_extra(size_t number, char **extra) {
    static char argument[PATH_MAX + 16];
    char depfile[PATH_MAX];
    if (!smith_lc_jobs[number].usable || !smith_lc_depfile(number, depfile)) return 0;
    snprintf(argument, sizeof argument, "--extra-arg=-Wp,-MMD,%s", depfile);
    extra[0] = argument;
    return 1;
}

// Reads a make-format dependency file ("x: a b \<newline> c") into the manifest buffer as names, one per line (relative
// to the root when inside it). Returns the length of the text, or 0 if it is damaged, empty or has too many names.
static size_t smith_lc_parse_depfile(const char *path, char *out, size_t capacity) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return 0;
    size_t size = fread(smith_lc_scratch, 1, smith_lc_output_max, file);
    bool whole = feof(file) != 0;
    fclose(file);
    smith_lc_scratch[size] = '\0';
    const char *colon = strstr(smith_lc_scratch, ": ");
    if (!whole || colon == nullptr) return 0;
    size_t used = 0;
    size_t names = 0;
    char name[PATH_MAX];
    size_t length = 0;
    for (const char *c = colon + 1; c <= smith_lc_scratch + size; c++) { // bounded by the text, ended by a 0 byte
        bool end = *c == '\0';
        bool space = *c == ' ' || *c == '\n' || *c == '\t';
        if (*c == '\\' && c[1] == '\n') {
            c++;
            space = true;
        } else if (*c == '\\' && (c[1] == ' ' || c[1] == '#')) {
            c++;
            space = false;
        } else if (*c == '$' && c[1] == '$') {
            c++;
        }
        if (!end && !space) {
            if (length + 1 >= sizeof name) return 0;
            name[length++] = *c;
            continue;
        }
        if (length > 0) {
            name[length] = '\0';
            const char *relative = smith_lc_relative(name);
            size_t need = strlen(relative) + 1;
            if (used + need >= capacity || ++names > smith_lc_deps_max) return 0;
            memcpy(out + used, relative, need - 1);
            out[used + need - 1] = '\n';
            used += need;
            length = 0;
        }
        if (end) break;
    }
    return used;
}

// Whether every file of the manifest (and each `.clang-tidy`, which the first key holds) is older than the run, with a
// margin: a file changed since may have been read in either state.
static bool smith_lc_settled(const char *text, size_t size) {
    for (size_t at = 0; at < size;) { // bounded by the text
        char name[PATH_MAX];
        size_t length = strcspn(text + at, "\n");
        if (length >= sizeof name) return false;
        memcpy(name, text + at, length);
        name[length] = '\0';
        struct stat info;
        if (stat(name, &info) != 0 || smith_lc_nanoseconds(&info) > smith_lc.began - smith_lc_margin_ns) return false;
        const SmithLcFile *file = smith_lc_file(name); // made before the run, or now
        if (file == nullptr || !file->ok || (file->mtime != 0 && file->mtime != smith_lc_nanoseconds(&info)))
            return false;
        at += length + 1;
    }
    return true;
}

// Stores the result of a run that completed (exit status 0 or 1: a crash or a missing tool is not a result), from the
// log the child wrote and the dependency file it left; always removes that file.
void smith_lc_store(size_t number, int wait_status, FILE *log) {
    SmithLcJob *job = &smith_lc_jobs[number];
    char depfile[PATH_MAX];
    if (!job->usable || !smith_lc_depfile(number, depfile)) return;
    static char manifest[smith_lc_output_max];
    size_t manifest_size = smith_lc_parse_depfile(depfile, manifest, sizeof manifest);
    unlink(depfile);
    if (!WIFEXITED(wait_status) || WEXITSTATUS(wait_status) > 1) return;
    if (manifest_size == 0 || !smith_lc_settled(manifest, manifest_size)) {
        smith_lc_why(job->first, "result not stored: no dependency file, or a file changed in the run");
        return;
    }
    fseek(log, 0, SEEK_SET);
    size_t size = fread(smith_lc_entry, 1, smith_lc_output_max + 1, log);
    if (size > smith_lc_output_max || smith_lc_has_word(smith_lc_entry, size)) return;
    size_t swapped = smith_lc_swap(smith_lc_entry, size, smith_lc_scratch, sizeof smith_lc_scratch, true);
    if (swapped == SIZE_MAX) return;
    SmithLcJob fresh = *job;
    fresh.deps = manifest;
    fresh.count = 0;
    for (size_t i = 0; i < manifest_size; i++) { // bounded by the text
        if (manifest[i] != '\n') continue;
        manifest[i] = '\0';
        fresh.count++;
    }
    char key[smith_sha256_hex_size];
    smith_lc_second_key(&fresh, key);
    for (size_t i = 0; i < manifest_size; i++) // back to lines for the file
        if (manifest[i] == '\0') manifest[i] = '\n';
    char head[96];
    snprintf(head, sizeof head, "%sstatus %d\nsize %zu\n", smith_lc_header, WEXITSTATUS(wait_status), swapped);
    static char answer[smith_lc_output_max + 256];
    smith_stepcache_answer_put(job->first, manifest, manifest_size);
    int head_size = snprintf(answer, sizeof answer, "%s", head);
    if (head_size <= 0 || (size_t)head_size + swapped > sizeof answer) return;
    memcpy(answer + head_size, smith_lc_scratch, swapped);
    smith_stepcache_answer_put(key, answer, (size_t)head_size + swapped);
}

// Reads "status <n>\nsize <n>" at the start of the text; header is the length of that.
static bool smith_lc_parse(const char *text, int *status, unsigned long long *size, int *header) {
    char *end = nullptr;
    if (strncmp(text, "status ", 7) != 0) return false;
    long code = strtol(text + 7, &end, 10);
    if (end == text + 7 || strncmp(end, "\nsize ", 6) != 0) return false;
    const char *digits = end + 6;
    unsigned long long length = strtoull(digits, &end, 10);
    if (end == digits || end - text > 1000) return false;
    *status = (int)code;
    *size = length;
    *header = (int)(end - text);
    return true;
}

// Opens the cache for a run of `smith lint`; false (and nothing cached) when it is off (SMITH_LINT_CACHE=0) or cannot
// be used.
bool smith_lc_open(void) {
    smith_lc.enabled = smith_lc_start();
    return smith_lc.enabled;
}

#ifdef SMITH_CORE
#include "smith_core_lint_cache.h"

// Forgets the digests, names and manifests of an earlier run: each run of `smith lint` is a process, so they start
// empty.
static void smith_lc_reset(void) {
    memset(smith_lc_file_table, 0, sizeof smith_lc_file_table);
    smith_lc_file_count = 0;
    smith_lc_pool_used = 0;
    smith_lc_manifests_used = 0;
    smith_lc = (SmithLc){};
}

// The dependency file a miss would have clang-tidy write: the names it read, as `x.o: a b`.
static void smith_lc_fake_depfile(size_t number, const char *reads) {
    char depfile[PATH_MAX];
    if (!smith_lc_depfile(number, depfile)) return;
    FILE *file = fopen(depfile, "wb");
    if (file == nullptr) return;
    fputs("x.o: ", file);
    for (const char *c = reads; *c != '\0'; c++) // bounded by the string
        fputc(*c == '\n' ? ' ' : *c, file);
    fputc('\n', file);
    fclose(file);
}

// One miss: the fake clang-tidy "ran" with its output and status, and the cache stores what it kept.
static void smith_lc_fake_run(size_t number, const SmithCoreLintFile *file, FILE *out) {
    char *extra[1];
    (void)smith_lc_extra(number, extra);
    smith_lc_fake_depfile(number, file->reads);
    FILE *log = tmpfile();
    if (log == nullptr) return;
    fputs(file->output, log);
    smith_lc_store(number, file->status << 8, log);
    fclose(log);
    fputs(file->output, out);
}

// include/smith_core_lint_cache.h: the lint identity of the runs that follow, or nullptr for the binary's own id.
void smith_core_lint_set_identity(const char *identity) {
    smith_lc_core_identity = identity;
}

// include/smith_core_lint_cache.h: the decisions of a `smith lint` run, in process.
bool smith_core_lint_plan(const char *folder, const char *cache, const char *setting, const SmithCoreLintFile *files,
                          size_t count, SmithCoreLintResult *results, char *output, size_t output_size) {
    if (count > smith_lc_jobs_max) return false;
    int home = open(".", O_RDONLY);
    FILE *out = fmemopen(output, output_size, "w");
    if (home < 0 || out == nullptr || chdir(folder) != 0) {
        if (out != nullptr) fclose(out);
        if (home >= 0) close(home);
        return false;
    }
    smith_lc_core_cache = cache;
    smith_lc_core_setting = setting;
    smith_core_package_environment(cache != nullptr, cache, nullptr);
    smith_lc_reset();
    bool cached = smith_lc_open();
    smith_lc.began += (int64_t)2000000000; // the run starts after the files the test wrote, whatever their mtimes
    for (size_t i = 0; i < count; i++) {   // bounded by the files
        int status = 0;
        if (cached) smith_lc_plan(i, files[i].file, files[i].flags);
        bool hit = cached && smith_lc_replay_to(i, &status, out);
        if (!hit) {
            status = files[i].status;
            smith_lc_fake_run(i, &files[i], out);
        }
        results[i] = (SmithCoreLintResult){.ran = !hit, .cached = hit, .status = status};
    }
    fclose(out);
    smith_lc_core_cache = nullptr;
    smith_lc_core_setting = nullptr;
    smith_core_package_environment(false, nullptr, nullptr);
    bool back = fchdir(home) == 0;
    close(home);
    return back;
}
#endif
