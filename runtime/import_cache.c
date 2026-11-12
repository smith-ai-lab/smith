// The disk cache of the new front end's imports (#2891, knowledge/front-end.md "From disk"). One process of #2888
// reads std and each C header once; this keeps what it read on disk, so the next process, a CI job or another machine
// with the same smith reads it from here.
//
// An entry is an answer in the one store (#3177, runtime/executor_cache.c, `steps/a/`, whose prune and cap apply): its
// key is the SHA-256 of the first 16 hex digits of the import identity `smith build` hashed (build_import_identity:
// every file under front/ and lib/std/, shim/cimport*, the runtime's sources, build.c, the LLVM pack's identity, the
// compile flags and the format's version), the entry's kind and `<key>`, so a smith built from other sources reads none
// of another's entries and a format change needs nothing more; `<kind>` is `h` (a C header's words, shim/cimport.cpp)
// or `m` (a module's parse tree, front/module_cache.smith); `<key>` is what the caller hashed with
// smith_import_cache_key, which never holds a time, a machine's name or a folder outside the project. An entry is a
// head (`SMIC`, the version, the payload's size and a hash of it) and the payload; one that does not read back whole
// with its hash is a miss and is written again, never an error. A smith with no identity (a build without LLVM, a test
// runner) has the cache off.
//
// A hit touches its entry when the entry is more than a day old.
#include "import_cache.h"
#include "core.h"
#include "executor_cache.h"
#include "package.h"

#include <sys/stat.h>
#include <utime.h>

constexpr uint32_t smith_ic_magic = 0x43494d53; // "SMIC"
constexpr uint32_t smith_ic_version = 1;
constexpr size_t smith_ic_head = 32; // magic, version, size (8), hash (8), 8 spare
constexpr time_t smith_ic_day = 86400;

typedef struct {
    bool asked;
    char identity[17]; // the first 16 hex digits of the import identity, or "" when the cache is off
    uint32_t hits[2];
    uint32_t misses[2];
} SmithImportCache;

static SmithImportCache smith_ic;

// A hash of `size` bytes from `seed`: an entry's finds a damaged or truncated payload, and two seeds make a key. Not
// cryptographic: the entries come from this machine or from CI, never from a stranger. Four lanes, so the multiplies do
// not wait on each other (an image is megabytes, and smith's dev build is -O0).
// MurmurHash3's finalizer: every bit of `x` moves every bit of the result.
static uint64_t smith_ic_mix(uint64_t x) {
    x ^= x >> 33;
    x *= 0xFF51AFD7ED558CCDULL;
    x ^= x >> 33;
    x *= 0xC4CEB9FE1A85EC53ULL;
    return x ^ (x >> 33);
}

static uint64_t smith_ic_hash(const uint8_t *data, size_t size, uint64_t seed) {
    uint64_t lane[4] = {0x9E3779B97F4A7C15ULL ^ size, seed, seed ^ 0xC4CEB9FE1A85EC53ULL, ~seed};
    size_t at = 0;
    for (; at + 32 <= size; at += 32) { // bounded by size
        uint64_t w[4];
        memcpy(w, data + at, sizeof w);
        lane[0] = (lane[0] ^ w[0]) * 0xFF51AFD7ED558CCDULL;
        lane[1] = (lane[1] ^ w[1]) * 0xC4CEB9FE1A85EC53ULL;
        lane[2] = (lane[2] ^ w[2]) * 0x9E3779B97F4A7C15ULL;
        lane[3] = (lane[3] ^ w[3]) * 0xD6E8FEB86659FD93ULL;
        lane[0] ^= lane[0] >> 31;
        lane[1] ^= lane[1] >> 31;
        lane[2] ^= lane[2] >> 31;
        lane[3] ^= lane[3] >> 31;
    }
    uint64_t h = seed;
    for (size_t k = 0; k < 4; k++) // bounded by 4
        h = smith_ic_mix(h ^ smith_ic_mix(lane[k] + k));
    for (; at < size; at++) // bounded by 32
        h = (h ^ data[at]) * 0x100000001B3ULL;
    return smith_ic_mix(h);
}

// The first 8 bytes of every entry: the magic and the version.
static uint64_t smith_ic_identity(void) {
    return (uint64_t)smith_ic_magic | ((uint64_t)smith_ic_version << 32);
}

static void smith_ic_put64(uint8_t *out, uint64_t value) {
    for (size_t i = 0; i < 8; i++) // bounded by 8
        out[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t smith_ic_take64(const uint8_t *in) {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; i++) // bounded by 8
        value |= (uint64_t)in[i] << (8 * i);
    return value;
}

static void smith_ic_touch_when_old(const char *path, time_t now) {
    struct stat info;
    if (stat(path, &info) == 0 && now - info.st_mtime > smith_ic_day) utime(path, nullptr);
}

#ifdef SMITH_IMPORT_IDENTITY_LINKED
extern const char smith_import_identity[]; // runtime/import_identity.c
#endif

// The identity `smith build` compiled in, or nullptr.
static const char *smith_ic_build_identity(void) {
#ifdef SMITH_IMPORT_IDENTITY_LINKED
    if (strlen(smith_import_identity) >= 16) return smith_import_identity;
#endif
    return nullptr;
}

// The identity of this smith's entries (16 hex digits), or "" when the cache is off ($SMITH_IMPORT_CACHE=0) or this
// smith has none: the callers ask it whether the cache is on. Worked out once a process.
const char *smith_import_cache_folder(void) {
    if (smith_ic.asked) return smith_ic.identity;
    smith_ic.asked = true;
    smith_ic.identity[0] = '\0';
    const char *given = getenv("SMITH_IMPORT_CACHE");
    if (given != nullptr && strcmp(given, "0") == 0) return smith_ic.identity;
    const char *identity = smith_ic_build_identity();
    if (identity != nullptr) snprintf(smith_ic.identity, sizeof smith_ic.identity, "%.16s", identity);
    return smith_ic.identity;
}

void smith_import_cache_key(const void *material, size_t size, char key[smith_import_cache_key_size]) {
    snprintf(key, smith_import_cache_key_size, "%016llx%016llx",
             (unsigned long long)smith_ic_hash((const uint8_t *)material, size, 0x243F6A8885A308D3ULL),
             (unsigned long long)smith_ic_hash((const uint8_t *)material, size, 0x13198A2E03707344ULL));
}

// The address of an entry in the store: the SHA-256 of the identity, the kind and the key; false when the cache is off
// or the kind or key is not one this file makes.
static bool smith_ic_address(const char *kind, const char *key, char hex[smith_sha256_hex_size]) {
    const char *identity = smith_import_cache_folder();
    if (identity[0] == '\0' || strlen(kind) != 1 || strlen(key) != smith_import_cache_key_size - 1) return false;
    for (size_t i = 0; key[i] != '\0'; i++) // bounded by the key's 32 digits
        if (!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f'))) return false;
    const char *const parts[] = {"smith-import 2", identity, kind, key};
    SmithSha256 sha;
    smith_sha256_init(&sha);
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) // bounded by the parts
        smith_sha256_update(&sha, (const uint8_t *)parts[i], strlen(parts[i]) + 1);
    smith_sha256_final(&sha, hex);
    return true;
}

// The file of an entry in the store; false when the cache is off or the kind or key is not one this file makes.
static bool smith_ic_path(const char *kind, const char *key, char *out) {
    char hex[smith_sha256_hex_size];
    return smith_ic_address(kind, key, hex) && smith_stepcache_answer_file(hex, out);
}

size_t smith_import_cache_load(const char *kind, const char *key, uint8_t *out, size_t capacity) {
    char path[PATH_MAX];
    if (!smith_ic_path(kind, key, path)) return SIZE_MAX;
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return SIZE_MAX;
    uint8_t head[smith_ic_head];
    size_t result = SIZE_MAX;
    if (fread(head, 1, sizeof head, file) == sizeof head && smith_ic_take64(head) == smith_ic_identity()) {
        uint64_t size = smith_ic_take64(head + 8);
        uint64_t hash = smith_ic_take64(head + 16);
        if (size > capacity || out == nullptr) {
            result = size < SIZE_MAX ? (size_t)size : SIZE_MAX;
        } else if (fread(out, 1, size, file) == size && fgetc(file) == EOF && smith_ic_hash(out, size, 0) == hash) {
            result = size;
            smith_ic_touch_when_old(path, time(nullptr));
        }
    }
    fclose(file);
    return result;
}

void smith_import_cache_store(const char *kind, const char *key, const uint8_t *data, size_t size) {
    char hex[smith_sha256_hex_size];
    if (!smith_ic_address(kind, key, hex)) return;
    uint8_t head[smith_ic_head] = {};
    smith_ic_put64(head, smith_ic_identity());
    smith_ic_put64(head + 8, size);
    smith_ic_put64(head + 16, smith_ic_hash(data, size, 0));
    smith_stepcache_answer_put_parts(hex, (const char *)head, sizeof head, (const char *)data, size);
}

void smith_import_cache_count(const char *kind, bool hit) {
    size_t at = kind[0] == 'h' ? 0 : 1;
    if (hit) {
        smith_ic.hits[at] += 1;
    } else {
        smith_ic.misses[at] += 1;
    }
}

void smith_import_cache_report(void) {
    const char *log = getenv("SMITH_IMPORT_CACHE_LOG");
    if (log == nullptr || *log == '\0') return;
    if (smith_ic.hits[0] + smith_ic.misses[0] + smith_ic.hits[1] + smith_ic.misses[1] == 0) return;
    FILE *file = fopen(log, "a");
    if (file == nullptr) return;
    fprintf(file, "headers %u %u modules %u %u\n", smith_ic.hits[0], smith_ic.misses[0], smith_ic.hits[1],
            smith_ic.misses[1]);
    fclose(file);
    // and the identity folder this process used, one line in `<log>.identity` (#2978): the save drops every other
    // folder before it publishes. A smith with no identity writes none, and the save then drops nothing.
    const char *identity = smith_ic_build_identity();
    char named[PATH_MAX];
    if (identity == nullptr || snprintf(named, sizeof named, "%s.identity", log) >= (int)sizeof named) return;
    file = fopen(named, "a");
    if (file == nullptr) return;
    fprintf(file, "%.16s\n", identity);
    fclose(file);
}

// ---------------------------------------------------------------- the image of a module's parse tree

#include <sys/mman.h>
#ifndef MAP_POPULATE
#define MAP_POPULATE 0 // macOS: the pages are read as they are touched
#endif
#ifdef __linux__
#include <link.h>
#endif

constexpr size_t smith_ic_maps_max = 8192;
constexpr uint32_t smith_ic_image_magic = 0x474d4953; // "SIMG"
constexpr size_t smith_ic_image_head = 32;            // magic and moves, size, root, fixed: the image is 16-aligned

typedef struct {
    uintptr_t start;
    uintptr_t end;
} SmithIcRange;

static SmithIcRange smith_ic_maps[smith_ic_maps_max];

// Where smith's own executable has its read-only parts: a tree's pointer to one of its texts (a literal the parser
// names) is stored as an offset from `bias` with the 16 bytes it points at, and a load moves it to this process's
// program only when the same 16 bytes are there (the identity says the sources are the same, not that the link laid
// them out the same). A pointer into a part the program writes is never stored.
constexpr size_t smith_ic_parts_max = 16;
constexpr size_t smith_ic_check = 16;

static struct {
    uintptr_t bias;
    size_t count;
    SmithIcRange parts[smith_ic_parts_max];
    bool found;
} smith_ic_program;

#ifdef __linux__
static int smith_ic_program_part(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    (void)data;
    smith_ic_program.bias = info->dlpi_addr;
    for (size_t i = 0; i < info->dlpi_phnum && smith_ic_program.count < smith_ic_parts_max; i++) { // bounded
        const ElfW(Phdr) *part = &info->dlpi_phdr[i];
        if (part->p_type != PT_LOAD || (part->p_flags & PF_W) != 0) continue;
        uintptr_t from = info->dlpi_addr + part->p_vaddr;
        smith_ic_program.parts[smith_ic_program.count++] = (SmithIcRange){from, from + part->p_memsz};
    }
    return 1; // the first object is the program itself
}
#endif

static bool smith_ic_find_program(void) {
#ifdef __linux__
    if (!smith_ic_program.found) dl_iterate_phdr(smith_ic_program_part, nullptr);
#endif
    smith_ic_program.found = true;
    return smith_ic_program.count > 0;
}

// Whether `value` and the 16 bytes from it are in a read-only part of the program.
static bool smith_ic_in_program(uint64_t value) {
    for (size_t i = 0; i < smith_ic_program.count; i++) { // bounded by the parts
        const SmithIcRange *part = &smith_ic_program.parts[i];
        if (value >= part->start && value < part->end && part->end - value >= smith_ic_check) return true;
    }
    return false;
}

// Why an image is not stored, printed when $SMITH_IMPORT_CACHE is `why`; always false.
static bool smith_ic_refuse(const char *reason, size_t at, uint64_t word) {
    const char *setting = getenv("SMITH_IMPORT_CACHE");
    if (setting != nullptr && strcmp(setting, "why") == 0)
        fprintf(stderr, "smith: import cache: not stored: %s (byte %zu, word 0x%llx)\n", reason, at,
                (unsigned long long)word);
    return false;
}

void smith_import_cache_why(const char *reason) {
    (void)smith_ic_refuse(reason, 0, 0);
}

bool smith_import_region_open(SmithImportRegion *region, size_t reserve) {
    void *base = mmap(nullptr, reserve, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (base == MAP_FAILED) return false;
    *region = (SmithImportRegion){.base = base, .used = 0, .reserved = reserve};
    return true;
}

void smith_import_region_close(SmithImportRegion *region) {
    if (region->base != nullptr) munmap(region->base, region->reserved);
    *region = (SmithImportRegion){};
}

void *smith_import_region_take(SmithImportRegion *region, size_t size, size_t align) {
    SMITH_ASSERT(align > 0 && align <= 16 && (align & (align - 1)) == 0);
    size_t at = (region->used + align - 1) & ~(align - 1);
    if (at > region->reserved || region->reserved - at < size) return nullptr;
    region->used = at + size;
    return region->base + at;
}

// The ranges this process has mapped, from /proc/self/maps; 0 when they cannot be read (then nothing is stored).
static size_t smith_ic_read_maps(void) {
#ifdef __linux__
    FILE *file = fopen("/proc/self/maps", "r");
    if (file == nullptr) return 0;
    size_t count = 0;
    bool sorted = true;
    char line[4096];
    while (count < smith_ic_maps_max && fgets(line, sizeof line, file) != nullptr) { // bounded by the table
        char *dash = nullptr;
        char *space = nullptr;
        unsigned long long start = strtoull(line, &dash, 16);
        if (dash == line || *dash != '-') continue;
        unsigned long long end = strtoull(dash + 1, &space, 16);
        if (space == dash + 1 || *space != ' ') continue;
        if (count > 0 && start < smith_ic_maps[count - 1].end) sorted = false;
        smith_ic_maps[count++] = (SmithIcRange){start, end};
    }
    bool whole = feof(file) != 0 && sorted;
    fclose(file);
    return whole ? count : 0;
#else
    return 0; // macOS has no /proc: a module's image is not stored there
#endif
}

// Whether `value` is an address this process has mapped outside the two regions: /proc/self/maps lists its ranges in
// order, so a binary search.
static bool smith_ic_mapped(uintptr_t value, size_t maps, const SmithImportRegion *a, const SmithImportRegion *b) {
    uintptr_t a_start = (uintptr_t)a->base;
    uintptr_t b_start = (uintptr_t)b->base;
    if (value >= a_start && value < a_start + a->reserved) return false;
    if (value >= b_start && value < b_start + b->reserved) return false;
    size_t low = 0;
    size_t high = maps;
    while (low < high) { // bounded by log2 of the table
        size_t middle = low + (high - low) / 2;
        if (value < smith_ic_maps[middle].start) {
            high = middle;
        } else if (value >= smith_ic_maps[middle].end) {
            low = middle + 1;
        } else {
            return true;
        }
    }
    return false;
}

typedef struct {
    uint32_t *moves; // words that point into the tree
    size_t move_count;
    uint32_t *fixed; // words that point into smith's executable
    uint8_t *checks; // the 16 bytes each of those points at
    size_t fixed_count;
} SmithIcLists;

// `a` in `image`, its words that point into it or into smith's executable written as offsets and listed in `lists`;
// false when `a` and `b` differ in another way or a word points elsewhere into the process.
static bool smith_ic_pack(const SmithImportRegion *a, const SmithImportRegion *b, uint8_t *image, SmithIcLists *lists) {
    size_t maps = smith_ic_read_maps();
    if (maps == 0 || !smith_ic_find_program()) return false;
    if (a->used != b->used || a->used % 8 != 0) return smith_ic_refuse("two parses of different sizes", 0, 0);
    uintptr_t a_start = (uintptr_t)a->base;
    uintptr_t delta = (uintptr_t)b->base - a_start;
    for (size_t at = 0; at < a->used; at += 8) { // bounded by the image
        uint64_t x = 0;
        uint64_t y = 0;
        memcpy(&x, a->base + at, 8);
        memcpy(&y, b->base + at, 8);
        if (x == y) {
            if (smith_ic_in_program(x)) { // a text in smith itself: moved with the program
                if (at / 8 > UINT32_MAX) return false;
                memcpy(lists->checks + smith_ic_check * lists->fixed_count, (const void *)(uintptr_t)x, smith_ic_check);
                lists->fixed[lists->fixed_count++] = (uint32_t)(at / 8);
                uint64_t offset = x - smith_ic_program.bias;
                memcpy(image + at, &offset, 8);
                continue;
            }
            if (smith_ic_mapped((uintptr_t)x, maps, a, b)) return smith_ic_refuse("an address outside the tree", at, x);
            memcpy(image + at, &x, 8);
            continue;
        }
        if (y - x != delta || x < a_start || x > a_start + a->used)
            return smith_ic_refuse("a word that differs", at, x);
        if (at / 8 > UINT32_MAX) return false;
        lists->moves[lists->move_count++] = (uint32_t)(at / 8);
        uint64_t offset = x - a_start;
        memcpy(image + at, &offset, 8);
    }
    return true;
}

bool smith_import_image_store(const char *key, const SmithImportRegion *a, const SmithImportRegion *b, size_t root) {
    char path[PATH_MAX];
    if (!smith_ic_path("m", key, path) || a->used == 0 || a->used > (size_t)1 << 31) return false;
    size_t words = a->used / 8;
    size_t head = smith_ic_image_head;
    size_t bytes = head + a->used + (8 + smith_ic_check) * words; // room for every word in each list
    uint8_t *payload = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (payload == MAP_FAILED) return false;
    uint8_t *lists_at = payload + head + a->used;
    SmithIcLists lists = {
        .moves = (uint32_t *)lists_at, .fixed = (uint32_t *)(lists_at + 4 * words), .checks = lists_at + 8 * words};
    bool packed = smith_ic_pack(a, b, payload + head, &lists);
    if (packed) { // the moves, the fixed after them, and their checks
        size_t moves = lists.move_count;
        size_t fixed = lists.fixed_count;
        memmove(lists_at + 4 * moves, lists.fixed, 4 * fixed);
        memmove(lists_at + 4 * (moves + fixed), lists.checks, smith_ic_check * fixed);
        smith_ic_put64(payload, smith_ic_image_magic | ((uint64_t)moves << 32));
        smith_ic_put64(payload + 8, a->used);
        smith_ic_put64(payload + 16, root);
        smith_ic_put64(payload + 24, fixed);
        smith_import_cache_store("m", key, payload, head + a->used + 4 * (moves + fixed) + smith_ic_check * fixed);
    }
    munmap(payload, bytes);
    return packed;
}

// Adds `base` to each listed word of `image`, an offset that must be under `limit`; false when the list is damaged.
static bool smith_ic_move(uint8_t *image, uint64_t used, const uint8_t *list, uint64_t count, uintptr_t base,
                          uint64_t limit) {
    for (uint64_t i = 0; i < count; i++) { // bounded by the list
        uint32_t word = 0;
        memcpy(&word, list + 4 * i, 4);
        if ((uint64_t)word * 8 + 8 > used) return false;
        uint64_t offset = 0;
        memcpy(&offset, image + 8 * (uint64_t)word, 8);
        if (offset > limit) return false;
        uint64_t moved = (uint64_t)base + offset;
        memcpy(image + 8 * (uint64_t)word, &moved, 8);
    }
    return true;
}

// The entry `kind`/`key` mapped copy-on-write (a page the caller writes is its own): the payload, its size in `size`;
// null when there is no entry or it is damaged. The mapping starts `smith_ic_head` bytes before the payload.
static uint8_t *smith_ic_map(const char *kind, const char *key, size_t *size) {
    char path[PATH_MAX];
    if (!smith_ic_path(kind, key, path)) return nullptr;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return nullptr;
    struct stat info;
    uint8_t *mapped = MAP_FAILED;
    if (fstat(fd, &info) == 0 && (uint64_t)info.st_size > smith_ic_head)
        mapped = mmap(nullptr, (size_t)info.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_POPULATE, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) return nullptr;
    size_t whole = (size_t)info.st_size;
    uint64_t length = smith_ic_take64(mapped + 8);
    if (smith_ic_take64(mapped) == smith_ic_identity() && length == whole - smith_ic_head &&
        smith_ic_hash(mapped + smith_ic_head, length, 0) == smith_ic_take64(mapped + 16)) {
        smith_ic_touch_when_old(path, time(nullptr));
        *size = length;
        return mapped + smith_ic_head;
    }
    munmap(mapped, whole);
    return nullptr;
}

// Moves each fixed word of `image` to this process's program, when the 16 bytes it points at are those stored.
static bool smith_ic_move_fixed(uint8_t *image, uint64_t used, const uint8_t *list, const uint8_t *checks,
                                uint64_t count) {
    for (uint64_t i = 0; i < count; i++) { // bounded by the list
        uint32_t word = 0;
        memcpy(&word, list + 4 * i, 4);
        if ((uint64_t)word * 8 + 8 > used) return false;
        uint64_t offset = 0;
        memcpy(&offset, image + 8 * (uint64_t)word, 8);
        uint64_t moved = (uint64_t)smith_ic_program.bias + offset;
        if (moved < offset || !smith_ic_in_program(moved)) return false;
        if (memcmp((const void *)(uintptr_t)moved, checks + smith_ic_check * i, smith_ic_check) != 0) return false;
        memcpy(image + 8 * (uint64_t)word, &moved, 8);
    }
    return true;
}

void *smith_import_image_load(const char *key) {
    size_t size = 0;
    uint8_t *payload = smith_ic_find_program() ? smith_ic_map("m", key, &size) : nullptr;
    if (payload == nullptr) return nullptr;
    bool ok = size >= smith_ic_image_head;
    uint64_t head = smith_ic_take64(payload);
    uint64_t moves = head >> 32;
    uint64_t used = smith_ic_take64(payload + 8);
    uint64_t root = smith_ic_take64(payload + 16);
    uint64_t fixed = smith_ic_take64(payload + 24);
    ok = ok && (uint32_t)head == smith_ic_image_magic && used % 8 == 0 && root < used && fixed <= used / 8 &&
         size == smith_ic_image_head + used + 4 * (moves + fixed) + smith_ic_check * fixed;
    uint8_t *image = payload + smith_ic_image_head;
    const uint8_t *list = image + used;
    ok = ok && smith_ic_move(image, used, list, moves, (uintptr_t)image, used);
    ok = ok && smith_ic_move_fixed(image, used, list + 4 * moves, list + 4 * (moves + fixed), fixed);
    if (!ok) {
        munmap(payload - smith_ic_head, size + smith_ic_head);
        return nullptr;
    }
    return image + root;
}

// Gives back the address space past what the region uses: a region that holds a tree for the process's life.
void smith_import_region_trim(SmithImportRegion *region) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t keep = (region->used + page - 1) / page * page;
    if (keep == 0) keep = page;
    if (keep < region->reserved && munmap(region->base + keep, region->reserved - keep) == 0) region->reserved = keep;
}
