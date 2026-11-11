// The digest of a smith by its code (#2833): what `smith test`'s cache (runtime/test_cache.c) keys a test that runs
// smith by, and what the step cache (runtime/executor_cache.c) keys the compiler of its own smith-cc steps by. A file
// of its own, so that an edit to either cache does not change the digest object (#2855's key holds this one alone).
// Moved unchanged from runtime/test_cache.c, where #2854 added it.
#include "self_digest.h"
#include "core.h"
#include "package.h"
#include "platform_linux.h"

#include <sys/mman.h>
#include <sys/stat.h>

#ifdef SMITH_COMMIT_LINKED
extern const char smith_commit[];
#else
static const char smith_commit[] = "";
#endif

constexpr size_t smith_tc_blank_max = 16;

// Where `needle` (length `size`) occurs in the mapped file, at most `max` places, in order.
static size_t smith_tc_find_all(const uint8_t *file, size_t length, const uint8_t *needle, size_t size, size_t *at,
                                size_t max) {
    size_t found = 0;
    size_t from = 0;
    while (size > 0 && found < max && from + size <= length) { // bounded by the file and max
        const uint8_t *hit = memmem(file + from, length - from, needle, size);
        if (hit == nullptr) break;
        at[found++] = (size_t)(hit - file);
        from = (size_t)(hit - file) + size;
    }
    return found;
}

// The value of a hex digit, -1 for any other character.
static int smith_tc_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// A little-endian 32-bit word of the file at `at`, which the caller checked is inside it.
static uint32_t smith_tc_word(const uint8_t *file, size_t at) {
    return (uint32_t)file[at] | (uint32_t)file[at + 1] << 8 | (uint32_t)file[at + 2] << 16 |
           (uint32_t)file[at + 3] << 24;
}

// Where the code signature of a 64-bit Mach-O file is (LC_CODE_SIGNATURE's dataoff and datasize). The linker signs
// every arm64 binary, and the signature holds a hash of each page, those with the commit and LC_UUID among them: it
// changes with every commit as they do, so it is blanked with them (#3050). False for any other file.
static bool smith_tc_signature(const uint8_t *file, size_t length, size_t *at, size_t *size) {
    constexpr uint32_t magic = 0xfeedfacf;
    constexpr uint32_t code_signature = 0x1d;
    constexpr size_t header = 32;
    if (length < header || smith_tc_word(file, 0) != magic) return false;
    uint32_t commands = smith_tc_word(file, 16);
    size_t place = header;
    for (uint32_t i = 0; i < commands && place + 16 <= length; i++) { // bounded by ncmds and the file
        uint32_t command = smith_tc_word(file, place);
        uint32_t command_size = smith_tc_word(file, place + 4);
        if (command_size < 8) return false;
        if (command == code_signature) {
            size_t offset = smith_tc_word(file, place + 8);
            size_t bytes = smith_tc_word(file, place + 12);
            if (offset > length || bytes > length - offset) return false;
            *at = offset;
            *size = bytes;
            return true;
        }
        place += command_size;
    }
    return false;
}

// The digest of the smith under test (#2833). Its build-id and the commit it was built from are in the file, so they
// change with every commit; counted as they are, no test that runs smith could pass from the cache on another commit,
// and the key of every test held the build-id. Here both are blanked (they are zeros in what is hashed), so the digest
// changes with the code smith was built from and not with the commit it happens to carry. A file that does not hold
// them (or when this smith knows no commit) is hashed whole. A Mach-O file's code signature is blanked too.
bool smith_self_digest(const char *path, char hex[smith_sha256_hex_size]) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat info;
    if (fstat(fd, &info) != 0 || info.st_size <= 0) {
        close(fd);
        return false;
    }
    size_t length = (size_t)info.st_size;
    const uint8_t *file = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file == MAP_FAILED) return false;
    size_t at[2 * smith_tc_blank_max + 1]; // the commits, the ids and the signature
    size_t size[2 * smith_tc_blank_max + 1];
    size_t count = 0;
    size_t commit_size = strlen(smith_commit);
    count +=
        smith_tc_find_all(file, length, (const uint8_t *)smith_commit, commit_size, at + count, smith_tc_blank_max);
    for (size_t i = 0; i < count; i++)
        size[i] = commit_size;
    char id_hex[2 * smith_platform_id_bytes + 1];
    uint8_t id[smith_platform_id_bytes];
    bool have_id = smith_platform_file_id(path, id_hex);
    for (size_t k = 0; have_id && k < smith_platform_id_bytes; k++) { // the id's bytes, from its hex
        int high = smith_tc_hex_value(id_hex[2 * k]);
        int low = smith_tc_hex_value(id_hex[2 * k + 1]);
        have_id = high >= 0 && low >= 0;
        id[k] = (uint8_t)(high * 16 + low);
    }
    if (have_id) {
        size_t first = count;
        count += smith_tc_find_all(file, length, id, smith_platform_id_bytes, at + count, smith_tc_blank_max);
        for (size_t i = first; i < count; i++)
            size[i] = smith_platform_id_bytes;
    }
    if (smith_tc_signature(file, length, &at[count], &size[count])) count++;
    for (size_t i = 1; i < count; i++) { // insertion sort by place: few
        size_t place = at[i], width = size[i], j = i;
        for (; j > 0 && at[j - 1] > place; j--) {
            at[j] = at[j - 1];
            size[j] = size[j - 1];
        }
        at[j] = place;
        size[j] = width;
    }
    static const uint8_t zeros[4096];
    SmithSha256 sha;
    smith_sha256_init(&sha);
    size_t done = 0;
    for (size_t i = 0; i < count; i++) { // bounded by the blanks
        if (at[i] < done) continue;
        smith_sha256_update(&sha, file + done, at[i] - done);
        for (size_t left = size[i]; left > 0;) { // bounded by the blank's size
            size_t part = left < sizeof zeros ? left : sizeof zeros;
            smith_sha256_update(&sha, zeros, part);
            left -= part;
        }
        done = at[i] + size[i];
    }
    smith_sha256_update(&sha, file + done, length - done);
    smith_sha256_final(&sha, hex);
    munmap((void *)file, length);
    return true;
}
