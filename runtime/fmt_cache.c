// `smith fmt` on many files (issue #639): the files that are already formatted are skipped, found by content, and the
// others are formatted side by side, in processes of smith itself with a few files each. One file, included by smith.c
// before runtime/verbs.c, after runtime/package.c (the SHA-256 and where the caches are). See
// knowledge/format-lint-and-editor-tools.md and knowledge/caches.md.
//
// The cache is one file for each smith, $XDG_CACHE_HOME/smith/fmt/<identity of the smith>: a list of 16-byte keys, each
// the first half of the SHA-256 of a file's content, its extension and the .clang-format files that govern its folder
// (or smith's default when there is none), meaning "clang-format leaves this content alone". Every smith has a file of
// its own because the key does not name the clang-format that judged it. A key is added when a file passed
// `--dry-run --Werror` or was written by `-i`; a file is skipped only when its key is in the list, so a changed file or
// a changed .clang-format is a new key and is formatted. SMITH_FMT_CACHE=0 turns the cache off (the files are still
// formatted side by side), as does SMITH_CLANG_FORMAT (a clang-format that is not this smith's has no identity).
constexpr size_t smith_fmt_known_max = 262144; // keys; a larger file is dropped and started again
constexpr size_t smith_fmt_key_size = 16;
constexpr size_t smith_fmt_folders_max = 1024;
constexpr size_t smith_fmt_folder_size = 512;
constexpr size_t smith_fmt_config_max = 1U << 20;
constexpr size_t smith_fmt_chunk_max = 128;       // files given to one process
constexpr time_t smith_fmt_prune_after = 2678400; // another smith's list unused for 31 days is removed

typedef struct {
    uint8_t bytes[smith_fmt_key_size];
} SmithFmtKey;

typedef struct {
    char folder[smith_fmt_folder_size];
    char chain[33]; // the first 32 digits of the SHA-256 of the .clang-format files that govern the folder
    bool usable;
} SmithFmtFolder;

static SmithFmtKey smith_fmt_known[smith_fmt_known_max];
static size_t smith_fmt_known_count;
static SmithFmtFolder smith_fmt_folders[smith_fmt_folders_max];
static size_t smith_fmt_folder_count;
static char smith_fmt_list_path[PATH_MAX];

static size_t smith_verbs_jobs(void); // runtime/verbs.c

static int smith_fmt_key_compare(const void *left, const void *right) {
    return memcmp(left, right, smith_fmt_key_size);
}

static bool smith_fmt_cache_wanted(void) {
    const char *off = getenv("SMITH_FMT_CACHE");
    const char *tool = getenv("SMITH_CLANG_FORMAT");
    return (off == nullptr || strcmp(off, "0") != 0) && (tool == nullptr || *tool == '\0');
}

// Loads this smith's list of keys, sorted. False when there is nowhere to keep it: the files are then all formatted.
static bool smith_fmt_cache_open(void) {
    if (!smith_fmt_cache_wanted()) return false;
    SmithPackageFault fault;
    char base[PATH_MAX];
    char identity[2 * smith_platform_id_bytes + 1];
    if (!smith_cache_base(base, &fault)) return false;
    smith_platform_self_id(identity, sizeof identity);
    int written = snprintf(smith_fmt_list_path, sizeof smith_fmt_list_path, "%s/fmt/%s", base, identity);
    if (written <= 0 || (size_t)written >= sizeof smith_fmt_list_path) return false;
    smith_fmt_known_count = 0;
    int fd = open(smith_fmt_list_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return true; // none yet
    struct stat info;
    if (fstat(fd, &info) != 0 || (size_t)info.st_size > smith_fmt_known_max * smith_fmt_key_size) {
        close(fd);
        unlink(smith_fmt_list_path); // grown too large: started again, the files are formatted once more
        return true;
    }
    size_t want = (size_t)info.st_size / smith_fmt_key_size * smith_fmt_key_size;
    size_t have = 0;
    for (size_t guard = 0; guard < want + 1 && have < want; guard++) { // bounded by the bytes
        ssize_t got = read(fd, (char *)smith_fmt_known + have, want - have);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        have += (size_t)got;
    }
    close(fd);
    smith_fmt_known_count = have / smith_fmt_key_size;
    qsort(smith_fmt_known, smith_fmt_known_count, smith_fmt_key_size, smith_fmt_key_compare);
    return true;
}

static bool smith_fmt_cache_has(const SmithFmtKey *key) {
    return bsearch(key, smith_fmt_known, smith_fmt_known_count, smith_fmt_key_size, smith_fmt_key_compare) != nullptr;
}

// Removes the lists of other smiths that nothing has appended to for 31 days (done when a smith writes its first).
static void smith_fmt_cache_prune(const char *folder) {
    DIR *dir = opendir(folder);
    if (dir == nullptr) return;
    time_t now = time(nullptr);
    for (struct dirent *entry = readdir(dir); entry != nullptr; entry = readdir(dir)) { // bounded by the folder
        char path[PATH_MAX];
        struct stat info;
        if (entry->d_name[0] == '.') continue;
        int written = snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
        if (written > 0 && (size_t)written < sizeof path && stat(path, &info) == 0 &&
            now - info.st_mtime > smith_fmt_prune_after)
            unlink(path);
    }
    closedir(dir);
}

// Appends keys to this smith's list, in one write; a list that cannot be written is only a cache that stays cold.
static void smith_fmt_cache_add(const SmithFmtKey *keys, size_t count) {
    if (count == 0 || smith_fmt_list_path[0] == '\0') return;
    char folder[PATH_MAX];
    snprintf(folder, sizeof folder, "%s", smith_fmt_list_path);
    *strrchr(folder, '/') = '\0';
    for (char *slash = strchr(folder + 1, '/'); slash != nullptr; slash = strchr(slash + 1, '/')) { // mkdir -p, softly
        *slash = '\0';
        mkdir(folder, 0755);
        *slash = '/';
    }
    mkdir(folder, 0755);
    int fd = open(smith_fmt_list_path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return;
    struct stat info;
    if (fstat(fd, &info) == 0 && info.st_size == 0) smith_fmt_cache_prune(folder);
    size_t total = count * smith_fmt_key_size;
    size_t done = 0;
    for (size_t guard = 0; guard < total + 1 && done < total; guard++) { // bounded by the bytes
        ssize_t put = write(fd, (const char *)keys + done, total - done);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) break;
        done += (size_t)put;
    }
    close(fd);
}

// ---- Keys

// The contents of a small file into `out`; false when it is not there or does not fit.
static bool smith_fmt_read_config(const char *path, uint8_t *out, size_t capacity, size_t *size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    size_t have = 0;
    for (size_t guard = 0; guard < capacity + 1; guard++) { // bounded by the capacity
        ssize_t got = read(fd, out + have, capacity - have);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        have += (size_t)got;
    }
    char extra;
    bool whole = read(fd, &extra, 1) == 0;
    close(fd);
    *size = have;
    return whole;
}

// The digits of the .clang-format files from `start` up to the root (all of them, nearest first, since a file may
// inherit its parent's), or of smith's default when the project has none. False when a file is too large to read.
static bool smith_fmt_chain_of(const char *start, char chain[33]) {
    static const char *const names[] = {".clang-format", "_clang-format"};
    static uint8_t content[smith_fmt_config_max];
    SmithSha256 sha;
    smith_sha256_init(&sha);
    char folder[PATH_MAX];
    snprintf(folder, sizeof folder, "%s", start);
    bool found = false;
    for (int depth = 0; depth < smith_defaults_depth_max; depth++) {
        for (size_t i = 0; i < 2; i++) {
            char path[PATH_MAX + 16];
            size_t size = 0;
            snprintf(path, sizeof path, "%s/%s", folder, names[i]);
            if (!smith_fmt_read_config(path, content, sizeof content, &size)) {
                if (access(path, F_OK) == 0) return false; // there, and too large
                continue;
            }
            uint64_t length = size;
            smith_sha256_update(&sha, (const uint8_t *)&length, sizeof length);
            smith_sha256_update(&sha, content, size);
            found = true;
        }
        char *slash = strrchr(folder, '/');
        if (slash == nullptr || slash == folder) break;
        *slash = '\0';
    }
    if (!found) smith_sha256_update(&sha, smith_defaults_clang_format, sizeof smith_defaults_clang_format);
    char hex[smith_sha256_hex_size];
    smith_sha256_final(&sha, hex);
    memcpy(chain, hex, 32);
    chain[32] = '\0';
    return true;
}

// The .clang-format chain of the folder of `file` (as written, relative to the current folder), remembered by folder.
static const SmithFmtFolder *smith_fmt_folder_of(const char *file) {
    char folder[smith_fmt_folder_size];
    const char *slash = strrchr(file, '/');
    size_t length = slash == nullptr ? 1 : (size_t)(slash - file);
    if (length >= sizeof folder) return nullptr;
    if (slash == nullptr)
        memcpy(folder, ".", 2);
    else if (slash == file)
        memcpy(folder, "/", 2);
    else {
        memcpy(folder, file, length);
        folder[length] = '\0';
    }
    for (size_t i = 0; i < smith_fmt_folder_count; i++) // bounded by the folders seen
        if (strcmp(smith_fmt_folders[i].folder, folder) == 0)
            return smith_fmt_folders[i].usable ? &smith_fmt_folders[i] : nullptr;
    if (smith_fmt_folder_count >= smith_fmt_folders_max) return nullptr; // too many folders to remember: not cached
    char start[PATH_MAX];
    smith_defaults_start(file, start);
    SmithFmtFolder *entry = &smith_fmt_folders[smith_fmt_folder_count++];
    snprintf(entry->folder, sizeof entry->folder, "%s", folder);
    entry->usable = smith_fmt_chain_of(start, entry->chain);
    return entry->usable ? entry : nullptr;
}

// The key of a file's content now. False when it cannot be read or its folder's configuration cannot be.
static bool smith_fmt_key_of(const char *file, SmithFmtKey *key) {
    const SmithFmtFolder *folder = smith_fmt_folder_of(file);
    if (folder == nullptr) return false;
    int fd = open(file, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    static uint8_t buffer[smith_package_read_size];
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_sha256_update(&sha, (const uint8_t *)folder->chain, 32);
    const char *dot = strrchr(file, '.');
    const char *extension = dot != nullptr && strchr(dot, '/') == nullptr ? dot : "";
    smith_sha256_update(&sha, (const uint8_t *)extension, strlen(extension) + 1);
    bool ok = true;
    for (uint64_t chunks = 0; chunks < (UINT64_C(1) << 40) / smith_package_read_size && ok; chunks++) {
        ssize_t got = read(fd, buffer, sizeof buffer);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) ok = false;
        if (got <= 0) break;
        smith_sha256_update(&sha, buffer, (size_t)got);
    }
    close(fd);
    if (!ok) return false;
    char hex[smith_sha256_hex_size];
    smith_sha256_final(&sha, hex);
    for (size_t i = 0; i < smith_fmt_key_size; i++) {
        char pair[3] = {hex[2 * i], hex[2 * i + 1], '\0'};
        key->bytes[i] = (uint8_t)strtoul(pair, nullptr, 16);
    }
    return true;
}

// ---- Running

// What one run knows of each file it sends to clang-format.
typedef struct {
    const char *file;
    SmithFmtKey key;
    bool keyed;  // the key above is the content as it was read
    bool passed; // clang-format left it alone (check) or wrote it (in place)
    struct timespec modified;
    off_t size;
} SmithFmtTodo;

static SmithFmtTodo smith_fmt_todo[smith_verbs_files_max];
static size_t smith_fmt_order[smith_verbs_files_max]; // the todo entries a pass covers, in order
static bool smith_fmt_passed[smith_verbs_files_max];  // by job
static char *smith_fmt_argv[smith_fmt_chunk_max + 6];

typedef struct {
    const char *self;
    bool check;
    size_t chunk;
    size_t count;
} SmithFmtPass;

static char *const *smith_fmt_job(size_t job, void *context) {
    const SmithFmtPass *pass = (const SmithFmtPass *)context;
    size_t used = 0;
    smith_fmt_argv[used++] = (char *)pass->self;
    smith_fmt_argv[used++] = (char *)"fmt";
    smith_fmt_argv[used++] = (char *)(pass->check ? "--dry-run" : "-i");
    if (pass->check) smith_fmt_argv[used++] = (char *)"--Werror";
    for (size_t i = job * pass->chunk; i < pass->count && i < (job + 1) * pass->chunk; i++)
        smith_fmt_argv[used++] = (char *)smith_fmt_todo[smith_fmt_order[i]].file;
    smith_fmt_argv[used] = nullptr;
    return smith_fmt_argv;
}

// Runs one pass over the first `count` of smith_fmt_order, `chunk` files to a process; marks the files of the processes
// that passed. A process that failed in a check says nothing here (its files are run again one by one); in place it
// says what it printed. Returns how many processes failed.
static size_t smith_fmt_pass(const char *self, bool check, size_t chunk, size_t count) {
    SmithFmtPass pass = {.self = self, .check = check, .chunk = chunk, .count = count};
    size_t jobs = (count + chunk - 1) / chunk;
    size_t workers = smith_verbs_jobs();
    size_t failed = smith_verbs_parallel_status(jobs, workers < jobs ? workers : jobs, smith_fmt_job, &pass,
                                                smith_fmt_passed, !check || chunk == 1);
    for (size_t job = 0; job < jobs; job++) // bounded by the jobs
        for (size_t i = job * chunk; i < count && i < (job + 1) * chunk; i++)
            smith_fmt_todo[smith_fmt_order[i]].passed = smith_fmt_passed[job];
    return failed;
}

// The keys to remember: of the files that passed. A file written in place has the key of what it is now, unless it was
// not touched.
static size_t smith_fmt_remember(size_t todo_count, bool check, SmithFmtKey *keys) {
    size_t count = 0;
    for (size_t i = 0; i < todo_count; i++) {
        SmithFmtTodo *todo = &smith_fmt_todo[i];
        if (!todo->passed || !todo->keyed) continue;
        struct stat info;
        bool same = check;
        if (!check && stat(todo->file, &info) == 0) {
            struct timespec modified = smith_platform_mtime(&info);
            same = info.st_size == todo->size && modified.tv_sec == todo->modified.tv_sec &&
                   modified.tv_nsec == todo->modified.tv_nsec;
        }
        if (same)
            keys[count++] = todo->key;
        else if (!check && smith_fmt_key_of(todo->file, &keys[count]))
            count++;
    }
    return count;
}

// Whether one file's key is in the list. Files that cannot be keyed are formatted.
static bool smith_fmt_cached(const char *file, SmithFmtTodo *todo, bool cache) {
    todo->file = file;
    todo->keyed = false;
    todo->passed = false;
    struct stat info;
    if (!cache || stat(file, &info) != 0 || !smith_fmt_key_of(file, &todo->key)) return false;
    todo->keyed = true;
    todo->modified = smith_platform_mtime(&info);
    todo->size = info.st_size;
    return smith_fmt_cache_has(&todo->key);
}

// Formats `count` files in place, or checks them with `check` (--dry-run --Werror), skipping those the cache knows to
// be formatted, the rest in processes of smith side by side. Prints a summary line when `summary`. Returns 0, or 1 when
// a file is not formatted (check) or clang-format failed.
static int smith_fmt_run(const char *self, bool check, const char *const *files, size_t count, bool summary) {
    SMITH_ASSERT(count <= smith_verbs_files_max);
    static SmithFmtKey remembered[smith_verbs_files_max];
    setenv("SMITH_FMT_BATCH", "1", 1); // the processes below format; they do not go through this again
    bool cache = smith_fmt_cache_open();
    size_t todo_count = 0;
    for (size_t i = 0; i < count; i++) { // bounded by the files
        if (!smith_fmt_cached(files[i], &smith_fmt_todo[todo_count], cache)) todo_count++;
    }
    size_t failed = 0;
    if (todo_count > 0) {
        size_t workers = smith_verbs_jobs();
        size_t chunk = (todo_count + workers * 4 - 1) / (workers * 4);
        chunk = chunk > smith_fmt_chunk_max ? smith_fmt_chunk_max : chunk;
        for (size_t i = 0; i < todo_count; i++)
            smith_fmt_order[i] = i;
        failed = smith_fmt_pass(self, check, chunk, todo_count);
        if (failed > 0 && check && chunk > 1) { // which files? the failed processes' files, one by one, for their words
            size_t again = 0;
            for (size_t i = 0; i < todo_count; i++)
                if (!smith_fmt_todo[i].passed) smith_fmt_order[again++] = i;
            failed = smith_fmt_pass(self, check, 1, again);
        }
    }
    if (cache) smith_fmt_cache_add(remembered, smith_fmt_remember(todo_count, check, remembered));
    if (summary)
        fprintf(stderr, "smith fmt: %zu files (%zu already formatted, %zu checked)\n", count, count - todo_count,
                todo_count);
    return failed > 0 ? 1 : 0;
}
