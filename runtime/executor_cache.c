// The shared cache of the executor's compile steps (#621): a compile whose inputs and command a build has seen before
// is not run, its outputs are cloned out of <cache>/smith/steps. Included by executor.c before executor_run.c, which
// calls it. The design and its limits are in knowledge/executor.md and knowledge/caches.md.
//
// Two levels, as ccache's direct mode has them. A compile's inputs are known only after it ran (its depfile), so:
//   manifest  m/<xx>/<key>: for a key = SHA-256 of (the command, the compiler, the environment, the content of the
//             explicit and implicit inputs), up to 8 variants: the files a compile read (the depfile of -MD, system
//             headers included) with a spec for each, `c:<sha-256>` for a file of the project and `s:<size>:<mtime>`
//             for a system file.
//   result    r/<xx>/<rkey>/: the object, its .fx effects summary if any, and the depfile, for the variant whose specs
//             all hold now. rkey = SHA-256 of (the key, the variant, and the root when an output holds it).
// The worktree's root is a token in everything stored. An output that holds the root as bytes (a -g object does not:
// smith_object maps the root to `.` in its debug info, #621) is stored under a key that has the root in it: a hit must
// be what a rebuild here makes, byte for byte, and a rebuild here writes this root. A hit is only ever a compile with
// no output (a warning is not hidden), whose inputs were all older than the compile.

constexpr size_t smith_stepcache_hex = 65;
constexpr size_t smith_stepcache_text_max = (size_t)1 << 20;    // a manifest, the lines of a variant
constexpr size_t smith_stepcache_output_max = (size_t)16 << 20; // a text output (.fx), read whole
constexpr size_t smith_stepcache_chunk = (size_t)1 << 16;
constexpr size_t smith_stepcache_carry = 16; // the longest macro name we look for, plus one
constexpr size_t smith_stepcache_variants_max = 8;
constexpr size_t smith_stepcache_deps_max = 8192;
constexpr size_t smith_stepcache_dirs_max = 64;
constexpr size_t smith_stepcache_words_max = 4096;
constexpr size_t smith_stepcache_command_max = (size_t)1 << 16;
constexpr size_t smith_stepcache_memo_max = (size_t)1 << 20;
constexpr size_t smith_stepcache_ids_max = 8;
constexpr int64_t smith_stepcache_fresh_ns = 2 * 1'000'000'000LL; // a file this new is hashed again each time
constexpr int smith_stepcache_touch_seconds = 86400;
constexpr char smith_stepcache_root_token = '\x01';
constexpr char smith_stepcache_percent_token = '\x02';

typedef enum {
    SmithStepCache_Unopened,
    SmithStepCache_On,
    SmithStepCache_Off,
} SmithStepCacheState;

// What the executor hashed, by path, so a header that a hundred compiles read is read once: valid while the file's
// size, time and inode are the same, and not kept for a file written in the last two seconds.
typedef struct {
    const char *path;
    int64_t size;
    int64_t mtime;
    uint64_t inode;
    bool time_macro;
    char hex[smith_stepcache_hex];
} SmithStepMemo;

typedef struct {
    SmithStepCacheState state;
    SmithExec *exec;
    char folder[PATH_MAX];
    char roots[2][PATH_MAX]; // the folder the build runs in as clang spells it ($PWD when valid), then as getcwd does
    size_t root_count;
    char environment[2048];
    SmithStepMemo *memo;
    size_t memo_count;
    uint32_t *table;
    size_t table_capacity;
    unsigned counter;
    char id_names[smith_stepcache_ids_max][256];
    char id_values[smith_stepcache_ids_max][320];
    size_t id_count;
} SmithStepCache;

static SmithStepCache smith_stepcache;

// The words of one command and the folders it names: the project's (-I, -iquote, -F, ...) and the system's.
typedef struct {
    char storage[smith_stepcache_command_max];
    const char *words[smith_stepcache_words_max];
    size_t count;
    const char *project[smith_stepcache_dirs_max];
    size_t project_count;
    const char *system[smith_stepcache_dirs_max];
    size_t system_count;
} SmithStepScope;

static SmithStepScope smith_stepcache_scope;
static char smith_stepcache_buffer[smith_stepcache_chunk + smith_stepcache_carry];
static char smith_stepcache_manifest[smith_stepcache_text_max];
static char smith_stepcache_lines[smith_stepcache_text_max];
static char smith_stepcache_text_in[smith_stepcache_output_max];
static char smith_stepcache_text_out[smith_stepcache_output_max];
static char smith_stepcache_whole[(size_t)2 << 20];
static char smith_stepcache_normal[(size_t)2 << 20];

// ---------------------------------------------------------------- text

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
    bool full;
} SmithStepText;

static void smith_steptext_add(SmithStepText *text, const char *bytes, size_t length) {
    if (text->size + length + 1 > text->capacity) {
        text->full = true;
        return;
    }
    if (length > 0) memcpy(text->data + text->size, bytes, length);
    text->size += length;
    text->data[text->size] = '\0';
}

[[gnu::format(printf, 2, 3)]] static void smith_steptext_addf(SmithStepText *text, const char *format, ...) {
    char piece[smith_exec_name_max + 256];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(piece, sizeof piece, format, arguments);
    va_end(arguments);
    if (length < 0 || (size_t)length >= sizeof piece) {
        text->full = true;
        return;
    }
    smith_steptext_add(text, piece, (size_t)length);
}

static void smith_stepcache_feed(SmithSha256 *sha, const char *text) {
    smith_sha256_update(sha, (const uint8_t *)text, strlen(text));
}

static bool smith_stepcache_name_char(char c) {
    return isalnum((unsigned char)c) != 0 || c == '_' || c == '-' || c == '.';
}

// Whether `needle` is at text[at] as a whole path: not the end of a longer name before it, not the start of one after.
static bool smith_stepcache_at_root(const char *text, size_t length, size_t at, const char *needle, size_t size,
                                    bool percent) {
    if (at + size > length || memcmp(text + at, needle, size) != 0) return false;
    if (at > 0) {
        char before = text[at - 1];
        if (smith_stepcache_name_char(before) || (!percent && (before == '/' || before == '%'))) return false;
    }
    return at + size == length || !smith_stepcache_name_char(text[at + size]);
}

// The root as the `%` spelling of the effects summaries' file names: every '/' a '%'.
static size_t smith_stepcache_percent(const char *root, char *out, size_t capacity) {
    size_t length = strlen(root);
    if (length + 1 > capacity) return 0;
    for (size_t i = 0; i < length; i++)
        out[i] = root[i] == '/' ? '%' : root[i];
    out[length] = '\0';
    return length;
}

// Puts the tokens in the place of the root, as a path and as a `%` name. False when the text holds a token already, or
// does not fit.
static bool smith_stepcache_normalize(const char *text, size_t length, SmithStepText *out) {
    char percent[2][PATH_MAX];
    size_t sizes[2][2] = {};
    for (size_t r = 0; r < smith_stepcache.root_count; r++) {
        sizes[r][0] = strlen(smith_stepcache.roots[r]);
        sizes[r][1] = smith_stepcache_percent(smith_stepcache.roots[r], percent[r], sizeof percent[r]);
    }
    for (size_t i = 0; i < length;) {
        if (text[i] == smith_stepcache_root_token || text[i] == smith_stepcache_percent_token) return false;
        bool replaced = false;
        for (size_t r = 0; r < smith_stepcache.root_count && !replaced; r++) {
            if (smith_stepcache_at_root(text, length, i, smith_stepcache.roots[r], sizes[r][0], false)) {
                smith_steptext_add(out, &smith_stepcache_root_token, 1);
                i += sizes[r][0];
                replaced = true;
            } else if (sizes[r][1] > 0 && smith_stepcache_at_root(text, length, i, percent[r], sizes[r][1], true)) {
                smith_steptext_add(out, &smith_stepcache_percent_token, 1);
                i += sizes[r][1];
                replaced = true;
            }
        }
        if (!replaced) smith_steptext_add(out, text + i++, 1);
    }
    return !out->full;
}

// The other way: the tokens become this build's root.
static bool smith_stepcache_expand(const char *text, size_t length, SmithStepText *out) {
    char percent[PATH_MAX];
    size_t root_size = strlen(smith_stepcache.roots[0]);
    size_t percent_size = smith_stepcache_percent(smith_stepcache.roots[0], percent, sizeof percent);
    for (size_t i = 0; i < length; i++) {
        if (text[i] == smith_stepcache_root_token)
            smith_steptext_add(out, smith_stepcache.roots[0], root_size);
        else if (text[i] == smith_stepcache_percent_token)
            smith_steptext_add(out, percent, percent_size);
        else
            smith_steptext_add(out, text + i, 1);
    }
    return !out->full;
}

// ---------------------------------------------------------------- files

// The whole file in `buffer`, which has room for `capacity` bytes. False when it is not there or does not fit.
static bool smith_stepcache_read(const char *path, char *buffer, size_t capacity, size_t *size) {
    int file = open(path, O_RDONLY | O_CLOEXEC);
    if (file < 0) return false;
    size_t used = 0;
    bool ok = true;
    for (size_t rounds = 0; ok && rounds <= capacity; rounds++) {
        ssize_t got = read(file, buffer + used, capacity - used);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0 || (got > 0 && used + (size_t)got == capacity)) ok = false;
        if (got <= 0) break;
        used += (size_t)got;
    }
    close(file);
    *size = used;
    return ok;
}

// A new file with these bytes: false when it exists, or cannot be written whole.
static bool smith_stepcache_write(const char *path, const char *bytes, size_t size) {
    int file = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (file < 0) return false;
    size_t done = 0;
    bool ok = true;
    for (size_t rounds = 0; ok && done < size && rounds <= size; rounds++) {
        ssize_t put = write(file, bytes + done, size - done);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0)
            ok = false;
        else
            done += (size_t)put;
    }
    if (close(file) != 0) ok = false;
    if (!ok) unlink(path);
    return ok;
}

// A new file that is a copy of another, by clone when the file system does that, else by reading it.
static bool smith_stepcache_copy(const char *from, const char *to) {
    if (smith_platform_clone_file(from, to)) return true;
    int source = open(from, O_RDONLY | O_CLOEXEC);
    int target = source < 0 ? -1 : open(to, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    bool ok = target >= 0;
    for (size_t rounds = 0; ok && rounds < ((size_t)1 << 32) / smith_stepcache_chunk; rounds++) {
        ssize_t got = read(source, smith_stepcache_buffer, smith_stepcache_chunk);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) ok = false;
        if (got <= 0) break;
        for (ssize_t done = 0; ok && done < got;) {
            ssize_t put = write(target, smith_stepcache_buffer + done, (size_t)(got - done));
            if (put < 0 && errno == EINTR) continue;
            if (put <= 0)
                ok = false;
            else
                done += put;
        }
    }
    if (source >= 0) close(source);
    if (target >= 0 && close(target) != 0) ok = false;
    if (!ok && target >= 0) unlink(to);
    return ok;
}

static bool smith_stepcache_make_parent(const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash == nullptr || slash == path) return true;
    char folder[PATH_MAX];
    size_t length = (size_t)(slash - path);
    if (length >= sizeof folder) return false;
    memcpy(folder, path, length);
    folder[length] = '\0';
    return smith_package_make_folders(folder);
}

static bool smith_stepcache_contains_root(const char *path) {
    int file = open(path, O_RDONLY | O_CLOEXEC);
    if (file < 0) return true; // unknown: as if it held it
    size_t keep = 0;
    bool found = false;
    bool failed = false;
    for (size_t rounds = 0; !found && !failed && rounds < ((size_t)1 << 32) / smith_stepcache_chunk; rounds++) {
        ssize_t got = read(file, smith_stepcache_buffer + keep, smith_stepcache_chunk);
        if (got < 0 && errno == EINTR) continue;
        failed = got < 0;
        if (got <= 0) break;
        size_t total = keep + (size_t)got;
        for (size_t r = 0; r < smith_stepcache.root_count; r++)
            found = found || memmem(smith_stepcache_buffer, total, smith_stepcache.roots[r],
                                    strlen(smith_stepcache.roots[r])) != nullptr;
        keep = total < PATH_MAX - 1 ? total : PATH_MAX - 1;
        if (keep > smith_stepcache_carry * 4) keep = smith_stepcache_carry * 4;
        memmove(smith_stepcache_buffer, smith_stepcache_buffer + total - keep, keep);
    }
    close(file);
    return found || failed;
}

// ---------------------------------------------------------------- hashing the inputs

// Whether the bytes name a macro whose value no key can know. The names are put together here, so that this file, which
// smith.c includes, does not name them itself and smith.c's compile can be cached.
static bool smith_stepcache_time_macro(const char *bytes, size_t size) {
    static const char *const names[] = {"TIME", "DATE", "TIMESTAMP"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char macro[32];
        int length = snprintf(macro, sizeof macro, "__%s__", names[i]);
        if (length > 0 && memmem(bytes, size, macro, (size_t)length) != nullptr) return true;
    }
    return false;
}

static int64_t smith_stepcache_mtime_of(const struct stat *info) {
    struct timespec modified = smith_platform_mtime(info);
    return (int64_t)modified.tv_sec * (int64_t)smith_exec_nanoseconds + (int64_t)modified.tv_nsec;
}

// A file small enough to read at once is hashed with the root a token, so that the same file in two folders (the
// generated module map of a project names its headers by absolute path) has one hash. A bigger one is hashed as it is.
static bool smith_stepcache_hash_small(const char *path, size_t size, char hex[smith_stepcache_hex], bool *time_macro) {
    size_t got = 0;
    if (!smith_stepcache_read(path, smith_stepcache_whole, sizeof smith_stepcache_whole, &got) || got != size)
        return false;
    SmithStepText normal = {.data = smith_stepcache_normal, .capacity = sizeof smith_stepcache_normal};
    normal.data[0] = '\0';
    if (!smith_stepcache_normalize(smith_stepcache_whole, got, &normal)) {
        // a token in the file, or no room: the bytes as they are
        normal.size = 0;
        smith_steptext_add(&normal, smith_stepcache_whole, got);
        if (normal.full) return false;
    }
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_sha256_update(&sha, (const uint8_t *)normal.data, normal.size);
    smith_sha256_final(&sha, hex);
    *time_macro = smith_stepcache_time_macro(smith_stepcache_whole, got);
    return true;
}

// SHA-256 of a file, which must still be what `expected` says when it has been read. *time_macro says whether it names
// one of the time macros. False when it cannot be read whole.
static bool smith_stepcache_hash_file(const char *path, const struct stat *expected, char hex[smith_stepcache_hex],
                                      bool *time_macro) {
    if ((size_t)expected->st_size < sizeof smith_stepcache_whole - 1) {
        struct stat after = {};
        return smith_stepcache_hash_small(path, (size_t)expected->st_size, hex, time_macro) &&
               stat(path, &after) == 0 && after.st_size == expected->st_size &&
               smith_stepcache_mtime_of(&after) == smith_stepcache_mtime_of(expected);
    }
    int file = open(path, O_RDONLY | O_CLOEXEC);
    if (file < 0) return false;
    struct stat before = {};
    SmithSha256 sha;
    smith_sha256_init(&sha);
    bool ok = fstat(file, &before) == 0 && before.st_size == expected->st_size && S_ISREG(before.st_mode);
    size_t keep = 0;
    *time_macro = false;
    for (size_t rounds = 0; ok && rounds < ((size_t)1 << 40) / smith_stepcache_chunk; rounds++) {
        ssize_t got = read(file, smith_stepcache_buffer + keep, smith_stepcache_chunk);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) ok = false;
        if (got <= 0) break;
        smith_sha256_update(&sha, (const uint8_t *)smith_stepcache_buffer + keep, (size_t)got);
        size_t total = keep + (size_t)got;
        *time_macro = *time_macro || smith_stepcache_time_macro(smith_stepcache_buffer, total);
        keep = total < smith_stepcache_carry - 1 ? total : smith_stepcache_carry - 1;
        memmove(smith_stepcache_buffer, smith_stepcache_buffer + total - keep, keep);
    }
    struct stat after = {};
    ok = ok && fstat(file, &after) == 0 && after.st_size == before.st_size &&
         smith_stepcache_mtime_of(&after) == smith_stepcache_mtime_of(&before);
    close(file);
    if (ok) smith_sha256_final(&sha, hex);
    return ok;
}

// The memo entry for a path, or nullptr; *slot is the table slot where it is, or where it would go.
static SmithStepMemo *smith_stepcache_memo_find(const char *path, size_t *slot) {
    SmithStepCache *cache = &smith_stepcache;
    size_t at = smith_exec_hash(path, strlen(path)) & (cache->table_capacity - 1);
    for (size_t probe = 0; probe < cache->table_capacity; probe++) {
        uint32_t entry = cache->table[at];
        if (entry == 0 || strcmp(cache->memo[entry - 1].path, path) == 0) {
            *slot = at;
            return entry == 0 ? nullptr : &cache->memo[entry - 1];
        }
        at = (at + 1) & (cache->table_capacity - 1);
    }
    *slot = cache->table_capacity;
    return nullptr;
}

// The hash of the content of a project file: from the memo, or read now. *time_macro as above.
static bool smith_stepcache_content(const char *path, const struct stat *info, char hex[smith_stepcache_hex],
                                    bool *time_macro, int64_t now) {
    SmithStepCache *cache = &smith_stepcache;
    int64_t mtime = smith_stepcache_mtime_of(info);
    size_t slot = 0;
    SmithStepMemo *memo = smith_stepcache_memo_find(path, &slot);
    if (memo != nullptr && memo->size == info->st_size && memo->mtime == mtime &&
        memo->inode == (uint64_t)info->st_ino) {
        memcpy(hex, memo->hex, smith_stepcache_hex);
        *time_macro = memo->time_macro;
        return true;
    }
    if (!smith_stepcache_hash_file(path, info, hex, time_macro)) return false;
    bool fresh = now - mtime < smith_stepcache_fresh_ns;
    if (fresh || (memo == nullptr && (cache->memo_count >= smith_stepcache_memo_max || slot >= cache->table_capacity)))
        return true;
    if (memo == nullptr) {
        memo = &cache->memo[cache->memo_count++];
        cache->table[slot] = (uint32_t)cache->memo_count;
        memo->path = smith_exec_copy(cache->exec, path, strlen(path));
    }
    memo->size = info->st_size;
    memo->mtime = mtime;
    memo->inode = (uint64_t)info->st_ino;
    memo->time_macro = *time_macro;
    memcpy(memo->hex, hex, smith_stepcache_hex);
    return true;
}

static bool smith_stepcache_under(const char *path, const char *folder) {
    size_t length = strlen(folder);
    while (length > 1 && folder[length - 1] == '/')
        length -= 1;
    return strncmp(path, folder, length) == 0 && (path[length] == '/' || path[length] == '\0');
}

// Whether a file belongs to the system, not to the project: it is hashed by path, size and time, and the graph does
// not track it. A relative path is the project's. An absolute one is the system's when it is in a system folder the
// command names (-isystem, -isysroot, --sysroot, SDKROOT), in smith's embedded runtime, or under /usr, /lib, the SDKs
// of macOS; a folder the command names with -I, -iquote, -idirafter, -F or -include is the project's even then.
static bool smith_stepcache_is_system(const char *path) {
    const SmithStepScope *scope = &smith_stepcache_scope;
    if (path[0] != '/') return false;
    for (size_t i = 0; i < scope->project_count; i++) {
        if (smith_stepcache_under(path, scope->project[i])) return false;
    }
    for (size_t i = 0; i < scope->system_count; i++) {
        if (smith_stepcache_under(path, scope->system[i])) return true;
    }
    if (strstr(path, "/smith/clang/") != nullptr) return true;
    static const char *const roots[] = {
        "/usr", "/lib", "/lib64", "/System", "/Library/Developer", "/Applications/Xcode.app"};
    for (size_t i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        if (smith_stepcache_under(path, roots[i])) return true;
    }
    return false;
}

// The spec of a file as a manifest records it, and its time. False when the file cannot be read, or names a macro that
// no key can know.
static bool smith_stepcache_spec(const char *path, char *out, size_t capacity, int64_t *mtime, int64_t now) {
    struct stat info = {};
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) return false;
    *mtime = smith_stepcache_mtime_of(&info);
    if (smith_stepcache_is_system(path)) {
        int written = snprintf(out, capacity, "s:%lld:%lld", (long long)info.st_size, (long long)*mtime);
        return written > 0 && (size_t)written < capacity;
    }
    char hex[smith_stepcache_hex];
    bool time_macro = false;
    if (!smith_stepcache_content(path, &info, hex, &time_macro, now) || time_macro) return false;
    int written = snprintf(out, capacity, "c:%s", hex);
    return written > 0 && (size_t)written < capacity;
}

// ---------------------------------------------------------------- the command

static bool smith_stepcache_shell_feature(char c) {
    return strchr(";&|<>()$`*?[]{}~#!\n", c) != nullptr;
}

// Splits a command as sh does, unquoted, into the scope's words. False for a shell feature (a pipe, a redirect, an
// expansion), and for a command too long.
static bool smith_stepcache_split(const char *command) {
    SmithStepScope *scope = &smith_stepcache_scope;
    size_t length = strlen(command);
    if (length + 1 > sizeof scope->storage) return false;
    char *out = scope->storage;
    scope->count = 0;
    bool in_word = false;
    for (size_t i = 0; i < length; i++) {
        char c = command[i];
        if (c == ' ' || c == '\t') {
            if (in_word) *out++ = '\0';
            in_word = false;
            continue;
        }
        if (!in_word) {
            if (scope->count >= smith_stepcache_words_max) return false;
            scope->words[scope->count++] = out;
            in_word = true;
        }
        if (c == '\'' || c == '"') {
            char quote = c;
            for (i += 1; i < length && command[i] != quote; i++) {
                if (quote == '"' && (command[i] == '$' || command[i] == '`')) return false;
                if (quote == '"' && command[i] == '\\' && i + 1 < length) i += 1;
                *out++ = command[i];
            }
            if (i >= length) return false;
        } else if (c == '\\' && i + 1 < length) {
            *out++ = command[++i];
        } else if (smith_stepcache_shell_feature(c) || c == '\\') {
            return false;
        } else {
            *out++ = c;
        }
    }
    if (in_word) *out++ = '\0';
    scope->words[scope->count] = nullptr;
    return scope->count > 0;
}

static bool smith_stepcache_has_prefix(const char *word, const char *prefix) {
    return strncmp(word, prefix, strlen(prefix)) == 0;
}

// What the command names that no depfile does, or that makes a compile write more than its object, or that is not a
// function of the inputs. Such a compile is not cached.
static bool smith_stepcache_refused(const char *word) {
    static const char *const prefixes[] = {"@",
                                           "-fprofile",
                                           "-fcs-profile",
                                           "-fmemory-profile",
                                           "-fsanitize-ignorelist",
                                           "-fsanitize-blacklist",
                                           "-fsanitize-coverage-",
                                           "-include-pch",
                                           "-fplugin",
                                           "-fpass-plugin",
                                           "-Xclang",
                                           "-load",
                                           "-fmodule-file",
                                           "-fprebuilt-module-path",
                                           "--config",
                                           "--coverage",
                                           "-ftest-coverage",
                                           "-save-temps",
                                           "-ftime-trace",
                                           "-fsave-optimization-record",
                                           "-fstack-usage",
                                           "-MJ",
                                           "-fcrash-diagnostics",
                                           "-fdiagnostics-hotness",
                                           "-gen-cdb",
                                           "-E",
                                           "-fsyntax-only",
                                           "-###",
                                           "-fno-integrated-cc1",
                                           "-fripa",
                                           "-index-store",
                                           "-fno-temp-file"};
    for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
        if (smith_stepcache_has_prefix(word, prefixes[i])) return true;
    }
    return false;
}

// The value of the flag at word i: "-I" "dir" or "-Idir". nullptr when it is not that flag.
static const char *smith_stepcache_flag_value(const SmithStepScope *scope, size_t i, const char *flag) {
    const char *word = scope->words[i];
    size_t length = strlen(flag);
    if (strcmp(word, flag) == 0) return i + 1 < scope->count ? scope->words[i + 1] : nullptr;
    if (strncmp(word, flag, length) == 0 && (flag[1] == 'I' || flag[1] == 'F' || word[length] == '=') &&
        strcmp(flag, "-i") != 0)
        return word + length + (word[length] == '=' ? 1 : 0);
    return nullptr;
}

// Fills the scope's folders from its words.
static void smith_stepcache_folders(void) {
    SmithStepScope *scope = &smith_stepcache_scope;
    static const char *const project[] = {"-I", "-iquote", "-idirafter", "-F", "-include", "-imacros"};
    static const char *const system[] = {"-isystem",     "-isysroot",      "--sysroot",    "-iframework",
                                         "-cxx-isystem", "-isystem-after", "-resource-dir"};
    scope->project_count = 0;
    scope->system_count = 0;
    for (size_t i = 0; i < scope->count; i++) {
        for (size_t k = 0; k < sizeof project / sizeof project[0] && scope->project_count < smith_stepcache_dirs_max;
             k++) {
            const char *value = smith_stepcache_flag_value(scope, i, project[k]);
            if (value != nullptr && value[0] == '/') scope->project[scope->project_count++] = value;
        }
        for (size_t k = 0; k < sizeof system / sizeof system[0] && scope->system_count < smith_stepcache_dirs_max;
             k++) {
            const char *value = smith_stepcache_flag_value(scope, i, system[k]);
            if (value != nullptr && value[0] == '/') scope->system[scope->system_count++] = value;
        }
    }
    const char *sdk = getenv("SDKROOT");
    if (sdk != nullptr && sdk[0] == '/' && scope->system_count < smith_stepcache_dirs_max)
        scope->system[scope->system_count++] = sdk;
}

// Whether the edge is a compile this cache may serve: one object (and its .fx), a depfile, -MD, no shell feature, and
// none of the flags above. The words and folders of the command stay in the scope.
static bool smith_stepcache_eligible(SmithExec *exec, SmithEdge *edge) {
    if (smith_stepcache.state == SmithStepCache_Off || exec->options.dry_run) return false;
    if (edge->step_cache == 0) {
        bool shape = edge->has_deps && edge->depfile != nullptr && !edge->phony && edge->pool == 0 &&
                     edge->output_count >= 1 && edge->output_count <= 2;
        if (shape && edge->output_count == 2) {
            const char *second = exec->graph.nodes[smith_plan_output(exec, edge, 1)].path;
            shape = smith_ends_with(second, ".fx");
        }
        bool parsed = shape && smith_stepcache_split(edge->command);
        bool md = false;
        bool depfile = false;
        for (size_t i = 0; parsed && i < smith_stepcache_scope.count; i++) {
            const char *word = smith_stepcache_scope.words[i];
            md = md || strcmp(word, "-MD") == 0;
            depfile = depfile || (strcmp(word, "-MF") == 0 && i + 1 < smith_stepcache_scope.count &&
                                  strcmp(smith_stepcache_scope.words[i + 1], edge->depfile) == 0);
            parsed = !smith_stepcache_refused(word);
        }
        edge->step_cache = parsed && md && depfile ? 1 : 2;
    }
    if (edge->step_cache != 1) return false;
    if (!smith_stepcache_split(edge->command)) return false;
    smith_stepcache_folders();
    return true;
}

// ---------------------------------------------------------------- the compiler

static bool smith_stepcache_find_program(const char *name, char *real) {
    char found[PATH_MAX];
    if (strchr(name, '/') != nullptr) return realpath(name, real) != nullptr;
    const char *path = getenv("PATH");
    if (path == nullptr) return false;
    for (const char *start = path; *start != '\0' || start == path;) {
        const char *end = strchr(start, ':');
        size_t length = end != nullptr ? (size_t)(end - start) : strlen(start);
        int written = snprintf(found, sizeof found, "%.*s/%s", (int)length, length > 0 ? start : ".", name);
        if (written > 0 && (size_t)written < sizeof found && access(found, X_OK) == 0 && realpath(found, real))
            return true;
        if (end == nullptr) break;
        start = end + 1;
    }
    return false;
}

// What a program is: its build-id (LC_UUID on macOS) when it has one, else its real path, size and time. Kept by name.
static const char *smith_stepcache_program(const char *name) {
    SmithStepCache *cache = &smith_stepcache;
    for (size_t i = 0; i < cache->id_count; i++) {
        if (strcmp(cache->id_names[i], name) == 0) return cache->id_values[i];
    }
    if (cache->id_count >= smith_stepcache_ids_max || strlen(name) >= sizeof cache->id_names[0]) return nullptr;
    char real[PATH_MAX];
    struct stat info = {};
    if (!smith_stepcache_find_program(name, real) || stat(real, &info) != 0) return nullptr;
    char *value = cache->id_values[cache->id_count];
    char id[33]; // 16 bytes in hex
    int written = 0;
    if (smith_platform_file_id(real, id)) {
        written = snprintf(value, sizeof cache->id_values[0], "id:%s", id);
    } else {
        written = snprintf(value, sizeof cache->id_values[0], "file:%s:%lld:%lld", real, (long long)info.st_size,
                           (long long)smith_stepcache_mtime_of(&info));
    }
    if (written <= 0 || (size_t)written >= sizeof cache->id_values[0]) return nullptr;
    snprintf(cache->id_names[cache->id_count], sizeof cache->id_names[0], "%s", name);
    cache->id_count += 1;
    return value;
}

// ---------------------------------------------------------------- the keys

// The environment a compile can read, as lines of the key. Fixed at the first use.
static void smith_stepcache_environment(void) {
    static const char *const names[] = {"SMITH_LANG_FLAGS",
                                        "SMITH_LANG_INPUTS",
                                        "SDKROOT",
                                        "SMITH_CLANG",
                                        "CPATH",
                                        "C_INCLUDE_PATH",
                                        "CPLUS_INCLUDE_PATH",
                                        "OBJC_INCLUDE_PATH",
                                        "OBJCPLUS_INCLUDE_PATH",
                                        "MACOSX_DEPLOYMENT_TARGET",
                                        "IPHONEOS_DEPLOYMENT_TARGET",
                                        "DEVELOPER_DIR",
                                        "CCC_OVERRIDE_OPTIONS",
                                        "CCC_ADD_ARGS",
                                        "SOURCE_DATE_EPOCH",
                                        "SMITH_LANG_LAG"};
    SmithStepText text = {.data = smith_stepcache.environment, .capacity = sizeof smith_stepcache.environment};
    text.data[0] = '\0';
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *value = getenv(names[i]);
        if (value == nullptr) continue;
        smith_steptext_addf(&text, "env %s=", names[i]);
        (void)smith_stepcache_normalize(value, strlen(value), &text);
        smith_steptext_add(&text, "\n", 1);
    }
    if (text.full) smith_stepcache.state = SmithStepCache_Off;
}

// The root as the compiler spells it: $PWD when it is this folder, else getcwd's.
static bool smith_stepcache_find_roots(void) {
    SmithStepCache *cache = &smith_stepcache;
    char physical[PATH_MAX];
    if (getcwd(physical, sizeof physical) == nullptr) return false;
    const char *logical = getenv("PWD");
    struct stat here = {};
    struct stat named = {};
    bool differs = logical != nullptr && logical[0] == '/' && strcmp(logical, physical) != 0 &&
                   stat(logical, &named) == 0 && stat(".", &here) == 0 && named.st_ino == here.st_ino &&
                   named.st_dev == here.st_dev && strlen(logical) < PATH_MAX;
    snprintf(cache->roots[0], PATH_MAX, "%s", differs ? logical : physical);
    cache->root_count = 1;
    if (differs) {
        snprintf(cache->roots[1], PATH_MAX, "%s", physical);
        cache->root_count = 2;
    }
    for (size_t r = 0; r < cache->root_count; r++) {
        for (const char *c = cache->roots[r]; *c != '\0'; c++) {
            // a spelling that a depfile writes differently (an escape) or a token cannot stand for
            if (*c == ' ' || *c == '#' || *c == '$' || *c == '\\' || *c == '\n' || *c == '\x01' || *c == '\x02' ||
                *c == ':')
                return false;
        }
    }
    return strcmp(cache->roots[0], "/") != 0;
}

// Opens the cache on first use: the folder, the roots and the memo. Off when anything is missing.
static bool smith_stepcache_open(SmithExec *exec) {
    SmithStepCache *cache = &smith_stepcache;
    if (cache->state != SmithStepCache_Unopened) return cache->state == SmithStepCache_On;
    cache->state = SmithStepCache_Off;
    const char *setting = getenv("SMITH_STEP_CACHE");
    if (setting != nullptr && (strcmp(setting, "0") == 0 || strcmp(setting, "off") == 0)) return false;
    SmithPackageFault fault;
    char base[PATH_MAX];
    if (!smith_cache_base(base, &fault) || !smith_stepcache_find_roots()) return false;
    int written = snprintf(cache->folder, sizeof cache->folder, "%s/steps", base);
    if (written <= 0 || (size_t)written >= sizeof cache->folder || !smith_package_make_folders(cache->folder))
        return false;
    cache->exec = exec;
    cache->memo = smith_exec_carve(exec, smith_stepcache_memo_max * sizeof(SmithStepMemo));
    cache->table_capacity = smith_stepcache_memo_max * 2;
    cache->table = smith_exec_carve(exec, cache->table_capacity * sizeof(uint32_t));
    cache->state = SmithStepCache_On;
    smith_stepcache_environment();
    return cache->state == SmithStepCache_On;
}

// The key of a compile: its command (the root a token), the compiler, the environment and the content of the inputs the
// manifest names. *newest is the time of the newest input. False when the compile cannot be keyed.
static bool smith_stepcache_key(SmithExec *exec, const SmithEdge *edge, char hex[smith_stepcache_hex], int64_t *newest,
                                int64_t now) {
    const SmithStepScope *scope = &smith_stepcache_scope;
    const char *compiler = smith_stepcache_program(scope->words[0]);
    const char *clang = getenv("SMITH_CLANG");
    const char *external = clang != nullptr && *clang != '\0' ? smith_stepcache_program(clang) : "none";
    if (compiler == nullptr || external == nullptr) return false;
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_stepcache_feed(&sha, "smith-step-cache 1\ncommand ");
    SmithStepText command = {.data = smith_stepcache_lines, .capacity = sizeof smith_stepcache_lines};
    command.data[0] = '\0';
    if (!smith_stepcache_normalize(edge->command, strlen(edge->command), &command)) return false;
    smith_sha256_update(&sha, (const uint8_t *)command.data, command.size);
    smith_stepcache_feed(&sha, "\n");
    smith_stepcache_feed(&sha, smith_stepcache.environment);
    smith_stepcache_feed(&sha, "compiler ");
    smith_stepcache_feed(&sha, compiler);
    smith_stepcache_feed(&sha, "\nclang ");
    smith_stepcache_feed(&sha, external);
    smith_stepcache_feed(&sha, "\n");
    *newest = 0;
    uint32_t inputs = edge->explicit_count + edge->implicit_count;
    for (uint32_t k = 0; k < inputs; k++) {
        const char *path = exec->graph.nodes[exec->graph.ids[edge->inputs + k]].path;
        char spec[smith_stepcache_hex + 32];
        int64_t mtime = 0;
        if (!smith_stepcache_spec(path, spec, sizeof spec, &mtime, now)) return false;
        if (mtime > *newest) *newest = mtime;
        command.size = 0;
        command.data[0] = '\0';
        smith_steptext_addf(&command, "input %s ", spec);
        if (!smith_stepcache_normalize(path, strlen(path), &command)) return false;
        smith_steptext_add(&command, "\n", 1);
        smith_sha256_update(&sha, (const uint8_t *)command.data, command.size);
    }
    smith_sha256_final(&sha, hex);
    return true;
}

// <folder>/<kind>/<first two digits>/<hex>[/<name>]
static bool smith_stepcache_path(char *out, const char *kind, const char *hex, const char *name) {
    int written = name == nullptr
                      ? snprintf(out, PATH_MAX, "%s/%s/%.2s/%s", smith_stepcache.folder, kind, hex, hex)
                      : snprintf(out, PATH_MAX, "%s/%s/%.2s/%s/%s", smith_stepcache.folder, kind, hex, hex, name);
    return written > 0 && written < PATH_MAX;
}

static void smith_stepcache_touch(const char *path) {
    struct stat info = {};
    if (stat(path, &info) == 0 && time(nullptr) - info.st_mtime > smith_stepcache_touch_seconds) utimes(path, nullptr);
}

// ---------------------------------------------------------------- a hit

// The variants of a manifest, oldest first: where each starts, and its header.
typedef struct {
    const char *start;
    size_t size; // header and lines
    bool rooted;
    size_t count;
} SmithStepVariant;

// A decimal number of at most nine digits at *at, then the byte `stop`; *at is past both. False if it is not one.
static bool smith_stepcache_take_number(const char **at, const char *end, unsigned long *value, char stop) {
    const char *cursor = *at;
    unsigned long number = 0;
    size_t digits = 0;
    for (; cursor < end && *cursor >= '0' && *cursor <= '9' && digits < 9; digits++, cursor++)
        number = number * 10 + (unsigned long)(*cursor - '0');
    if (digits == 0 || cursor >= end || *cursor != stop) return false;
    *value = number;
    *at = cursor + 1;
    return true;
}

// The header of a variant, "variant <0 or 1> <count>\n": *at is past it.
static bool smith_stepcache_take_header(const char **at, const char *end, bool *rooted, unsigned long *count) {
    constexpr char word[] = "variant ";
    const char *cursor = *at;
    unsigned long flag = 0;
    if ((size_t)(end - cursor) < sizeof word - 1 || memcmp(cursor, word, sizeof word - 1) != 0) return false;
    cursor += sizeof word - 1;
    if (!smith_stepcache_take_number(&cursor, end, &flag, ' ') || flag > 1 ||
        !smith_stepcache_take_number(&cursor, end, count, '\n'))
        return false;
    *rooted = flag == 1;
    *at = cursor;
    return true;
}

// Reads the manifest of a key into the variants (at most eight: the writer keeps no more). Returns how many.
static size_t smith_stepcache_variants(const char *key, SmithStepVariant variants[smith_stepcache_variants_max]) {
    char path[PATH_MAX];
    size_t size = 0;
    constexpr char header[] = "smith-step-manifest 1\n";
    if (!smith_stepcache_path(path, "m", key, nullptr) ||
        !smith_stepcache_read(path, smith_stepcache_manifest, sizeof smith_stepcache_manifest, &size))
        return 0;
    if (size < sizeof header - 1 || memcmp(smith_stepcache_manifest, header, sizeof header - 1) != 0) return 0;
    const char *end = smith_stepcache_manifest + size;
    const char *at = smith_stepcache_manifest + sizeof header - 1;
    size_t total = 0;
    for (size_t guard = 0; at < end && guard < size && total < smith_stepcache_variants_max; guard++) {
        bool rooted = false;
        unsigned long count = 0;
        const char *cursor = at;
        if (!smith_stepcache_take_header(&cursor, end, &rooted, &count)) break;
        for (unsigned long i = 0; i < count && cursor < end; i++) {
            const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
            if (newline == nullptr) return 0;
            cursor = newline + 1;
        }
        variants[total] =
            (SmithStepVariant){.start = at, .size = (size_t)(cursor - at), .rooted = rooted, .count = count};
        total += 1;
        at = cursor;
    }
    return total;
}

// Whether every file of a variant is what its spec says. Lines are "<spec> <path>" with the root a token.
static bool smith_stepcache_variant_holds(const SmithStepVariant *variant, int64_t now) {
    const char *line = (const char *)memchr(variant->start, '\n', variant->size) + 1;
    const char *end = variant->start + variant->size;
    for (size_t i = 0; i < variant->count; i++) {
        const char *newline = memchr(line, '\n', (size_t)(end - line));
        const char *space = memchr(line, ' ', (size_t)(newline - line));
        if (space == nullptr) return false;
        char path[smith_exec_name_max];
        SmithStepText text = {.data = path, .capacity = sizeof path};
        path[0] = '\0';
        if (!smith_stepcache_expand(space + 1, (size_t)(newline - space - 1), &text)) return false;
        char spec[smith_stepcache_hex + 32];
        int64_t mtime = 0;
        if (!smith_stepcache_spec(path, spec, sizeof spec, &mtime, now)) return false;
        if (strlen(spec) != (size_t)(space - line) || memcmp(spec, line, (size_t)(space - line)) != 0) return false;
        line = newline + 1;
    }
    return true;
}

// The key of the result of a variant: the manifest's key, the variant as written, and the root when it matters.
static void smith_stepcache_result_key(const char *key, const SmithStepVariant *variant,
                                       char hex[smith_stepcache_hex]) {
    SmithSha256 sha;
    smith_sha256_init(&sha);
    smith_stepcache_feed(&sha, "smith-step-result 1\n");
    smith_stepcache_feed(&sha, key);
    smith_stepcache_feed(&sha, "\n");
    smith_sha256_update(&sha, (const uint8_t *)variant->start, variant->size);
    if (variant->rooted) smith_stepcache_feed(&sha, smith_stepcache.roots[0]);
    smith_sha256_final(&sha, hex);
}

// Puts a file of the result in its place: a clone of a raw output, the text of a normalized one with this root in it.
static bool smith_stepcache_put(const char *source, const char *destination, bool text) {
    char temporary[smith_exec_name_max];
    int written = snprintf(temporary, sizeof temporary, "%s.step-%d", destination, (int)getpid());
    if (written <= 0 || (size_t)written >= sizeof temporary || !smith_stepcache_make_parent(destination)) return false;
    unlink(temporary);
    bool ok = false;
    if (!text) {
        ok = smith_stepcache_copy(source, temporary);
    } else {
        size_t size = 0;
        SmithStepText out = {.data = smith_stepcache_text_out, .capacity = sizeof smith_stepcache_text_out};
        out.data[0] = '\0';
        ok = smith_stepcache_read(source, smith_stepcache_text_in, sizeof smith_stepcache_text_in, &size) &&
             smith_stepcache_expand(smith_stepcache_text_in, size, &out) &&
             smith_stepcache_write(temporary, out.data, out.size);
    }
    if (ok && rename(temporary, destination) != 0) ok = false;
    if (!ok) unlink(temporary);
    return ok;
}

// Restores the outputs and the depfile of a result. False when anything is missing or does not fit.
static bool smith_stepcache_apply(SmithExec *exec, const SmithEdge *edge, const char *folder) {
    char path[PATH_MAX];
    char meta[256];
    size_t size = 0;
    if (snprintf(path, sizeof path, "%s/meta", folder) >= (int)sizeof path ||
        !smith_stepcache_read(path, meta, sizeof meta, &size))
        return false;
    meta[size] = '\0';
    constexpr char head[] = "smith-step-result 1\n";
    const char *cursor = meta + sizeof head - 1;
    unsigned long count = 0;
    if (size < sizeof head || memcmp(meta, head, sizeof head - 1) != 0 ||
        !smith_stepcache_take_number(&cursor, meta + size, &count, '\n') || count != edge->output_count)
        return false;
    for (uint32_t k = 0; k < edge->output_count; k++) {
        char name[16];
        snprintf(name, sizeof name, "o%u", k);
        if (snprintf(path, sizeof path, "%s/%s", folder, name) >= (int)sizeof path) return false;
        const char *destination = exec->graph.nodes[smith_plan_output(exec, edge, k)].path;
        if (!smith_stepcache_put(path, destination, k > 0)) return false;
    }
    if (snprintf(path, sizeof path, "%s/d", folder) >= (int)sizeof path) return false;
    return smith_stepcache_put(path, edge->depfile, true);
}

// Looks for a result of the edge, and when there is one puts its outputs in place. *key says how the edge was keyed
// (for the store after a miss). True for a hit: the edge's outputs and its depfile are as a compile leaves them.
static bool smith_stepcache_restore(SmithExec *exec, uint32_t number, SmithStepKey *key, int64_t now) {
    SmithEdge *edge = &exec->graph.edges[number];
    key->valid = false;
    if (!smith_stepcache_open(exec) || !smith_stepcache_eligible(exec, edge)) return false;
    int64_t newest = 0;
    if (!smith_stepcache_key(exec, edge, key->manifest, &newest, now)) return false;
    key->valid = true;
    SmithStepVariant found[smith_stepcache_variants_max] = {};
    size_t total = smith_stepcache_variants(key->manifest, found);
    for (size_t i = total; i > 0; i--) { // the newest first
        const SmithStepVariant *variant = &found[i - 1];
        if (!smith_stepcache_variant_holds(variant, now)) continue;
        char result[smith_stepcache_hex];
        char folder[PATH_MAX];
        smith_stepcache_result_key(key->manifest, variant, result);
        if (!smith_stepcache_path(folder, "r", result, nullptr) || !smith_stepcache_apply(exec, edge, folder)) continue;
        smith_stepcache_touch(folder);
        char manifest[PATH_MAX];
        if (smith_stepcache_path(manifest, "m", key->manifest, nullptr)) smith_stepcache_touch(manifest);
        smith_plan_explain(exec, "%s restored from the step cache",
                           exec->graph.nodes[smith_plan_output(exec, edge, 0)].path);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- a miss, and the store after it

// What the depfile names, for the graph. A compile with -MD names the system's headers too, for the cache's variant
// alone: the graph tracks what -MMD would have named, so a no-op build stats no more files than it did.
static uint32_t smith_stepcache_graph_deps(SmithExec *exec, const SmithEdge *edge, const char *const *deps,
                                           uint32_t count, const char ***kept) {
    bool split = smith_stepcache_split(edge->command);
    bool has_md = false;
    for (size_t i = 0; split && i < smith_stepcache_scope.count; i++)
        has_md = has_md || strcmp(smith_stepcache_scope.words[i], "-MD") == 0;
    if (has_md) smith_stepcache_folders();
    const char **list =
        (const char **)smith_exec_push(exec, ((size_t)count + 1) * sizeof(const char *), alignof(const char *));
    uint32_t used = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!has_md || !smith_stepcache_is_system(deps[i])) list[used++] = deps[i];
    }
    *kept = list;
    return used;
}

// The lines of a variant for the files a compile read, newest content: "variant <rooted> <count>" and a line for each.
// False when a file cannot be keyed or changed after the compile began.
static bool smith_stepcache_variant_lines(const SmithJob *job, bool rooted, SmithStepText *out, int64_t now) {
    SmithStepText lines = {.data = smith_stepcache_text_in, .capacity = sizeof smith_stepcache_text_in};
    lines.data[0] = '\0';
    if (job->step_dep_count > smith_stepcache_deps_max) return false;
    for (uint32_t i = 0; i < job->step_dep_count; i++) {
        char spec[smith_stepcache_hex + 32];
        int64_t mtime = 0;
        const char *path = job->step_deps[i];
        if (strchr(path, '\n') != nullptr || !smith_stepcache_spec(path, spec, sizeof spec, &mtime, now)) return false;
        if (mtime >= job->wall) return false;
        smith_steptext_addf(&lines, "%s ", spec);
        if (!smith_stepcache_normalize(path, strlen(path), &lines)) return false;
        smith_steptext_add(&lines, "\n", 1);
    }
    smith_steptext_addf(out, "variant %d %u\n", rooted ? 1 : 0, job->step_dep_count);
    smith_steptext_add(out, lines.data, lines.size);
    return !out->full && !lines.full;
}

// Makes <folder>/<kind>/<xx>.
static bool smith_stepcache_shard(const char *kind, const char *hex) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s/%s/%.2s", smith_stepcache.folder, kind, hex) >= (int)sizeof path) return false;
    return smith_package_make_folders(path);
}

// Writes the files of a result in a folder of its own and moves it to its name. An existing result is as good.
static bool smith_stepcache_write_result(SmithExec *exec, const SmithEdge *edge, const SmithJob *job,
                                         const char *result) {
    char folder[PATH_MAX];
    char temporary[PATH_MAX];
    char path[PATH_MAX];
    if (!smith_stepcache_path(folder, "r", result, nullptr) || !smith_stepcache_shard("r", result)) return false;
    struct stat existing = {};
    if (stat(folder, &existing) == 0) return true;
    if (snprintf(temporary, sizeof temporary, "%s/tmp/%d-%u", smith_stepcache.folder, (int)getpid(),
                 smith_stepcache.counter++) >= (int)sizeof temporary ||
        !smith_package_make_folders(temporary))
        return false;
    bool ok = true;
    SmithStepText meta = {.data = smith_stepcache_text_out, .capacity = sizeof smith_stepcache_text_out};
    meta.data[0] = '\0';
    smith_steptext_addf(&meta, "smith-step-result 1\n%u\n", edge->output_count);
    for (uint32_t k = 0; k < edge->output_count && ok; k++) {
        const char *output = exec->graph.nodes[smith_plan_output(exec, edge, k)].path;
        struct stat info = {};
        ok = stat(output, &info) == 0 && snprintf(path, sizeof path, "%s/o%u", temporary, k) < (int)sizeof path;
        if (ok && k == 0) {
            ok = smith_stepcache_copy(output, path);
        } else if (ok) {
            size_t size = 0;
            SmithStepText text = {.data = smith_stepcache_text_in, .capacity = sizeof smith_stepcache_text_in};
            char *source = smith_stepcache_text_in + sizeof smith_stepcache_text_in / 2;
            text.capacity = sizeof smith_stepcache_text_in / 2;
            text.data[0] = '\0';
            ok = smith_stepcache_read(output, source, sizeof smith_stepcache_text_in / 2, &size) &&
                 smith_stepcache_normalize(source, size, &text) && smith_stepcache_write(path, text.data, text.size);
        }
        smith_steptext_addf(&meta, "%c %lld\n", k == 0 ? 'r' : 't', (long long)info.st_size);
    }
    SmithStepText depfile = {.data = smith_stepcache_text_in, .capacity = sizeof smith_stepcache_text_in};
    depfile.data[0] = '\0';
    ok = ok && snprintf(path, sizeof path, "%s/d", temporary) < (int)sizeof path &&
         smith_stepcache_normalize(job->depfile_text, job->depfile_size, &depfile) &&
         smith_stepcache_write(path, depfile.data, depfile.size);
    ok = ok && snprintf(path, sizeof path, "%s/meta", temporary) < (int)sizeof path && !meta.full &&
         smith_stepcache_write(path, meta.data, meta.size);
    ok = ok && rename(temporary, folder) == 0;
    if (!ok) smith_package_remove(temporary);
    return ok || stat(folder, &existing) == 0;
}

// Adds the variant to the key's manifest (the newest eight stay), through a temporary file and a rename.
static void smith_stepcache_add_variant(const char *key, const SmithStepText *variant) {
    char path[PATH_MAX];
    char temporary[PATH_MAX];
    SmithStepVariant found[smith_stepcache_variants_max] = {};
    if (!smith_stepcache_path(path, "m", key, nullptr) || !smith_stepcache_shard("m", key)) return;
    size_t total = smith_stepcache_variants(key, found);
    SmithStepText text = {.data = smith_stepcache_lines, .capacity = sizeof smith_stepcache_lines};
    text.data[0] = '\0';
    smith_steptext_add(&text, "smith-step-manifest 1\n", 22);
    size_t first = total + 1 > smith_stepcache_variants_max ? total + 1 - smith_stepcache_variants_max : 0;
    for (size_t i = first; i < total; i++) {
        if (found[i].size == variant->size && memcmp(found[i].start, variant->data, variant->size) == 0) return;
        smith_steptext_add(&text, found[i].start, found[i].size);
    }
    smith_steptext_add(&text, variant->data, variant->size);
    if (text.full || snprintf(temporary, sizeof temporary, "%s/tmp/%d-%u", smith_stepcache.folder, (int)getpid(),
                              smith_stepcache.counter++) >= (int)sizeof temporary)
        return;
    if (!smith_stepcache_make_parent(temporary) || !smith_stepcache_write(temporary, text.data, text.size)) return;
    if (rename(temporary, path) != 0) unlink(temporary);
}

// After a compile that ran and passed: stores its outputs for the inputs it read. Nothing is stored when the compile
// printed anything (a warning is not hidden), a file it read was written after it began, or a key cannot be made.
static void smith_stepcache_store(SmithExec *exec, SmithEdge *edge, const SmithJob *job, int64_t now) {
    if (!job->key.valid || job->output_size > 0 || job->truncated || job->step_deps == nullptr ||
        job->depfile_text == nullptr || !smith_stepcache_eligible(exec, edge))
        return;
    char again[smith_stepcache_hex];
    int64_t newest = 0;
    if (!smith_stepcache_key(exec, edge, again, &newest, now) || strcmp(again, job->key.manifest) != 0 ||
        newest >= job->wall)
        return;
    bool rooted = false;
    for (uint32_t k = 0; k < edge->output_count && !rooted; k++) {
        const char *output = exec->graph.nodes[smith_plan_output(exec, edge, k)].path;
        rooted = !smith_ends_with(output, ".fx") && smith_stepcache_contains_root(output);
    }
    static char variant_storage[smith_stepcache_text_max];
    SmithStepText variant = {.data = variant_storage, .capacity = sizeof variant_storage};
    variant.data[0] = '\0';
    if (!smith_stepcache_variant_lines(job, rooted, &variant, now)) return;
    SmithStepVariant parsed = {.start = variant.data, .size = variant.size, .rooted = rooted};
    char result[smith_stepcache_hex];
    smith_stepcache_result_key(job->key.manifest, &parsed, result);
    if (smith_stepcache_write_result(exec, edge, job, result)) smith_stepcache_add_variant(job->key.manifest, &variant);
}

// ---------------------------------------------------------------- the 31-day rule and the cap

constexpr int smith_stepcache_idle_seconds = 2678400; // an entry unused for 31 days is removed
constexpr int smith_stepcache_prune_seconds = 3600;   // the check, at most once an hour
constexpr size_t smith_stepcache_shards = 256;
constexpr size_t smith_stepcache_entries_max = (size_t)1 << 24;

// What a pass over the results does with each: add its size to the ages (the first pass), or remove it when it is
// idle for 31 days or older than the cutoff of #618's cap (the second).
static void smith_stepcache_pass_shard(const char *shard, time_t now, long cutoff, bool measure) {
    DIR *directory = opendir(shard);
    if (directory == nullptr) return;
    for (size_t i = 0; i < smith_stepcache_entries_max; i++) {
        struct dirent *entry = readdir(directory);
        if (entry == nullptr) break;
        char path[PATH_MAX];
        struct stat info = {};
        if (entry->d_name[0] == '.' || snprintf(path, sizeof path, "%s/%s", shard, entry->d_name) >= (int)sizeof path ||
            lstat(path, &info) != 0)
            continue;
        if (measure) {
            uint64_t bytes = 0;
            time_t newest = 0;
            smith_cache_measure(path, &bytes, &newest);
            smith_cache_ages_add(bytes, info.st_mtime, now);
        } else if (now - info.st_mtime > smith_stepcache_idle_seconds || smith_cache_goes(info.st_mtime, now, cutoff)) {
            smith_package_remove(path);
        }
    }
    closedir(directory);
}

static void smith_stepcache_pass(const char *kind, time_t now, long cutoff, bool measure) {
    char folder[PATH_MAX];
    if (snprintf(folder, sizeof folder, "%s/%s", smith_stepcache.folder, kind) >= (int)sizeof folder) return;
    DIR *directory = opendir(folder);
    if (directory == nullptr) return;
    for (size_t i = 0; i < smith_stepcache_shards + 2; i++) {
        struct dirent *entry = readdir(directory);
        char shard[PATH_MAX];
        if (entry == nullptr) break;
        if (entry->d_name[0] != '.' &&
            snprintf(shard, sizeof shard, "%s/%s", folder, entry->d_name) < (int)sizeof shard)
            smith_stepcache_pass_shard(shard, now, cutoff, measure);
    }
    closedir(directory);
}

// Keeps the folder within the rules: an entry unused for 31 days goes, and when the results are over the cap of every
// smith cache (SMITH_CACHE_MAX, #618) the least recently used go until they fit. At most once an hour: the stamp is
// touched first, so a build that starts meanwhile does not check again. A hit touches its entries, so what a build
// is using is the newest; one removed under a build that restores it is a miss, and the compile runs.
static void smith_stepcache_prune(SmithExec *exec) {
    if (!smith_stepcache_open(exec) || exec->options.dry_run) return;
    char stamp[PATH_MAX];
    struct stat info = {};
    time_t now = time(nullptr);
    if (snprintf(stamp, sizeof stamp, "%s/prune.timestamp", smith_stepcache.folder) >= (int)sizeof stamp) return;
    if (stat(stamp, &info) == 0 && now - info.st_mtime < smith_stepcache_prune_seconds) return;
    (void)smith_cache_touch(stamp);
    smith_cache_ages_reset();
    smith_stepcache_pass("r", now, -1, true);
    uint64_t cap = smith_cache_limit();
    long cutoff = smith_cache_cutoff(smith_cache_ages.total > cap ? smith_cache_ages.total - cap : 0);
    smith_stepcache_pass("r", now, cutoff, false);
    smith_stepcache_pass("m", now, -1, false);
    char temporary[PATH_MAX];
    if (snprintf(temporary, sizeof temporary, "%s/tmp", smith_stepcache.folder) < (int)sizeof temporary)
        smith_stepcache_pass_shard(temporary, now, -1, false);
}
