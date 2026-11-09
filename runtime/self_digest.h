// The digest of a smith by its code (#2833): what runtime/self_digest.c shares with the other files.
#pragma once

#include "core.h"
#include "package.h"

// The digest of the smith at `path` with its commit and its build-id blanked: it changes with the code smith was built
// from and not with the commit it carries. Hex in `hex`, false when the file cannot be read (#2833).
bool smith_self_digest(const char *path, char hex[smith_sha256_hex_size]);

// The same code identity with a quick, unkeyed 128-bit hash in place of SHA-256 (32 hex digits): what names the import
// cache's folder (runtime/import_cache.c, #2891). False when the file cannot be read.
bool smith_self_code_hash(const char *path, char hex[33]);

// The same quick hash of `size` bytes in memory: the keys of the import cache's entries.
void smith_self_quick_hash(const void *data, size_t size, char hex[33]);
