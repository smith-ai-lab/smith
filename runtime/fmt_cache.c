// The cache of `smith fmt` with no arguments (#3081, knowledge/caches.md): a file that a run found formatted is kept as
// an empty entry under $XDG_CACHE_HOME/smith/fmt, named by a key, and the next run leaves that file out. The key is the
// SHA-256 of what the answer is a function of: the formatter's identity (the fmt identity `smith build` hashes: the
// LLVM pack, front/, patches/, shim/format.cpp, smith.c, the fmt runtime and smith's default style; the binary's
// build-id when smith has none), the environment that picks another clang-format, the file's path from the project's
// root and its content, and the `.clang-format`, `_clang-format` and `.clang-format-ignore` files from its folder up
// to /. An entry is written only after the batch that held the file exited 0, so a failure is never kept. Linked into
// smith before runtime/verbs.c, whose `smith fmt` asks it about each file.
#include "fmt_cache.h"
#include "core.h"
#include "package.h"
#include "platform_linux.h"

#include <dirent.h>
#include <sys/stat.h>

constexpr size_t smith_fc_depth_max = 256; // folders from a file up to /
constexpr char smith_fc_header[] = "smith-fmt-cache 1\n";

typedef struct {
    bool enabled;
    char folder[PATH_MAX]; // <cache>/smith/fmt
    char root[PATH_MAX];   // the folder smith fmt runs in, the project's root
    SmithSha256 base;      // the identity and the environment
} SmithFc;

static SmithFc smith_fc;

static void smith_fc_text_add(SmithSha256 *sha, const char *text) {
    smith_sha256_update(sha, (const uint8_t *)text, strlen(text) + 1);
}

// The fmt identity `smith build` hashed (runtime/lint_identity.c), or nullptr when this smith has none.
#ifdef SMITH_LINT_IDENTITY_LINKED
extern const char smith_fmt_identity[];
#endif
static const char *smith_fc_identity(void) {
#ifdef SMITH_LINT_IDENTITY_LINKED
    if (smith_fmt_identity[0] != '\0') return smith_fmt_identity;
#endif
    return nullptr;
}

// Adds a file's name and digest to a key; a file that is not there adds nothing, one that cannot be read adds "-".
static void smith_fc_add_present(SmithSha256 *sha, const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return;
    char hex[smith_sha256_hex_size];
    smith_fc_text_add(sha, path);
    smith_fc_text_add(sha, S_ISREG(info.st_mode) && smith_sha256_file(path, hex) ? hex : "-");
}

// The style files and the ignore lists from the folder of the file up to /: clang-format reads the nearest style
// (which may inherit the next), and each `.clang-format-ignore` on the way can take the file out.
static bool smith_fc_add_configs(SmithSha256 *sha, const char *file) {
    static const char *const names[] = {".clang-format", "_clang-format", ".clang-format-ignore"};
    char folder[PATH_MAX];
    int written = snprintf(folder, sizeof folder, "%s/%s", smith_fc.root, file);
    if (written <= 0 || (size_t)written >= sizeof folder) return false;
    for (size_t depth = 0; depth < smith_fc_depth_max; depth++) { // bounded by the folders
        char *slash = strrchr(folder, '/');
        if (slash == nullptr) break;
        *slash = '\0';
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
            char config[PATH_MAX];
            written = snprintf(config, sizeof config, "%s/%s", folder, names[i]);
            if (written > 0 && (size_t)written < sizeof config) smith_fc_add_present(sha, config);
        }
        if (folder[0] == '\0') break;
    }
    return true;
}

// The key of the file as it is now; false when it cannot be made (the file cannot be read, a path too long).
static bool smith_fc_key(const char *file, char key[smith_sha256_hex_size]) {
    char hex[smith_sha256_hex_size];
    if (!smith_sha256_file(file, hex)) return false;
    SmithSha256 sha = smith_fc.base;
    smith_fc_text_add(&sha, file);
    smith_fc_text_add(&sha, hex);
    if (!smith_fc_add_configs(&sha, file)) return false;
    smith_sha256_final(&sha, key);
    return true;
}

static bool smith_fc_entry(char *out, const char *key, bool folder_only) {
    int written = folder_only ? snprintf(out, PATH_MAX, "%s/%.2s", smith_fc.folder, key)
                              : snprintf(out, PATH_MAX, "%s/%.2s/%s", smith_fc.folder, key, key + 2);
    return written > 0 && written < PATH_MAX;
}

// True when a run found the file, as it is now, formatted. An entry used is touched once a day, so the weekly prune
// keeps it.
bool smith_fc_formatted(const char *file) {
    char key[smith_sha256_hex_size];
    char path[PATH_MAX];
    struct stat info;
    if (!smith_fc.enabled || !smith_fc_key(file, key) || !smith_fc_entry(path, key, false) || stat(path, &info) != 0)
        return false;
    if (time(nullptr) - info.st_mtime > 86400) utimensat(AT_FDCWD, path, nullptr, 0);
    return true;
}

// Keeps that the file, as it is now (after `-i` rewrote it), is formatted.
void smith_fc_keep(const char *file) {
    char key[smith_sha256_hex_size];
    char folder[PATH_MAX];
    char path[PATH_MAX];
    if (!smith_fc.enabled || !smith_fc_key(file, key) || !smith_fc_entry(folder, key, true) ||
        !smith_fc_entry(path, key, false))
        return;
    if (mkdir(folder, 0777) != 0 && errno != EEXIST) return;
    int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
    if (fd >= 0) close(fd);
}

// Removes the entries of one shard unused for 31 days.
static void smith_fc_prune_folder(const char *folder, time_t now) {
    DIR *dir = opendir(folder);
    if (dir == nullptr) return;
    for (size_t i = 0; i < 1000000; i++) { // bounded by the entries
        struct dirent *entry = readdir(dir);
        if (entry == nullptr) break;
        char path[PATH_MAX];
        struct stat info;
        if (entry->d_name[0] == '.') continue;
        if (!smith_cache_join(path, folder, entry->d_name, "") || lstat(path, &info) != 0 || !S_ISREG(info.st_mode))
            continue;
        if (smith_cache_older(&info, now, smith_cache_prune_after)) unlink(path);
    }
    closedir(dir);
}

// When the weekly check is due (fmt/prune.timestamp missing or a week old): stamps it first, then prunes.
static void smith_fc_prune(void) {
    char marker[PATH_MAX];
    struct stat info;
    time_t now = time(nullptr);
    if (!smith_cache_join(marker, smith_fc.folder, "prune.timestamp", "")) return;
    if (stat(marker, &info) == 0 && !smith_cache_older(&info, now, smith_cache_prune_interval)) return;
    if (!smith_cache_touch(marker)) return;
    for (unsigned shard = 0; shard < 256; shard++) { // <xx>: the first two digits of a key
        char name[4];
        char folder[PATH_MAX];
        snprintf(name, sizeof name, "%02x", shard);
        if (smith_cache_join(folder, smith_fc.folder, name, "")) smith_fc_prune_folder(folder, now);
    }
}

// Opens the cache for a run of `smith fmt`; false (and nothing cached) when it is off (SMITH_FMT_CACHE=0) or cannot be
// used.
bool smith_fc_open(void) {
    smith_fc = (SmithFc){};
    const char *setting = getenv("SMITH_FMT_CACHE");
    if (setting != nullptr && strcmp(setting, "0") == 0) return false;
    SmithPackageFault error;
    char base[PATH_MAX];
    if (!smith_cache_base(base, &error) || getcwd(smith_fc.root, sizeof smith_fc.root) == nullptr) return false;
    int written = snprintf(smith_fc.folder, sizeof smith_fc.folder, "%s/fmt", base);
    if (written <= 0 || (size_t)written >= sizeof smith_fc.folder) return false;
    if (!smith_package_make_folders(base) || (mkdir(smith_fc.folder, 0777) != 0 && errno != EEXIST)) return false;
    char identity[2 * smith_platform_id_bytes + 1] = "";
    const char *fmt_identity = smith_fc_identity();
    if (fmt_identity == nullptr) smith_platform_self_id(identity, sizeof identity);
    smith_sha256_init(&smith_fc.base);
    smith_fc_text_add(&smith_fc.base, smith_fc_header);
    smith_fc_text_add(&smith_fc.base, fmt_identity != nullptr ? fmt_identity : identity);
    const char *tool = getenv("SMITH_CLANG_FORMAT"); // a stage0 runs another clang-format
    smith_fc_text_add(&smith_fc.base, tool != nullptr ? tool : "");
    smith_fc.enabled = true;
    smith_fc_prune();
    return true;
}
