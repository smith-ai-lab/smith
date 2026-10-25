// Tests of smith's archive readers (runtime/gzip.c on lib/smith/inflate.smith, runtime/xz.c, runtime/untar.c), run by
// tests/untar.sh under AddressSanitizer and UndefinedBehaviorSanitizer. It includes runtime/package.c to reach
// smith_package_extract. Modes:
//   vectors                                known answers: small gzip (fixed Huffman blocks) and xz streams, and
//                                          every shorter prefix of each refused
//   extract <archive> <folder>             extract; print "ok", or the error text; exit 0 or 1
//   prefixes <archive> <scratch>           every prefix shorter than the archive must fail (for a plain tar, shorter
//                                          than its entries and its two zero blocks), without crashing
//   fuzz <archive> <scratch> <n> <seed>    n random damages of the archive; none may crash, hang, or leave a file
//                                          outside the folder or a symlink that resolves outside it
// Damage is made by a fixed pseudo-random sequence, so a failure repeats. For a plain tar the damage also rewrites
// names, link targets and types in a header and then fixes the header's checksum, so that the checks on the names, the
// links and the types are what stop it, not the checksum.
#ifdef __linux__
// runtime/package.c, included below, needs what glibc hides from C23 (nftw's FTW_PHYS), as in smith.c.
// NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming): glibc's name for it
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <stdlib.h>

extern char **environ;
#define SMITH_ASSERT(condition) assert(condition)

// package.c holds much this test never calls (the cache, the download).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "../runtime/package.c"
#pragma clang diagnostic pop

constexpr size_t test_archive_max = 8U << 20;

static uint8_t test_original[test_archive_max];
static uint8_t test_damaged[test_archive_max];
static size_t test_original_size;

static bool test_read_archive(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    ssize_t got = read(fd, test_original, sizeof test_original);
    close(fd);
    if (got <= 0 || (size_t)got == sizeof test_original) return false;
    test_original_size = (size_t)got;
    return true;
}

static bool test_write_file(const char *path, const uint8_t *data, size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    ssize_t wrote = write(fd, data, size);
    close(fd);
    return wrote >= 0 && (size_t)wrote == size;
}

static uint64_t test_next(uint64_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

static size_t test_below(uint64_t *state, size_t bound) {
    return (size_t)(test_next(state) % bound);
}

// Extracts `size` bytes of `data` (written to <scratch>/archive) into a fresh <scratch>/box/out.
static bool test_extract_bytes(const char *scratch, const uint8_t *data, size_t size, SmithPackageError *error) {
    char archive[PATH_MAX];
    char box[PATH_MAX];
    char folder[PATH_MAX];
    snprintf(archive, sizeof archive, "%s/archive", scratch);
    snprintf(box, sizeof box, "%s/box", scratch);
    snprintf(folder, sizeof folder, "%s/box/out", scratch);
    smith_package_remove(box);
    if (!test_write_file(archive, data, size) || mkdir(box, 0777) != 0 || mkdir(folder, 0777) != 0) {
        fprintf(stderr, "cannot prepare %s\n", scratch);
        exit(2);
    }
    SmithExtracted extracted;
    return smith_package_extract(archive, "file", folder, &extracted, error);
}

static int test_check_box_entry(const char *path, const struct stat *info, int type, struct FTW *where) {
    (void)type;
    (void)where;
    if (!S_ISLNK(info->st_mode)) return 0;
    char resolved[PATH_MAX];
    if (realpath(path, resolved) == nullptr) return 0; // a dangling link goes nowhere
    const char *box = getenv("TEST_BOX");
    char box_resolved[PATH_MAX];
    if (box == nullptr || realpath(box, box_resolved) == nullptr) return 1;
    size_t length = strlen(box_resolved);
    bool inside = strncmp(resolved, box_resolved, length) == 0 && (resolved[length] == '/' || resolved[length] == '\0');
    if (!inside) fprintf(stderr, "symlink %s resolves to %s, outside %s\n", path, resolved, box_resolved);
    return inside ? 0 : 1;
}

// After an extraction: <scratch>/box holds only "out", and no symlink in it resolves outside it.
static bool test_box_is_clean(const char *scratch) {
    char box[PATH_MAX];
    snprintf(box, sizeof box, "%s/box", scratch);
    DIR *dir = opendir(box);
    if (dir == nullptr) return false;
    bool clean = true;
    for (size_t i = 0; i < 1000; i++) {
        struct dirent *entry = readdir(dir);
        if (entry == nullptr) break;
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 && strcmp(entry->d_name, "out") != 0) {
            fprintf(stderr, "a file %s appeared beside the extraction folder\n", entry->d_name);
            clean = false;
        }
    }
    closedir(dir);
    setenv("TEST_BOX", box, 1);
    char folder[PATH_MAX];
    snprintf(folder, sizeof folder, "%s/out", box);
    if (nftw(folder, test_check_box_entry, 32, FTW_PHYS) != 0) clean = false;
    char outer[PATH_MAX];
    snprintf(outer, sizeof outer, "%s/escaped", scratch);
    struct stat info;
    if (lstat(outer, &info) == 0) clean = false;
    return clean;
}

static int test_mode_extract(const char *archive, const char *folder) {
    SmithPackageError error = {};
    SmithExtracted extracted;
    if (!smith_package_extract(archive, "file", folder, &extracted, &error)) {
        printf("%s\n", error.text);
        return 1;
    }
    printf("ok\n");
    return 0;
}

// Where a plain tar's entries end, and then its two zero blocks: a prefix at least this long is a whole archive.
static size_t test_tar_length(const uint8_t *data, size_t size) {
    size_t at = 0;
    while (at + smith_untar_block <= size && !smith_untar_is_zero(data + at, smith_untar_block)) {
        uint64_t length = 0;
        if (!smith_untar_number(data + at + 124, 12, &length)) break;
        at += smith_untar_block + (size_t)((length + 511U) / 512U * 512U);
    }
    return at + 2 * smith_untar_block;
}

// Every prefix is tried when `step` is 1; else every step-th one, and every one in the first two blocks and within two
// bytes of a block boundary (where a header or a padding ends), which are the ones that differ.
static bool test_prefix_wanted(size_t length, size_t step) {
    size_t in_block = length % smith_untar_block;
    return step <= 1 || length < 2 * smith_untar_block || in_block <= 2 || in_block + 2 >= smith_untar_block ||
           length % step == 0;
}

static int test_mode_prefixes(const char *archive, const char *scratch, size_t step) {
    if (!test_read_archive(archive)) return 2;
    size_t limit = test_original_size;
    const char *refused = nullptr;
    if (smith_archive_format(test_original, test_original_size, &refused) == SmithArchive_Tar) {
        limit = test_tar_length(test_original, test_original_size);
    }
    for (size_t length = 0; length < limit; length++) {
        if (!test_prefix_wanted(length, step)) continue;
        SmithPackageError error = {};
        if (test_extract_bytes(scratch, test_original, length, &error)) {
            fprintf(stderr, "a prefix of %zu of %zu bytes was accepted\n", length, test_original_size);
            return 1;
        }
        if (error.text[0] == '\0' || !test_box_is_clean(scratch)) return 1;
    }
    printf("%zu prefixes refused\n", limit);
    return 0;
}

static const char *const test_names[] = {"..",      "../x",         "/abs",      "a/../b", ".",    "//",
                                         "x/./y",   "a/b/../../..", "",          "dir/",   "link", "link/under",
                                         "escaped", "../escaped",   "box/out/z", "z"};
static constexpr char test_types[] = "0012255xgLK3467S";

static void test_checksum_fix(uint8_t *block) {
    memset(block + 148, ' ', 8);
    unsigned sum = 0;
    for (size_t i = 0; i < smith_untar_block; i++)
        sum += block[i];
    snprintf((char *)block + 148, 8, "%06o", sum);
    block[155] = ' ';
}

// Rewrites one tar header with a name, a link target or a type from the lists, and fixes its checksum.
static void test_damage_header(uint8_t *data, size_t size, uint64_t *state) {
    size_t headers = 0;
    size_t starts[4096];
    for (size_t at = 0; at + smith_untar_block <= size && headers < 4096;) {
        if (smith_untar_is_zero(data + at, smith_untar_block) || !smith_untar_header_checksum_ok(data + at)) break;
        uint64_t length = 0;
        if (!smith_untar_number(data + at + 124, 12, &length)) break;
        starts[headers++] = at;
        at += smith_untar_block + (size_t)((length + 511U) / 512U * 512U);
    }
    if (headers == 0) return;
    uint8_t *block = data + starts[test_below(state, headers)];
    size_t choice = test_below(state, 3);
    if (choice == 0) {
        const char *name = test_names[test_below(state, sizeof test_names / sizeof test_names[0])];
        memset(block, 0, 100);
        memcpy(block, name, strlen(name) + 1); // a name is shorter than the field
    } else if (choice == 1) {
        const char *name = test_names[test_below(state, sizeof test_names / sizeof test_names[0])];
        memset(block + 157, 0, 100);
        memcpy(block + 157, name, strlen(name) + 1);
        block[156] = '2';
    } else {
        block[156] = (uint8_t)test_types[test_below(state, sizeof test_types - 1)];
    }
    test_checksum_fix(block);
}

static void test_damage(size_t *size, uint64_t *state, bool tar) {
    if (test_original_size == 0) return;
    memcpy(test_damaged, test_original, test_original_size);
    *size = test_original_size;
    size_t edits = 1 + test_below(state, 3);
    for (size_t i = 0; i < edits && *size > 0; i++) {
        size_t kind = test_below(state, 5);
        size_t at = test_below(state, *size);
        if (kind == 0)
            test_damaged[at] ^= (uint8_t)(1U << test_below(state, 8));
        else if (kind == 1)
            test_damaged[at] = (uint8_t)test_next(state);
        else if (kind == 2)
            *size = at;
        else if (kind == 3 && at + 8 < *size) {
            size_t cut = 1 + test_below(state, 7);
            memmove(test_damaged + at, test_damaged + at + cut, *size - at - cut);
            *size -= cut;
        } else if (tar) {
            test_damage_header(test_damaged, *size, state);
        }
    }
}

static int test_mode_fuzz(const char *archive, const char *scratch, size_t iterations, uint64_t seed) {
    if (!test_read_archive(archive)) return 2;
    bool tar = smith_archive_format(test_original, test_original_size, &(const char *){nullptr}) == SmithArchive_Tar;
    uint64_t state = seed * 0x9E3779B97F4A7C15U + 1U;
    size_t accepted = 0;
    for (size_t i = 0; i < iterations; i++) {
        size_t size = 0;
        test_damage(&size, &state, tar);
        SmithPackageError error = {};
        if (test_extract_bytes(scratch, test_damaged, size, &error))
            accepted++;
        else if (error.text[0] == '\0') {
            fprintf(stderr, "iteration %zu failed without a message\n", i);
            return 1;
        }
        if (!test_box_is_clean(scratch)) {
            fprintf(stderr, "iteration %zu left the box unclean\n", i);
            return 1;
        }
    }
    printf("%zu damaged archives, %zu accepted\n", iterations, accepted);
    return 0;
}

typedef struct {
    uint8_t bytes[128];
    size_t size;
} TestOutput;

static bool test_collect(void *context, const uint8_t *data, size_t size) {
    TestOutput *output = context;
    if (size > sizeof output->bytes - output->size) return false;
    memcpy(output->bytes + output->size, data, size);
    output->size += size;
    return true;
}

typedef struct {
    const char *name;
    bool xz;
    const char *expected;
    size_t size;
    uint8_t bytes[96];
} TestVector;

// Made with `gzip -9 -n` and `xz --check=crc64`. The gzip ones are single blocks with fixed Huffman codes (the first
// deflate byte ends in the bits 011), which the archives of the other tests, big enough for dynamic codes, never use.
static const TestVector test_vectors[] = {
    {"gzip, fixed codes with matches", false, "hello hello hello hello\n", 29, {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00,
                                                                                0x00, 0x00, 0x02, 0x03, 0xcb, 0x48,
                                                                                0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40,
                                                                                0x27, 0xb9, 0x00, 0x00, 0x88, 0x59,
                                                                                0x0b, 0x18, 0x00, 0x00, 0x00}},
    {"gzip, fixed codes", false, "hello", 25, {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
                                               0x03, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x07, 0x00, 0x86,
                                               0xa6, 0x10, 0x36, 0x05, 0x00, 0x00, 0x00}},
    {"gzip, empty", false, "", 20, {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x03,
                                    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"xz, one block",
     true,
     "hello hello hello hello\n",
     80,
     {0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x04, 0xe6, 0xd6, 0xb4, 0x46, 0x04, 0xc0, 0x14, 0x18,
      0x21, 0x01, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0xf3, 0x46, 0x45,
      0xe0, 0x00, 0x17, 0x00, 0x0c, 0x5d, 0x00, 0x34, 0x19, 0x49, 0xee, 0x8d, 0xe9, 0x56, 0x0a, 0xc1,
      0x21, 0xb0, 0x00, 0x00, 0x6c, 0x14, 0xed, 0x1b, 0x3b, 0x68, 0xbb, 0x91, 0x00, 0x01, 0x30, 0x18,
      0x8e, 0x1b, 0xac, 0xec, 0x1f, 0xb6, 0xf3, 0x7d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x59, 0x5a}},
    {"xz, no blocks", true, "", 32, {0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00, 0x00, 0x04, 0xe6, 0xd6, 0xb4,
                                     0x46, 0x00, 0x00, 0x00, 0x00, 0x1c, 0xdf, 0x44, 0x21, 0x1f, 0xb6,
                                     0xf3, 0x7d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x59, 0x5a}},
};

static int test_mode_vectors(void) {
    for (size_t i = 0; i < sizeof test_vectors / sizeof test_vectors[0]; i++) {
        const TestVector *vector = &test_vectors[i];
        TestOutput output = {};
        SmithPackageError error = {};
        bool ok = vector->xz ? smith_xz_decode(vector->bytes, vector->size, test_collect, &output, &error)
                             : smith_gzip_decode(vector->bytes, vector->size, test_collect, &output, &error);
        if (!ok || output.size != strlen(vector->expected) ||
            memcmp(output.bytes, vector->expected, output.size) != 0) {
            printf("%s: wrong answer: %s\n", vector->name, error.text);
            return 1;
        }
        for (size_t length = 0; length < vector->size; length++) {
            output = (TestOutput){};
            ok = vector->xz ? smith_xz_decode(vector->bytes, length, test_collect, &output, &error)
                            : smith_gzip_decode(vector->bytes, length, test_collect, &output, &error);
            if (ok) {
                printf("%s: a prefix of %zu bytes was accepted\n", vector->name, length);
                return 1;
            }
        }
    }
    printf("vectors ok\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "vectors") == 0) return test_mode_vectors();
    if (argc == 4 && strcmp(argv[1], "extract") == 0) return test_mode_extract(argv[2], argv[3]);
    if ((argc == 4 || argc == 5) && strcmp(argv[1], "prefixes") == 0)
        return test_mode_prefixes(argv[2], argv[3], argc == 5 ? (size_t)strtoul(argv[4], nullptr, 10) : 1);
    if (argc == 6 && strcmp(argv[1], "fuzz") == 0) {
        return test_mode_fuzz(argv[2], argv[3], strtoull(argv[4], nullptr, 10), strtoull(argv[5], nullptr, 10));
    }
    fprintf(stderr, "usage: untar extract A F | prefixes A S | fuzz A S N SEED\n");
    return 2;
}
