// What package.c, gzip.c, xz.c and untar.c share with the rest of smith and with the build runtime, which embeds them.
#pragma once

// Include it after the system headers (stdint.h, stdio.h, limits.h...), as package.c does.

constexpr size_t smith_sha256_hex_size = 65;   // 64 digits and the terminator

constexpr size_t smith_package_hash_size = 72; // "sha256:", 64 digits and the terminator

constexpr size_t smith_package_error_size = 2048;

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

void smith_sha256_init(SmithSha256 *sha);

void smith_sha256_update(SmithSha256 *sha, const uint8_t *data, size_t size);

void smith_sha256_final(SmithSha256 *sha, char hex[smith_sha256_hex_size]);

bool smith_sha256_file(const char *path, char hex[smith_sha256_hex_size]);

typedef struct {
    char text[smith_package_error_size];
    bool wrong_hash; // the archive's bytes are not the hash the caller named (the build library's PackageKind Hash)
} SmithPackageFault;

bool smith_package_is_folder(const char *path);

bool smith_package_make_folders(const char *path);

void smith_package_remove(const char *path);

typedef enum {
    SmithCache_Modules,
    SmithCache_Packages,
    SmithCache_Lint,
    SmithCache_Steps,
} SmithCacheKind;

const char *smith_cache_name(SmithCacheKind kind);

bool smith_cache_base(char *out, SmithPackageFault *error);

bool smith_cache_folder(SmithCacheKind kind, char *out, SmithPackageFault *error);

bool smith_cache_touch(const char *path);

bool smith_cache_join(char *out, const char *folder, const char *name, const char *suffix);

bool smith_cache_older(const struct stat *info, time_t now, int seconds);

constexpr size_t smith_cache_age_steps = 2048; // 42 days; the older ones share the last step

typedef struct {
    uint64_t bytes[smith_cache_age_steps];
    uint64_t total;
} SmithCacheAges;

extern SmithCacheAges smith_cache_ages;

extern uint64_t smith_cache_walk_regular;

void smith_cache_measure(const char *path, uint64_t *bytes, time_t *newest);

uint64_t smith_cache_limit(void);

void smith_cache_ages_reset(void);

void smith_cache_ages_add(uint64_t bytes, time_t used, time_t now);

long smith_cache_cutoff(uint64_t excess);

bool smith_cache_goes(time_t used, time_t now, long cutoff);

bool smith_package_ensure(const char *url, const char *wanted, bool offline, char *folder,
                                 char hash[smith_package_hash_size], SmithPackageFault *error);

