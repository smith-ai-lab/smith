// `smith cache`: shows and cleans the two caches, and the lock that tells a clean a build is using them. One file,
// included by smith.c after runtime/package.c, which holds where the caches are and the pruning rule. See
// knowledge/caches.md.
//
// The caches are $XDG_CACHE_HOME/smith/modules (clang's module cache, shared by every project and worktree) and
// $XDG_CACHE_HOME/smith/packages (smith_package). Both are keyed by hash, so anything removed is simply built or
// fetched again.
#include <sys/file.h>

constexpr size_t smith_cache_entries_max = 4096;
constexpr double smith_cache_unit = 1000.0;

typedef struct {
    char name[256];
    uint64_t bytes;
    time_t last_used;
} SmithCacheEntry;

static SmithCacheEntry smith_cache_entries[smith_cache_entries_max];

// <base>/lock: smith build holds it shared for as long as the build runs, `smith cache clean` takes it exclusively.
static bool smith_cache_lock_path(char *out) {
    SmithPackageFault error;
    char base[PATH_MAX];
    if (!smith_cache_base(base, &error)) return false;
    int written = snprintf(out, PATH_MAX, "%s/lock", base);
    return written > 0 && written < PATH_MAX;
}

// A build holds the caches by holding a shared flock on <base>/lock. The descriptor is inherited, not closed on exec,
// by ninja and every process it starts, so the lock lasts exactly as long as the build's processes do, and it is
// released by the system if smith is killed. Costs one open and one flock. Without a usable cache folder the build
// simply holds nothing (and then has no cache a clean could remove).
static void smith_cache_hold(void) {
    char lock[PATH_MAX];
    SmithPackageFault error;
    char base[PATH_MAX];
    if (!smith_cache_lock_path(lock) || !smith_cache_base(base, &error) || !smith_package_make_folders(base)) return;
    int fd = open(lock, O_RDWR | O_CREAT, 0666);
    if (fd < 0) return;
    if (flock(fd, LOCK_SH) != 0) close(fd);
}

static void smith_cache_format_size(uint64_t bytes, char out[32]) {
    static const char *const units[] = {"B", "kB", "MB", "GB", "TB"};
    double value = (double)bytes;
    size_t unit = 0;
    for (; unit < 4 && value >= smith_cache_unit; unit++)
        value /= smith_cache_unit;
    int written = unit == 0 ? snprintf(out, 32, "%llu B", (unsigned long long)bytes)
                            : snprintf(out, 32, "%.1f %s", value, units[unit]);
    SMITH_ASSERT(written > 0 && written < 32);
}

static int smith_cache_compare(const void *left, const void *right) {
    return strcmp(((const SmithCacheEntry *)left)->name, ((const SmithCacheEntry *)right)->name);
}

// The entries of one cache: its folders, except clang's and smith's own files (names that start with a dot, the
// timestamps and the stamps). Fills smith_cache_entries, sorted by name, and returns how many there are in all (which
// may be more than the table holds).
static size_t smith_cache_scan(SmithCacheKind kind, const char *folder, size_t *kept) {
    DIR *dir = opendir(folder);
    *kept = 0;
    if (dir == nullptr) return 0;
    size_t total = 0;
    for (size_t i = 0; i < 1000000; i++) {
        struct dirent *entry = readdir(dir);
        if (entry == nullptr) break;
        char path[PATH_MAX];
        if (entry->d_name[0] == '.' || !smith_cache_join(path, folder, entry->d_name, "")) continue;
        if (!smith_package_is_folder(path)) continue;
        total++;
        if (*kept == smith_cache_entries_max) continue;
        SmithCacheEntry *item = &smith_cache_entries[(*kept)++];
        snprintf(item->name, sizeof item->name, "%s", entry->d_name);
        smith_cache_measure(path, &item->bytes, &item->last_used);
        struct stat stamp;
        char used[PATH_MAX];
        if (kind == SmithCache_Packages && smith_cache_join(used, folder, entry->d_name, ".used") &&
            stat(used, &stamp) == 0) {
            item->last_used = stamp.st_mtime; // the stamp is the last use; the folder's own files are older
        }
    }
    closedir(dir);
    qsort(smith_cache_entries, *kept, sizeof smith_cache_entries[0], smith_cache_compare);
    return total;
}

static void smith_cache_show_one(SmithCacheKind kind, bool verbose) {
    char folder[PATH_MAX];
    SmithPackageFault error;
    if (!smith_cache_folder(kind, folder, &error)) smith_die(smith_exit_usage, "cache:", error.text);
    size_t kept = 0;
    size_t total = smith_cache_scan(kind, folder, &kept);
    uint64_t bytes = 0;
    time_t newest = 0;
    if (smith_package_is_folder(folder)) smith_cache_measure(folder, &bytes, &newest);
    char size[32];
    smith_cache_format_size(bytes, size);
    char cap[32];
    smith_cache_format_size(smith_cache_limit(), cap);
    printf("%s: %s\n  %zu %s, %s (cap %s)\n", smith_cache_name(kind), folder, total, total == 1 ? "entry" : "entries",
           size, cap);
    if (!verbose) return;
    for (size_t i = 0; i < kept; i++) {
        char entry_size[32];
        char when[32] = "unknown";
        smith_cache_format_size(smith_cache_entries[i].bytes, entry_size);
        struct tm utc;
        time_t last = smith_cache_entries[i].last_used;
        if (gmtime_r(&last, &utc) != nullptr) strftime(when, sizeof when, "%Y-%m-%d %H:%M UTC", &utc);
        printf("  %.16s  %10s  last used %s\n", smith_cache_entries[i].name, entry_size, when);
    }
    if (total > kept) printf("  and %zu more\n", total - kept);
}

static void smith_cache_show(bool verbose) {
    smith_cache_show_one(SmithCache_Modules, verbose);
    smith_cache_show_one(SmithCache_Packages, verbose);
    printf("an entry unused for %d days is removed, checked at most once a week\n",
           smith_cache_prune_after / (24 * 60 * 60));
}

// Takes the lock exclusively, or stops with a message when a build holds it. With no lock file there is no build.
// The descriptor stays open until the process ends, so no build starts while the clean runs: it waits for the lock.
static void smith_cache_exclude_builds(void) {
    char lock[PATH_MAX];
    if (!smith_cache_lock_path(lock)) return;
    int fd = open(lock, O_RDWR);
    if (fd < 0) return;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        smith_die(smith_exit_usage, "a smith build is using the cache; clean it when the build has finished", lock);
    }
}

static void smith_cache_clean_one(SmithCacheKind kind) {
    char folder[PATH_MAX];
    SmithPackageFault error;
    if (!smith_cache_folder(kind, folder, &error)) smith_die(smith_exit_usage, "cache:", error.text);
    size_t kept = 0;
    size_t total = smith_cache_scan(kind, folder, &kept);
    uint64_t bytes = 0;
    time_t newest = 0;
    if (smith_package_is_folder(folder)) smith_cache_measure(folder, &bytes, &newest);
    smith_package_remove(folder);
    char size[32];
    smith_cache_format_size(bytes, size);
    printf("removed %s: %zu %s, %s\n", folder, total, total == 1 ? "entry" : "entries", size);
}

// `smith cache [-v]` and `smith cache clean [modules|packages]`.
static int smith_cache_main(int count, char **args) {
    constexpr char usage[] = "usage: smith cache [-v] | smith cache clean [modules|packages]";
    if (count == 0 || (count == 1 && strcmp(args[0], "-v") == 0)) {
        smith_cache_show(count == 1);
        return 0;
    }
    if (strcmp(args[0], "clean") != 0 || count > 2) smith_die(smith_exit_usage, usage, nullptr);
    bool modules = count == 1 || strcmp(args[1], "modules") == 0;
    bool packages = count == 1 || strcmp(args[1], "packages") == 0;
    if (!modules && !packages) smith_die(smith_exit_usage, usage, nullptr);
    smith_cache_exclude_builds();
    if (modules) smith_cache_clean_one(SmithCache_Modules);
    if (packages) smith_cache_clean_one(SmithCache_Packages);
    return 0;
}
