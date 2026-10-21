// The format of the file scripts/pack.c writes and smith.c embeds: the files of clang's resource directory, each
// compressed on its own. Native byte order (it is made and read on the same machine); no padding to rely on.
//
//   SmithPackHeader
//   count times: SmithPackFile, then path_length bytes of path (relative, no NUL), then `stored` bytes of data
//
// A file is one record per chunk of at most smith_pack_chunk_max bytes; the records after the first have `continued`
// set and the same path, and append. A chunk whose `stored` is its `size` is stored as it is (it did not shrink);
// otherwise `stored` bytes of LZFSE (libcompression) expand to `size`. `hash` is FNV-1a 64 over everything after the
// header, so equal contents make equal hashes, and smith keys the folder it writes the files to by it.
#pragma once

#include <stdint.h>

constexpr uint32_t smith_pack_chunk_max = 4U * 1024U * 1024U; // the most bytes compressed at once
constexpr uint32_t smith_pack_files_max = 4096;               // the most records in one pack
constexpr uint32_t smith_pack_path_max = 256;                 // the longest path in one pack

typedef struct {
    char magic[8]; // "SMITHPK1"
    uint64_t hash;
    uint32_t count; // records
    uint32_t reserved;
} SmithPackHeader;

typedef struct {
    uint32_t path_length;
    uint32_t mode; // the permission bits
    uint32_t size;
    uint32_t stored;
    uint32_t continued; // 1: appends to the file of the record before
} SmithPackFile;
