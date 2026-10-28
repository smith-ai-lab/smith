// The cache of `smith lint` (issue #621, knowledge/caches.md): clang-tidy's result for one file, its output text and
// exit status, kept under $XDG_CACHE_HOME/smith/lint and replayed when nothing it read has changed. Included by smith.c
// before runtime/verbs.c, which asks it about each file and runs clang-tidy only on a miss.
//
// Two levels, as a compiler cache with a manifest does. The FIRST key is the SHA-256 of what is known before the run:
// smith itself (build-id or LC_UUID: clang-tidy and smith's own checks are in the binary), the environment that moves
// a compile, the file's name and compile command, and the chain of `.clang-tidy` files from its folder up to /. Under
// it, `m/<xx>/<key>` is the MANIFEST of the last run: the files clang-tidy read (its own dependency file, written by a
// miss through `-Xclang -dependency-file`). The SECOND key adds the content of those files, and `r/<xx>/<key>` holds
// the RESULT: the exit status and the output. So a header edited, a file edited, a `.clang-tidy` edited or another
// smith make another key, and a hit needs no build and no object. The project's root is replaced by a fixed word in the
// command and the output, so a clone in another folder shares the entries. A result is stored only when the run
// completed (exit status 0 or 1) and no file it read was changed during or just before the run.
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
constexpr time_t smith_lc_touch_after = 86400;
constexpr int smith_lc_temp_after = 86400;
constexpr char smith_lc_root_word[] = "\x01R\x01";
constexpr char smith_lc_header[] = "smith-lint-cache 1\n";

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
    char folder[PATH_MAX];
    SmithSha256 base; // the parts of every key that do not depend on the file
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

// Adds a file's name and digest to a key (a file that cannot be read adds "-": no stored key has that).
static void smith_lc_add_file(SmithSha256 *sha, const char *path) {
    const SmithLcFile *file = smith_lc_file(path);
    smith_lc_text_add(sha, smith_lc_relative(path));
    smith_lc_text_add(sha, file != nullptr && file->ok ? file->hex : "-");
}

// ---- The first key

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

static bool smith_lc_make_folder(const char *path) {
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

// The cache folders and the parts of the key that do not depend on the file. False: nothing is cached.
static bool smith_lc_start(void) {
    const char *setting = getenv("SMITH_LINT_CACHE");
    if (setting != nullptr && strcmp(setting, "0") == 0) return false;
    smith_lc.explain = setting != nullptr && strcmp(setting, "why") == 0;
    SmithPackageFault error;
    char base[PATH_MAX];
    if (!smith_cache_base(base, &error) || getcwd(smith_lc.root, sizeof smith_lc.root) == nullptr) return false;
    int written = snprintf(smith_lc.folder, sizeof smith_lc.folder, "%s/lint", base);
    if (written <= 0 || (size_t)written >= sizeof smith_lc.folder) return false;
    smith_lc.root_length = strlen(smith_lc.root);
    if (smith_lc.root_length < 2) return false; // a root of / would be replaced everywhere
    char temporary[PATH_MAX];
    if (!smith_package_make_folders(base) || !smith_lc_make_folder(smith_lc.folder) ||
        !smith_cache_join(temporary, smith_lc.folder, "t", "") || !smith_lc_make_folder(temporary))
        return false;
    char identity[2 * smith_platform_id_bytes + 1] = "";
    smith_platform_self_id(identity, sizeof identity);
    smith_sha256_init(&smith_lc.base);
    smith_lc_text_add(&smith_lc.base, smith_lc_header);
    smith_lc_text_add(&smith_lc.base, identity);
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
    smith_lc_text_add(sha, smith_lc_scratch);
    return true;
}

// ---- The entries

static bool smith_lc_entry_path(char *out, char kind, const char *key, bool folder_only) {
    int written = folder_only ? snprintf(out, PATH_MAX, "%s/%c/%.2s", smith_lc.folder, kind, key)
                              : snprintf(out, PATH_MAX, "%s/%c/%.2s/%s", smith_lc.folder, kind, key, key + 2);
    return written > 0 && written < PATH_MAX;
}

static void smith_lc_refresh(const char *path) {
    struct stat info;
    if (stat(path, &info) == 0 && time(nullptr) - info.st_mtime > smith_lc_touch_after)
        utimensat(AT_FDCWD, path, nullptr, 0);
}

// Reads the manifest under the job's first key into the pool: one name per line, each ended by a 0 byte here.
static void smith_lc_read_manifest(SmithLcJob *job) {
    char path[PATH_MAX];
    if (!smith_lc_entry_path(path, 'm', job->first, false)) return;
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return;
    size_t room = smith_lc_manifests_max - smith_lc_manifests_used;
    char *text = smith_lc_manifests + smith_lc_manifests_used;
    size_t size = fread(text, 1, room, file);
    bool whole = feof(file) != 0;
    fclose(file);
    if (!whole || size == 0 || text[size - 1] != '\n') return;
    uint32_t count = 0;
    for (size_t i = 0; i < size; i++) { // bounded by the size
        if (text[i] != '\n') continue;
        text[i] = '\0';
        count++;
    }
    smith_lc_manifests_used += size;
    job->deps = text;
    job->count = count;
    smith_lc_refresh(path);
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

// Finds the keys of a file with the flags of its compile (tab separated; nullptr: it has none of its own).
static void smith_lc_plan(size_t number, const char *file, const char *flags) {
    SMITH_ASSERT(number < smith_lc_jobs_max);
    SmithLcJob *job = &smith_lc_jobs[number];
    *job = (SmithLcJob){};
    if (!smith_lc.enabled) return;
    SmithSha256 sha = smith_lc.base;
    smith_lc_text_add(&sha, smith_lc_relative(file));
    size_t length =
        flags != nullptr ? smith_lc_swap(flags, strlen(flags), smith_lc_flags, sizeof smith_lc_flags, true) : SIZE_MAX;
    if (flags != nullptr && length != SIZE_MAX) {
        smith_lc_text_add(&sha, smith_lc_flags);
    } else if (!smith_lc_add_database(&sha)) {
        smith_lc_why(file, "no compile command to key on");
        return;
    }
    if (!smith_lc_add_configs(&sha, smith_lc_relative(file))) return;
    smith_sha256_final(&sha, job->first);
    job->usable = true;
    smith_lc_read_manifest(job);
    if (job->deps == nullptr) {
        smith_lc_why(file, "runs: no manifest of an earlier run");
        return;
    }
    smith_lc_second_key(job, job->key);
    job->keyed = true;
}

static bool smith_lc_parse(const char *text, int *status, unsigned long long *size, int *header);

// Looks for the job's result: true and its exit status when it is there (its output printed on standard output, as
// the child's log would be), false when it is not or is damaged.
static bool smith_lc_replay(size_t number, int *status) {
    const SmithLcJob *job = &smith_lc_jobs[number];
    char path[PATH_MAX];
    if (!job->keyed || !smith_lc_entry_path(path, 'r', job->key, false)) return false;
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return false;
    size_t size = fread(smith_lc_entry, 1, sizeof smith_lc_entry - 1, file);
    bool whole = feof(file) != 0;
    fclose(file);
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
    fwrite(smith_lc_scratch, 1, swapped, stdout);
    fflush(stdout);
    smith_lc_refresh(path);
    *status = code;
    return true;
}

// ---- Misses

// The dependency file a miss writes: <folder>/t/<pid>-<job>.d.
static bool smith_lc_depfile(size_t number, char *out) {
    int written = snprintf(out, PATH_MAX, "%s/t/%d-%zu.d", smith_lc.folder, (int)getpid(), number);
    return written > 0 && written < PATH_MAX;
}

// The argument that makes clang-tidy write its dependency file: `-Wp,-MMD,<file>`, which clang-tooling's stripping of
// -MD, -MMD, -MF and -MT leaves alone. It goes to `extra`; returns how many arguments that is (0 for a job that is not
// cached).
static size_t smith_lc_extra(size_t number, char **extra) {
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

// Writes a file whole to a temporary name and renames it: a lint in another worktree sees all of it or none.
static void smith_lc_write(char kind, const char *key, const char *head, const char *body, size_t size) {
    char folder[PATH_MAX];
    char path[PATH_MAX];
    char temp[PATH_MAX + 64];
    char subfolder[PATH_MAX];
    if (!smith_lc_entry_path(folder, kind, key, true) || !smith_lc_entry_path(path, kind, key, false)) return;
    int base = snprintf(subfolder, sizeof subfolder, "%s/%c", smith_lc.folder, kind);
    if (base <= 0 || (size_t)base >= sizeof subfolder || !smith_lc_make_folder(subfolder) ||
        !smith_lc_make_folder(folder))
        return;
    int written = snprintf(temp, sizeof temp, "%s/.tmp-%d-%.8s", folder, (int)getpid(), key);
    if (written <= 0 || (size_t)written >= sizeof temp) return;
    int fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) return;
    size_t head_size = strlen(head);
    bool good = (head_size == 0 || write(fd, head, head_size) == (ssize_t)head_size) &&
                (size == 0 || write(fd, body, size) == (ssize_t)size);
    good = close(fd) == 0 && good;
    if (!good || rename(temp, path) != 0) unlink(temp);
}

// Stores the result of a run that completed (exit status 0 or 1: a crash or a missing tool is not a result), from the
// log the child wrote and the dependency file it left; always removes that file.
static void smith_lc_store(size_t number, int wait_status, FILE *log) {
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
    smith_lc_write('m', job->first, "", manifest, manifest_size);
    smith_lc_write('r', key, head, smith_lc_scratch, swapped);
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

// ---- Keeping it small: the rule of the other caches (an entry unused for 31 days is removed, checked at most once a
// week). A hit touches the entries it read when they are a day old.

static void smith_lc_prune_folder(const char *folder, time_t now, bool temporary_folder) {
    DIR *dir = opendir(folder);
    if (dir == nullptr) return;
    for (size_t i = 0; i < 1000000; i++) { // bounded by the entries
        struct dirent *entry = readdir(dir);
        if (entry == nullptr) break;
        char path[PATH_MAX];
        struct stat info;
        if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || entry->d_name[1] == '.')) continue;
        if (!smith_cache_join(path, folder, entry->d_name, "") || lstat(path, &info) != 0 || !S_ISREG(info.st_mode))
            continue;
        bool temporary = entry->d_name[0] == '.' || temporary_folder;
        if (smith_cache_older(&info, now, temporary ? smith_lc_temp_after : smith_cache_prune_after)) unlink(path);
    }
    closedir(dir);
}

// When the weekly check is due (lint/prune.timestamp missing or a week old): stamps it first, so a lint starting
// meanwhile skips it, and removes the entries unused for 31 days. Otherwise one stat.
static void smith_lc_prune(void) {
    char marker[PATH_MAX];
    struct stat info;
    time_t now = time(nullptr);
    if (!smith_cache_join(marker, smith_lc.folder, "prune.timestamp", "")) return;
    if (stat(marker, &info) == 0 && !smith_cache_older(&info, now, smith_cache_prune_interval)) return;
    if (!smith_cache_touch(marker)) return;
    char temporary[PATH_MAX];
    if (smith_cache_join(temporary, smith_lc.folder, "t", "")) smith_lc_prune_folder(temporary, now, true);
    for (unsigned shard = 0; shard < 512; shard++) { // m/<xx> and r/<xx>: the first two digits of a key
        char name[8];
        char folder[PATH_MAX];
        snprintf(name, sizeof name, "%c/%02x", shard < 256 ? 'm' : 'r', shard % 256);
        if (smith_cache_join(folder, smith_lc.folder, name, "")) smith_lc_prune_folder(folder, now, false);
    }
}

// Opens the cache for a run of `smith lint`; false (and nothing cached) when it is off (SMITH_LINT_CACHE=0) or cannot
// be used.
static bool smith_lc_open(void) {
    smith_lc.enabled = smith_lc_start();
    if (smith_lc.enabled) smith_lc_prune();
    return smith_lc.enabled;
}
