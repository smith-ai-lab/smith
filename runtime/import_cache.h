// The disk cache of the new front end's imports (#2891): what runtime/import_cache.c shares with front/ and shim/.
#pragma once

#include "core.h"

constexpr size_t smith_import_cache_key_size = 33; // 32 hex digits and the terminator

// The folder of this smith's entries, `<cache>/smith/imports/<16 hex of smith_self_code_hash>`, or "" when the cache is
// off ($SMITH_IMPORT_CACHE=0) or this smith's digest cannot be had. Worked out once a process.
const char *smith_import_cache_folder(void);

// The key of `size` bytes of key material: 32 hex digits of smith_self_quick_hash (not a cryptographic hash: the
// entries come from this machine or from CI, never from a stranger).
void smith_import_cache_key(const void *material, size_t size, char key[smith_import_cache_key_size]);

// The payload of entry `kind`/`key` ("h" a C header, "m" a module): its size, written to `out` when it fits in
// `capacity` (call again with a buffer that big otherwise). SIZE_MAX when there is no entry, or it is damaged.
size_t smith_import_cache_load(const char *kind, const char *key, uint8_t *out, size_t capacity);

// Stores entry `kind`/`key`: written beside and renamed, so a reader sees all of it or none.
void smith_import_cache_store(const char *kind, const char *key, const uint8_t *data, size_t size);

// Counts a use of a kind's entry that the caller made sure of (a hit) or had to make (a miss).
void smith_import_cache_count(const char *kind, bool hit);

// Appends this process's counts to $SMITH_IMPORT_CACHE_LOG when it is set: one line `<hits> <misses>` for headers and
// modules, which CI adds up, and the identity folder it used as a line of `$SMITH_IMPORT_CACHE_LOG.identity` (#2978).
void smith_import_cache_report(void);

// A module's parse tree as an image (#2891): the parser runs twice, each time with every block it asks for taken from
// a region of its own (`smith_import_region_*`), on a copy of the source made first in that region. The two regions
// then hold the same bytes except for the pointers into themselves, which differ by the distance between the two
// bases: those are what a load moves. Any other difference (padding, a hash of an address), or a word that holds an
// address the process has mapped outside the region (a pointer to static or shared data, which another process would
// not have there), and the image is not stored.
typedef struct {
    uint8_t *base;
    size_t used;
    size_t reserved;
} SmithImportRegion;

// A region of `reserve` bytes of address space, committed as it is used. False when the system refuses.
bool smith_import_region_open(SmithImportRegion *region, size_t reserve);

void smith_import_region_close(SmithImportRegion *region);

// Gives back the address space the region does not use; it stays open.
void smith_import_region_trim(SmithImportRegion *region);

// `size` bytes at `align` (at most 16) from the region; null when it is full.
void *smith_import_region_take(SmithImportRegion *region, size_t size, size_t align);

// Prints `reason` as why an image is not stored, when SMITH_IMPORT_CACHE is `why`.
void smith_import_cache_why(const char *reason);

// Stores the image of `a` (`b` the same parse in another region) whose tree starts at byte `root` of the region.
// False when the two do not differ only by their own pointers (nothing is stored).
bool smith_import_image_store(const char *key, const SmithImportRegion *a, const SmithImportRegion *b, size_t root);

// The image of `key` loaded into memory of its own, with its pointers moved there: the address of its tree, or null
// when there is no entry or it is damaged. The memory is never freed (it backs the process's table of modules).
void *smith_import_image_load(const char *key);
