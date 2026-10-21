// Packs the files of clang's resource directory into the one file smith.c embeds (scripts/pack.h has the format).
//
// Usage: pack out.pack < list
// The list is one path per line, relative to the current folder (a build runs this in the LLVM install). Only regular
// files are accepted. The order is the list's, so a sorted list makes the same pack every time.
#include "pack.h"

#include <compression.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// On in every build. A failure is a bug or a bad input: report where and stop.
#define PACK_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "pack: %s failed at %s:%d\n", #condition, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

typedef struct {
    FILE *out;
    uint64_t hash;
    uint32_t records;
} Pack;

static unsigned char pack_raw[smith_pack_chunk_max];
static unsigned char pack_packed[smith_pack_chunk_max];
static unsigned char pack_scratch[1U << 22];

static uint64_t pack_hash_update(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) // bounded by size
        hash = (hash ^ bytes[i]) * 1099511628211ULL;
    return hash;
}

static void pack_write(Pack *pack, const void *data, size_t size) {
    pack->hash = pack_hash_update(pack->hash, data, size);
    PACK_ASSERT(fwrite(data, 1, size, pack->out) == size);
}

// Compresses the `size` bytes in pack_raw into pack_packed and returns the stored size; the bytes as they are when
// they do not shrink.
static uint32_t pack_compress(size_t size) {
    PACK_ASSERT(compression_encode_scratch_buffer_size(COMPRESSION_LZFSE) <= sizeof pack_scratch);
    size_t stored = compression_encode_buffer(pack_packed, size - 1, pack_raw, size, pack_scratch, COMPRESSION_LZFSE);
    if (stored == 0) {
        memcpy(pack_packed, pack_raw, size);
        return (uint32_t)size;
    }
    return (uint32_t)stored;
}

static void pack_chunk(Pack *pack, const char *path, uint32_t mode, size_t size, bool continued) {
    PACK_ASSERT(pack->records < smith_pack_files_max);
    uint32_t stored = pack_compress(size);
    SmithPackFile record = {.path_length = (uint32_t)strlen(path),
                            .mode = mode,
                            .size = (uint32_t)size,
                            .stored = stored,
                            .continued = continued ? 1U : 0U};
    pack_write(pack, &record, sizeof record);
    pack_write(pack, path, record.path_length);
    pack_write(pack, pack_packed, stored);
    pack->records++;
}

static void pack_file(Pack *pack, const char *path) {
    size_t length = strlen(path);
    PACK_ASSERT(length > 0 && length < smith_pack_path_max && path[0] != '/' && strstr(path, "..") == nullptr);
    struct stat info;
    PACK_ASSERT(stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0);
    FILE *in = fopen(path, "rb");
    PACK_ASSERT(in != nullptr);
    size_t left = (size_t)info.st_size;
    for (bool continued = false; left > 0; continued = true) { // each pass takes at least one byte
        size_t size = left < sizeof pack_raw ? left : sizeof pack_raw;
        PACK_ASSERT(fread(pack_raw, 1, size, in) == size);
        pack_chunk(pack, path, (uint32_t)(info.st_mode & 0777), size, continued);
        left -= size;
    }
    fclose(in);
}

int main(int argc, char **argv) {
    PACK_ASSERT(argc == 2);
    Pack pack = {.out = fopen(argv[1], "wb"), .hash = 14695981039346656037ULL};
    PACK_ASSERT(pack.out != nullptr);
    SmithPackHeader header = {};
    memcpy(header.magic, "SMITHPK1", sizeof header.magic);
    PACK_ASSERT(fwrite(&header, sizeof header, 1, pack.out) == 1);
    char line[smith_pack_path_max + 2];
    while (fgets(line, sizeof line, stdin) != nullptr) { // each file adds a record, which pack_chunk limits
        size_t length = strlen(line);
        PACK_ASSERT(length > 0 && line[length - 1] == '\n'); // a longer line has no newline in the buffer
        line[length - 1] = '\0';
        pack_file(&pack, line);
    }
    PACK_ASSERT(pack.records > 0);
    header.hash = pack.hash;
    header.count = pack.records;
    PACK_ASSERT(fseek(pack.out, 0, SEEK_SET) == 0 && fwrite(&header, sizeof header, 1, pack.out) == 1);
    PACK_ASSERT(fclose(pack.out) == 0);
    return 0;
}
