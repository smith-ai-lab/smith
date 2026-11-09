// The front end's test runner links no runtime: this is runtime/import_cache.h with the disk cache off (#2891), so
// front/module_cache.smith links and every import is parsed and read as without a cache.
// Keep in step with runtime/import_cache.h (which includes the runtime's core.h, not built here).
#include <stddef.h>

typedef struct {
    unsigned char *base;
    size_t used;
    size_t reserved;
} SmithImportRegion;

const char *smith_import_cache_folder(void) {
    return "";
}

void smith_import_cache_key(const void *material, size_t size, char key[33]) {
    (void)material;
    (void)size;
    key[0] = 0;
}

void smith_import_cache_count(const char *kind, bool hit) {
    (void)kind;
    (void)hit;
}

bool smith_import_region_open(SmithImportRegion *region, size_t reserve) {
    (void)region;
    (void)reserve;
    return false;
}

void smith_import_region_close(SmithImportRegion *region) {
    (void)region;
}

void smith_import_region_trim(SmithImportRegion *region) {
    (void)region;
}

void *smith_import_region_take(SmithImportRegion *region, size_t size, size_t align) {
    (void)region;
    (void)size;
    (void)align;
    return nullptr;
}

bool smith_import_image_store(const char *key, const SmithImportRegion *a, const SmithImportRegion *b, size_t root) {
    (void)key;
    (void)a;
    (void)b;
    (void)root;
    return false;
}

void *smith_import_image_load(const char *key) {
    (void)key;
    return nullptr;
}
