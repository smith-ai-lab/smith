// Packages: a URL and the SHA-256 of the archive's bytes, fetched into smith's cache. One file, included by smith.c
// (`smith fetch`) and by the build runtime (`smith_package`), and embedded in smith so the runtime finds it next to
// itself in .smith/. It needs SMITH_ASSERT from its includer. See knowledge/packages.md.
//
// The cache is $XDG_CACHE_HOME/smith/packages/<hex>/, or ~/.cache/smith/packages/<hex>/, keyed by the archive's hash. A
// package is downloaded to a temporary file, verified, extracted into a temporary folder and only then renamed into
// place, so a folder named by a hash is always complete. Temporary names start with ".tmp-", which no hash does.
//
// The same folder holds the module cache (smith/modules), and one rule serves both: an entry unused for 31 days is
// removed, checked at most once a week. clang applies it to modules through the flags smith passes. smith applies it
// to packages: each use touches <hex>.used beside the package, and a build that finds the weekly check due (a
// `prune.timestamp` older than a week) removes the packages whose <hex>.used is older than 31 days. See the comment
// above smith_package_prune_entry for why that is safe beside a build that is using one.
//
// The download is smith's own HTTPS client (<smith/http.h> on io.net), which this file includes with the libraries it
// needs, so whoever includes this file needs nothing else. It finds them in the layout of the project's .smith/
// (lib/ and internal/, written by `smith build`) or of smith's own repository (../lib/).
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if __has_include("bootstrap/lib/smith/inflate.c")
#include "bootstrap/lib/smith/inflate.c"
#else
#include "../bootstrap/lib/smith/inflate.c"
#endif

#ifdef __APPLE__
#if __has_include("bootstrap/lib/smith/io.c")
#include "bootstrap/lib/smith/arena.c"
#include "bootstrap/lib/smith/contract.c"
#include "bootstrap/lib/smith/io.c"
#include "bootstrap/lib/smith/random.c"
#include "internal/http.c"
#include "lib/trace.c"
#else
#include "../bootstrap/lib/smith/arena.c"
#include "../bootstrap/lib/smith/contract.c"
#include "../bootstrap/lib/smith/io.c"
#include "../bootstrap/lib/smith/random.c"
#include "../lib/http.c"
#include "../lib/trace.c"
#endif
#else
// No HTTPS client yet (io is macOS only): the download fails with a message, and these stand in for io's type.
typedef struct {
    int unused;
} SmithIo;
static SmithIo smith_io_make(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return (SmithIo){};
}
static void smith_io_close(SmithIo *io) {
    (void)io;
}
#endif

constexpr size_t smith_sha256_hex_size = 65;   // 64 digits and the terminator
constexpr size_t smith_package_hash_size = 72; // "sha256:", 64 digits and the terminator
constexpr size_t smith_package_error_size = 2048;
constexpr size_t smith_package_read_size = 65536;

// The one pruning rule of both caches, in seconds. clang gets them as -fmodules-prune-interval and
// -fmodules-prune-after; smith applies them to packages itself.
constexpr int smith_cache_prune_interval = 604800; // checked at most once a week (7 days)
constexpr int smith_cache_prune_after = 2678400;   // an entry unused for 31 days is removed

typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} SmithSha256;

static constexpr uint32_t smith_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t smith_sha256_rotate(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
}

static void smith_sha256_init(SmithSha256 *sha) {
    static const uint32_t start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(sha->state, start, sizeof start);
    sha->bytes = 0;
    sha->used = 0;
}

static void smith_sha256_block(SmithSha256 *sha, const uint8_t *block) {
    uint32_t w[64];
    for (size_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }
    for (size_t i = 16; i < 64; i++) {
        uint32_t s0 = smith_sha256_rotate(w[i - 15], 7) ^ smith_sha256_rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = smith_sha256_rotate(w[i - 2], 17) ^ smith_sha256_rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t v[8];
    memcpy(v, sha->state, sizeof v);
    for (size_t i = 0; i < 64; i++) {
        uint32_t s1 = smith_sha256_rotate(v[4], 6) ^ smith_sha256_rotate(v[4], 11) ^ smith_sha256_rotate(v[4], 25);
        uint32_t choose = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint32_t first = v[7] + s1 + choose + smith_sha256_k[i] + w[i];
        uint32_t s0 = smith_sha256_rotate(v[0], 2) ^ smith_sha256_rotate(v[0], 13) ^ smith_sha256_rotate(v[0], 22);
        uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        uint32_t second = s0 + majority;
        memmove(v + 1, v, 7 * sizeof v[0]);
        v[4] += first;
        v[0] = first + second;
    }
    for (size_t i = 0; i < 8; i++)
        sha->state[i] += v[i];
}

static void smith_sha256_update(SmithSha256 *sha, const uint8_t *data, size_t size) {
    SMITH_ASSERT(data != nullptr || size == 0);
    sha->bytes += size;
    for (size_t at = 0; at < size;) {
        size_t take = sizeof sha->block - sha->used;
        if (take > size - at) take = size - at;
        memcpy(sha->block + sha->used, data + at, take);
        sha->used += take;
        at += take;
        if (sha->used == sizeof sha->block) {
            smith_sha256_block(sha, sha->block);
            sha->used = 0;
        }
    }
}

// The digest as 64 lowercase hex digits and a terminator.
static void smith_sha256_final(SmithSha256 *sha, char hex[smith_sha256_hex_size]) {
    uint64_t bits = sha->bytes * 8U;
    uint8_t pad[72] = {0x80};
    size_t zeros = (sha->used < 56 ? 56 : 120) - sha->used; // up to 56 bytes into a block, then the length
    smith_sha256_update(sha, pad, zeros);
    uint8_t length[8];
    for (size_t i = 0; i < 8; i++)
        length[i] = (uint8_t)(bits >> (56U - 8U * i));
    smith_sha256_update(sha, length, sizeof length);
    SMITH_ASSERT(sha->used == 0);
    for (size_t i = 0; i < 8; i++) {
        int written = snprintf(hex + i * 8, 9, "%08x", sha->state[i]);
        SMITH_ASSERT(written == 8);
    }
}

// The SHA-256 of a file's bytes. False when it cannot be read.
static bool smith_sha256_file(const char *path, char hex[smith_sha256_hex_size]) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    static uint8_t buffer[smith_package_read_size];
    SmithSha256 sha;
    smith_sha256_init(&sha);
    bool ok = true;
    for (uint64_t chunks = 0; chunks < (UINT64_C(1) << 40) / smith_package_read_size && ok; chunks++) {
        ssize_t got = read(fd, buffer, sizeof buffer);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) ok = false;
        if (got <= 0) break;
        smith_sha256_update(&sha, buffer, (size_t)got);
    }
    close(fd);
    if (ok) smith_sha256_final(&sha, hex);
    return ok;
}

typedef struct {
    char text[smith_package_error_size];
} SmithPackageError;

[[gnu::format(printf, 2, 3)]] static bool smith_package_fail(SmithPackageError *error, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = vsnprintf(error->text, sizeof error->text, format, args);
    va_end(args);
    SMITH_ASSERT(written > 0);
    return false;
}

static bool smith_package_is_folder(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

// mkdir -p. False when a part cannot be made.
static bool smith_package_make_folders(const char *path) {
    char partial[PATH_MAX];
    int written = snprintf(partial, sizeof partial, "%s", path);
    SMITH_ASSERT(written > 0 && (size_t)written < sizeof partial);
    for (size_t i = 1; i <= (size_t)written; i++) {
        if (partial[i] != '/' && partial[i] != '\0') continue;
        partial[i] = '\0';
        if (mkdir(partial, 0777) != 0 && errno != EEXIST) return false;
        if (i < (size_t)written) partial[i] = '/';
    }
    return smith_package_is_folder(path);
}

static int smith_package_remove_entry(const char *path, const struct stat *info, int type, struct FTW *where) {
    (void)info;
    (void)type;
    (void)where;
    return remove(path);
}

// rm -rf, best effort: a path that is not there is fine.
static void smith_package_remove(const char *path) {
    struct stat info;
    if (lstat(path, &info) != 0) return;
    if (nftw(path, smith_package_remove_entry, 64, FTW_DEPTH | FTW_PHYS) != 0) remove(path);
}

// The archive readers, in smith's own code, so extraction runs no other process: gzip (on smith::inflate), then xz,
// then tar (which uses both). The order matters, so each include is a block of its own that clang-format leaves
// be.
#include "gzip.c"

#include "xz.c"

#include "untar.c"

#ifdef __APPLE__
// The only place a package is downloaded: smith's HTTP client, https only (and file:// for tests), also after a
// redirect. The body goes to the destination as it arrives. A failure's message is the client's, naming the URL.
static bool smith_package_download(SmithIo *io, const char *url, const char *destination, SmithPackageError *error) {
    SMITH_ASSERT(io != nullptr);
    SmithHttpResult result = smith_http_get(io, url, destination);
    if (result.error.code == 0) return true;
    return smith_package_fail(error, "cannot download %s\n  %s", url, result.error.message);
}
#else
static bool smith_package_download(SmithIo *io, const char *url, const char *destination, SmithPackageError *error) {
    (void)io;
    (void)destination;
    return smith_package_fail(error, "cannot download %s: smith has no HTTPS client on this platform yet", url);
}
#endif

typedef enum {
    SmithCache_Modules,
    SmithCache_Packages,
} SmithCacheKind;

static const char *smith_cache_name(SmithCacheKind kind) {
    return kind == SmithCache_Modules ? "modules" : "packages";
}

// $XDG_CACHE_HOME/smith, or ~/.cache/smith: the folder of every smith cache.
static bool smith_cache_base(char *out, SmithPackageError *error) {
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    int written = 0;
    if (xdg != nullptr && *xdg != '\0') {
        if (xdg[0] != '/') return smith_package_fail(error, "XDG_CACHE_HOME is not an absolute path: %s", xdg);
        written = snprintf(out, PATH_MAX, "%s/smith", xdg);
    } else if (home != nullptr && home[0] == '/') {
        written = snprintf(out, PATH_MAX, "%s/.cache/smith", home);
    } else {
        return smith_package_fail(error, "no cache folder: set XDG_CACHE_HOME or HOME");
    }
    if (written <= 0 || written >= PATH_MAX) return smith_package_fail(error, "the cache path is too long");
    return true;
}

// The folder of one cache: <base>/modules or <base>/packages.
static bool smith_cache_folder(SmithCacheKind kind, char *out, SmithPackageError *error) {
    char base[PATH_MAX];
    if (!smith_cache_base(base, error)) return false;
    int written = snprintf(out, PATH_MAX, "%s/%s", base, smith_cache_name(kind));
    if (written <= 0 || written >= PATH_MAX) return smith_package_fail(error, "the cache path is too long");
    return true;
}

static bool smith_package_root(char *out, SmithPackageError *error) {
    return smith_cache_folder(SmithCache_Packages, out, error);
}

// Creates the file or sets its times to now. Best effort: a cache folder that cannot be written is not an error here.
static bool smith_cache_touch(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
    if (fd < 0) return false;
    bool ok = futimens(fd, nullptr) == 0;
    close(fd);
    return ok;
}

static bool smith_cache_join(char *out, const char *folder, const char *name, const char *suffix) {
    int written = snprintf(out, PATH_MAX, "%s/%s%s", folder, name, suffix);
    return written > 0 && written < PATH_MAX;
}

static bool smith_cache_older(const struct stat *info, time_t now, int seconds) {
    return now - info->st_mtime > seconds;
}

// Records that a build uses the package <hex>: <packages>/<hex>.used.
static void smith_package_use(const char *packages, const char *digits) {
    char stamp[PATH_MAX];
    if (smith_cache_join(stamp, packages, digits, ".used")) smith_cache_touch(stamp);
}

// Prunes one entry of the packages folder.
//
// Safety beside a build that uses a package. A build touches <hex>.used BEFORE it looks for the folder <hex>, and a
// package is removed only after it is renamed out of the way and its stamp is read AGAIN. So when a build and a prune
// meet, one of two things happens. The build's look comes first: its touch came before that, the prune's second read
// sees the fresh stamp, and the package is renamed back, so it is never removed. Or the prune's rename comes first: the
// build finds no folder and fetches the package again. Either way the build gets a complete folder, and a package a
// build touched in the last 31 days is never removed. Nothing named ".tmp-" (a fetch in progress) is removed unless it
// is itself older than 31 days. A package with no stamp (made before pruning existed) is stamped now, not removed.
static void smith_package_prune_entry(const char *packages, const char *name, time_t now) {
    char path[PATH_MAX];
    struct stat info;
    if (strcmp(name, "prune.timestamp") == 0 || !smith_cache_join(path, packages, name, "")) return;
    if (lstat(path, &info) != 0) return;
    size_t length = strlen(name);
    if (strncmp(name, ".tmp-", 5) == 0) {
        if (smith_cache_older(&info, now, smith_cache_prune_after)) smith_package_remove(path);
        return;
    }
    if (length > 5 && strcmp(name + length - 5, ".used") == 0) { // a stamp whose package is gone
        char folder[PATH_MAX];
        snprintf(folder, sizeof folder, "%s", path);
        folder[strlen(folder) - 5] = '\0';
        if (!smith_package_is_folder(folder) && smith_cache_older(&info, now, smith_cache_prune_after)) remove(path);
        return;
    }
    char stamp[PATH_MAX];
    char parked[PATH_MAX];
    int stamp_length = snprintf(stamp, sizeof stamp, "%s.used", path);
    int parked_length = snprintf(parked, sizeof parked, "%s/.tmp-prune-%d-%s", packages, (int)getpid(), name);
    if (!S_ISDIR(info.st_mode) || stamp_length >= PATH_MAX || parked_length >= PATH_MAX) return;
    struct stat used;
    if (stat(stamp, &used) != 0) {
        smith_cache_touch(stamp);
        return;
    }
    if (!smith_cache_older(&used, now, smith_cache_prune_after)) return;
    if (rename(path, parked) != 0) return;
    if (stat(stamp, &used) == 0 && !smith_cache_older(&used, now, smith_cache_prune_after)) {
        if (rename(parked, path) != 0) smith_package_remove(parked);
        return;
    }
    smith_package_remove(parked);
    remove(stamp);
}

// The weekly check, as part of every smith_package. When it is not due this is one stat of prune.timestamp. When it is
// due, the timestamp is touched first, so a build that starts meanwhile skips the check, and then each entry is looked
// at once.
static void smith_package_prune(const char *packages) {
    char marker[PATH_MAX];
    if (!smith_cache_join(marker, packages, "prune.timestamp", "")) return;
    time_t now = time(nullptr);
    struct stat info;
    if (stat(marker, &info) == 0 && !smith_cache_older(&info, now, smith_cache_prune_interval)) return;
    if (!smith_cache_touch(marker)) return; // no packages folder yet
    DIR *dir = opendir(packages);
    if (dir == nullptr) return;
    for (size_t i = 0; i < 1000000; i++) {
        struct dirent *entry = readdir(dir);
        if (entry == nullptr) break;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        smith_package_prune_entry(packages, entry->d_name, now);
    }
    closedir(dir);
}

// "sha256:<64 hex digits>" to its lowercase digits. False when it is not that.
static bool smith_package_parse_hash(const char *hash, char digits[smith_sha256_hex_size]) {
    if (hash == nullptr || strncmp(hash, "sha256:", 7) != 0 || strlen(hash) != 7 + 64) return false;
    for (size_t i = 0; i < 64; i++) {
        char c = hash[7 + i];
        if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        digits[i] = c;
    }
    digits[64] = '\0';
    return true;
}

// The folder to move into place: the archive's one top-level folder when it has exactly one entry (as `zig fetch` and
// tar --strip-components=1 do), else the extraction folder itself. `single` says which.
static bool smith_package_contents(const char *extract, char *contents, bool *single, SmithPackageError *error) {
    DIR *dir = opendir(extract);
    if (dir == nullptr) return smith_package_fail(error, "cannot read %s", extract);
    size_t entries = 0;
    char only[PATH_MAX] = "";
    for (size_t i = 0; i < 1000000; i++) {
        struct dirent *entry = readdir(dir);
        if (entry == nullptr) break;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        entries++;
        if (entries == 1) snprintf(only, sizeof only, "%s/%s", extract, entry->d_name);
    }
    closedir(dir);
    if (entries == 0) return smith_package_fail(error, "the archive is empty");
    struct stat info;
    *single = entries == 1 && lstat(only, &info) == 0 && S_ISDIR(info.st_mode);
    int written = snprintf(contents, PATH_MAX, "%s", *single ? only : extract);
    SMITH_ASSERT(written > 0 && written < PATH_MAX);
    return true;
}

typedef struct {
    char packages[PATH_MAX]; // the cache folder
    char download[PATH_MAX]; // the temporary archive
    char extract[PATH_MAX];  // the temporary folder
} SmithPackageTemps;

static void smith_package_temps(SmithPackageTemps *temps) {
    int a = snprintf(temps->download, PATH_MAX, "%s/.tmp-%d-archive", temps->packages, (int)getpid());
    int b = snprintf(temps->extract, PATH_MAX, "%s/.tmp-%d-extract", temps->packages, (int)getpid());
    SMITH_ASSERT(a > 0 && a < PATH_MAX && b > 0 && b < PATH_MAX);
    smith_package_remove(temps->download); // left by an earlier process with this pid
    smith_package_remove(temps->extract);
}

// The name a gzip of one file gets: the last part of the URL (no query or fragment) without ".gz".
static void smith_package_file_name(const char *url, char *name, size_t size) {
    size_t length = strcspn(url, "?#");
    size_t start = length;
    while (start > 0 && url[start - 1] != '/')
        start--;
    size_t count = length - start;
    if (count > 3 && memcmp(url + length - 3, ".gz", 3) == 0) count -= 3;
    if (count == 0 || count >= size || (count == 1 && url[start] == '.') ||
        (count == 2 && url[start] == '.' && url[start + 1] == '.')) {
        snprintf(name, size, "file");
        return;
    }
    memcpy(name, url + start, count);
    name[count] = '\0';
}

// Verifies the downloaded archive, extracts it and moves it to its folder. The temporaries are the caller's to remove.
static bool smith_package_install(const char *url, const char *wanted, const SmithPackageTemps *temps, char *folder,
                                  char hash[smith_package_hash_size], SmithPackageError *error) {
    char digits[smith_sha256_hex_size];
    if (!smith_sha256_file(temps->download, digits))
        return smith_package_fail(error, "cannot read the download of %s", url);
    int written = snprintf(hash, smith_package_hash_size, "sha256:%s", digits);
    SMITH_ASSERT(written == 71);
    if (wanted != nullptr && strcmp(wanted, hash) != 0) {
        return smith_package_fail(error, "wrong hash for %s\n  expected %s\n  actual   %s", url, wanted, hash);
    }
    written = snprintf(folder, PATH_MAX, "%s/%s", temps->packages, digits);
    SMITH_ASSERT(written > 0 && written < PATH_MAX);
    if (smith_package_is_folder(folder)) return true;
    if (mkdir(temps->extract, 0777) != 0) return smith_package_fail(error, "cannot make %s", temps->extract);
    SmithExtracted extracted;
    char name[256];
    smith_package_file_name(url, name, sizeof name);
    if (!smith_package_extract(temps->download, name, temps->extract, &extracted, error)) {
        char reason[smith_package_error_size];
        snprintf(reason, sizeof reason, "%s", error->text);
        return smith_package_fail(error, "cannot extract %s: %s", url, reason);
    }
    char contents[PATH_MAX];
    bool single = false;
    if (!smith_package_contents(temps->extract, contents, &single, error)) return false;
    if (single && extracted.climbing[0] != '\0') {
        return smith_package_fail(error, "cannot extract %s: the symlink \"%.200s\" points outside the package folder",
                                  url, extracted.climbing);
    }
    if (rename(contents, folder) != 0 && !smith_package_is_folder(folder)) { // another process may have won
        return smith_package_fail(error, "cannot move the package into %s: %s", folder, strerror(errno));
    }
    return true;
}

// Makes sure the package is in the cache and puts its absolute path in folder (PATH_MAX bytes). The io is what the
// download uses (smith_io_make, or a test's io with a scripted network); with `offline` nothing is downloaded, so it
// may be nullptr. With `wanted` (the "sha256:<hex>" a build.c names) the download must match it, and a package already
// cached is not fetched again. With nullptr (`smith fetch`) the archive is always downloaded and its hash is the
// answer, put in `hash` either way. `offline` turns a missing package into an error that names the URL.
static bool smith_package_ensure(SmithIo *io, const char *url, const char *wanted, bool offline, char *folder,
                                 char hash[smith_package_hash_size], SmithPackageError *error) {
    SmithPackageTemps temps;
    if (!smith_package_root(temps.packages, error)) return false;
    char digits[smith_sha256_hex_size];
    char expected[smith_package_hash_size]; // a copy: install writes the actual hash into `hash`
    if (wanted != nullptr) {
        if (!smith_package_parse_hash(wanted, digits)) {
            return smith_package_fail(error, "the hash of %s must be sha256: and 64 hex digits, not \"%s\"", url,
                                      wanted);
        }
        int written = snprintf(folder, PATH_MAX, "%s/%s", temps.packages, digits);
        SMITH_ASSERT(written > 0 && written < PATH_MAX);
        snprintf(expected, sizeof expected, "sha256:%s", digits);
        snprintf(hash, smith_package_hash_size, "%s", expected);
        smith_package_use(temps.packages, digits); // before the look below, and before a prune: see the prune's comment
        smith_package_prune(temps.packages);
        if (smith_package_is_folder(folder)) return true;
        if (offline) return smith_package_fail(error, "%s is not in the cache and -DOFFLINE forbids fetching it", url);
    }
    if (!smith_package_make_folders(temps.packages)) {
        return smith_package_fail(error, "cannot make the cache folder %s", temps.packages);
    }
    if (wanted == nullptr) smith_package_prune(temps.packages);
    smith_package_temps(&temps);
    bool ok = smith_package_download(io, url, temps.download, error) &&
              smith_package_install(url, wanted != nullptr ? expected : nullptr, &temps, folder, hash, error);
    smith_package_remove(temps.download);
    smith_package_remove(temps.extract);
    if (ok) smith_package_use(temps.packages, hash + 7);
    return ok;
}
