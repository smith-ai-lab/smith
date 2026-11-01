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

// Where the listing and the "removed" lines go: stdout, or the memory of smith_core_cache.
static FILE *smith_cache_out;

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
    if (kind == SmithCache_Steps) { // m/ and r/ hold the entries by hash: the size is what there is to show
        uint64_t used = 0;
        time_t last = 0;
        char text[32];
        if (smith_package_is_folder(folder)) smith_cache_measure(folder, &used, &last);
        smith_cache_format_size(used, text);
        char cap[32];
        smith_cache_format_size(smith_cache_limit(), cap);
        fprintf(smith_cache_out, "steps: %s\n  %s (cap %s)\n", folder, text, cap);
        return;
    }
    size_t kept = 0;
    size_t total = smith_cache_scan(kind, folder, &kept);
    uint64_t bytes = 0;
    time_t newest = 0;
    if (smith_package_is_folder(folder)) smith_cache_measure(folder, &bytes, &newest);
    char size[32];
    smith_cache_format_size(bytes, size);
    char cap[32];
    smith_cache_format_size(smith_cache_limit(), cap);
    fprintf(smith_cache_out, "%s: %s\n  %zu %s, %s (cap %s)\n", smith_cache_name(kind), folder, total,
            total == 1 ? "entry" : "entries", size, cap);
    if (!verbose) return;
    for (size_t i = 0; i < kept; i++) {
        char entry_size[32];
        char when[32] = "unknown";
        smith_cache_format_size(smith_cache_entries[i].bytes, entry_size);
        struct tm utc;
        time_t last = smith_cache_entries[i].last_used;
        if (gmtime_r(&last, &utc) != nullptr) strftime(when, sizeof when, "%Y-%m-%d %H:%M UTC", &utc);
        fprintf(smith_cache_out, "  %.16s  %10s  last used %s\n", smith_cache_entries[i].name, entry_size, when);
    }
    if (total > kept) fprintf(smith_cache_out, "  and %zu more\n", total - kept);
}

// The lint cache (runtime/lint_cache.c) is files in two levels of folders, so it is counted by its files.
static void smith_cache_show_lint(void) {
    char folder[PATH_MAX];
    SmithPackageFault error;
    if (!smith_cache_folder(SmithCache_Lint, folder, &error)) smith_die(smith_exit_usage, "cache:", error.text);
    uint64_t bytes = 0;
    time_t newest = 0;
    uint64_t files = 0;
    if (smith_package_is_folder(folder)) {
        smith_cache_measure(folder, &bytes, &newest);
        files = smith_cache_walk_regular;
    }
    char size[32];
    smith_cache_format_size(bytes, size);
    fprintf(smith_cache_out, "lint: %s\n  %llu %s, %s\n", folder, (unsigned long long)files,
            files == 1 ? "file" : "files", size);
}

static void smith_cache_show(bool verbose) {
    smith_cache_show_one(SmithCache_Modules, verbose);
    smith_cache_show_one(SmithCache_Packages, verbose);
    smith_cache_show_lint();
    smith_cache_show_one(SmithCache_Steps, verbose);
    fprintf(smith_cache_out, "an entry unused for %d days is removed, checked at most once a week\n",
            smith_cache_prune_after / (24 * 60 * 60));
}

// The descriptor that holds the lock exclusively: open until the process ends (no build starts while the clean runs: it
// waits for the lock), or until smith_core_cache closes it.
static int smith_cache_exclusive_fd = -1;

// Takes the lock exclusively and returns true, or false (and its path) when a build holds it. With no lock file there
// is no build.
static bool smith_cache_exclude_builds(char lock[PATH_MAX]) {
    if (!smith_cache_lock_path(lock)) return true;
    int fd = open(lock, O_RDWR);
    if (fd < 0) return true;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return false;
    }
    smith_cache_exclusive_fd = fd;
    return true;
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
    fprintf(smith_cache_out, "removed %s: %zu %s, %s\n", folder, total, total == 1 ? "entry" : "entries", size);
}

static void smith_cache_clean_lint(void) {
    char folder[PATH_MAX];
    SmithPackageFault error;
    if (!smith_cache_folder(SmithCache_Lint, folder, &error)) smith_die(smith_exit_usage, "cache:", error.text);
    uint64_t bytes = 0;
    time_t newest = 0;
    uint64_t files = 0;
    if (smith_package_is_folder(folder)) {
        smith_cache_measure(folder, &bytes, &newest);
        files = smith_cache_walk_regular;
    }
    smith_package_remove(folder);
    char size[32];
    smith_cache_format_size(bytes, size);
    fprintf(smith_cache_out, "removed %s: %llu %s, %s\n", folder, (unsigned long long)files,
            files == 1 ? "file" : "files", size);
}

constexpr char smith_cache_usage[] = "usage: smith cache [-v] | smith cache clean [modules|packages|lint|steps]";
constexpr char smith_cache_busy[] = "a smith build is using the cache; clean it when the build has finished";

// Whether the arguments are one of the forms of the usage line.
static bool smith_cache_args_ok(int count, char **args) {
    if (count == 0 || (count == 1 && strcmp(args[0], "-v") == 0)) return true;
    if (strcmp(args[0], "clean") != 0 || count > 2) return false;
    return count == 1 || strcmp(args[1], "modules") == 0 || strcmp(args[1], "packages") == 0 ||
           strcmp(args[1], "lint") == 0 || strcmp(args[1], "steps") == 0;
}

// `smith cache [-v]` and `smith cache clean [modules|packages|lint]`.
static int smith_cache_main(int count, char **args) {
    if (smith_cache_out == nullptr) smith_cache_out = stdout;
    if (!smith_cache_args_ok(count, args)) smith_die(smith_exit_usage, smith_cache_usage, nullptr);
    if (count == 0 || (count == 1 && strcmp(args[0], "-v") == 0)) {
        smith_cache_show(count == 1);
        return 0;
    }
    bool modules = count == 1 || strcmp(args[1], "modules") == 0;
    bool packages = count == 1 || strcmp(args[1], "packages") == 0;
    bool lint = count == 1 || strcmp(args[1], "lint") == 0;
    bool steps = count == 1 || strcmp(args[1], "steps") == 0;
    char lock[PATH_MAX];
    if (!smith_cache_exclude_builds(lock)) smith_die(smith_exit_usage, smith_cache_busy, lock);
    if (modules) smith_cache_clean_one(SmithCache_Modules);
    if (packages) smith_cache_clean_one(SmithCache_Packages);
    if (lint) smith_cache_clean_lint();
    if (steps) smith_cache_clean_one(SmithCache_Steps);
    return 0;
}

#ifdef SMITH_CORE
// include/smith_core_cache.h: `smith cache` in process on an explicit cache folder. What the verb would end the process
// for is returned as its status (2) with the message in `out`.
int smith_core_cache(const char *xdg_cache_home, const char *home, int count, char **args, char *out, size_t out_size) {
    smith_core_environment.given = true;
    smith_core_environment.xdg = xdg_cache_home;
    smith_core_environment.home = home;
    int status = 0;
    char text[PATH_MAX + 128] = "";
    char base[PATH_MAX];
    SmithPackageFault fault = {};
    char *memory = nullptr;
    size_t length = 0;
    FILE *stream = open_memstream(&memory, &length);
    if (stream == nullptr) {
        snprintf(out, out_size, "cannot make a stream");
        smith_core_environment.given = false;
        return 1;
    }
    char lock[PATH_MAX];
    if (!smith_cache_args_ok(count, args)) {
        snprintf(text, sizeof text, "%s\n", smith_cache_usage);
        status = smith_exit_usage;
    } else if (!smith_cache_base(base, &fault)) {
        snprintf(text, sizeof text, "smith: cache: %s\n", fault.text);
        status = smith_exit_usage;
    } else if (count == 0 || (count == 1 && strcmp(args[0], "-v") == 0)) {
        smith_cache_out = stream;
        smith_cache_show(count == 1);
    } else if (!smith_cache_exclude_builds(lock)) {
        snprintf(text, sizeof text, "smith: %s: %s\n", smith_cache_busy, lock);
        status = smith_exit_usage;
    } else {
        smith_cache_out = stream;
        bool all = count == 1;
        if (all || strcmp(args[1], "modules") == 0) smith_cache_clean_one(SmithCache_Modules);
        if (all || strcmp(args[1], "packages") == 0) smith_cache_clean_one(SmithCache_Packages);
        if (all || strcmp(args[1], "lint") == 0) smith_cache_clean_lint();
        if (all || strcmp(args[1], "steps") == 0) smith_cache_clean_one(SmithCache_Steps);
    }
    smith_cache_out = nullptr;
    fputs(text, stream);
    fclose(stream);
    if (smith_cache_exclusive_fd >= 0) close(smith_cache_exclusive_fd); // the lock goes with it
    smith_cache_exclusive_fd = -1;
    snprintf(out, out_size, "%s", memory != nullptr ? memory : "");
    free(memory);
    smith_core_environment.given = false;
    smith_core_environment.xdg = nullptr;
    smith_core_environment.home = nullptr;
    return status;
}
#endif
