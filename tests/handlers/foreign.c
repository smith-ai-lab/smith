// A library the test program does not know the source of: built apart (the test builds it as a dylib and also loads it
// with dlopen), it allocates with the plain libc functions, which the runtime reaches through dyld.
#include <malloc/malloc.h>
#include <stdlib.h>

void *foreign_malloc(size_t size) {
    return malloc(size);
}

void *foreign_realloc(void *block, size_t size) {
    return realloc(block, size);
}

void foreign_free(void *block) {
    free(block);
}

void *foreign_zone_malloc(size_t size) {
    return malloc_zone_malloc(malloc_default_zone(), size);
}
