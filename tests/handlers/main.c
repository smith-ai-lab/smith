// The handlers runtime (runtime/handlers.c): malloc and its family from this program, from a library linked beside it
// and from one loaded with dlopen reach the allocator set on the thread; a thread with none, and the code after the
// handler was restored, get libc's. Prints `ok name` or `FAIL name`; `main bench` prints the cost of a malloc/free
// pair.
#include "smith/handlers.h"

#include <dlfcn.h>
#include <malloc/malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *foreign_malloc(size_t size);
void *foreign_realloc(void *block, size_t size);
void foreign_free(void *block);
void *foreign_zone_malloc(size_t size);

typedef struct {
    SmithAllocator allocator; // first, so that &bump.allocator is the allocator
    unsigned char *base;
    size_t used;
    size_t capacity;
    unsigned allocs;
    unsigned frees;
    unsigned reallocs;
} Bump;

constexpr size_t header = 64; // the size of the block, and room for any alignment the tests ask for
alignas(4096) static unsigned char arena_space[1 << 20];

static void *bump_alloc(SmithAllocator *self, size_t size, size_t align) {
    Bump *bump = (Bump *)self;
    size_t start = (bump->used + header + align - 1) / align * align;
    if (start + size > bump->capacity) return nullptr;
    memcpy(bump->base + start - sizeof size, &size, sizeof size);
    bump->used = start + size;
    bump->allocs += 1;
    return bump->base + start;
}

static size_t bump_size(SmithAllocator *self, const void *block) {
    (void)self;
    size_t size = 0;
    memcpy(&size, (const unsigned char *)block - sizeof size, sizeof size);
    return size;
}

static void *bump_realloc(SmithAllocator *self, void *block, size_t size, size_t align) {
    Bump *bump = (Bump *)self;
    bump->reallocs += 1;
    void *moved = bump_alloc(self, size, align);
    if (moved == nullptr) return nullptr;
    size_t old = bump_size(self, block);
    memcpy(moved, block, old < size ? old : size);
    return moved;
}

static void bump_free(SmithAllocator *self, void *block) {
    (void)block;
    ((Bump *)self)->frees += 1;
}

static bool bump_owns(SmithAllocator *self, const void *block) {
    const Bump *bump = (const Bump *)self;
    const unsigned char *at = block;
    return at >= bump->base && at < bump->base + bump->capacity;
}

static Bump make_bump(void) {
    return (Bump){.allocator = {bump_alloc, bump_realloc, bump_free, bump_owns, bump_size},
                  .base = arena_space,
                  .capacity = sizeof arena_space};
}

static int failures = 0;

static void check(const char *name, bool good) {
    printf("%s    %s\n", good ? "ok  " : "FAIL", name);
    failures += good ? 0 : 1;
}

static bool in(const Bump *bump, const void *block) {
    return bump_owns((SmithAllocator *)bump, block);
}

static void test_family(void) {
    Bump bump = make_bump();
    void *libc_block = malloc(40);
    SmithAllocator *previous = smith_handlers_set_allocator(&bump.allocator);
    check("set_allocator returns the previous one (none)", previous == nullptr);
    void *a = malloc(100);
    unsigned char *z = calloc(10, 8);
    check("malloc is the handler's", in(&bump, a));
    check("calloc is the handler's and zeroed", in(&bump, z) && z[0] == 0 && z[79] == 0);
    memset(a, 7, 100);
    void *b = realloc(a, 300);
    check("realloc of a handler block is the handler's and keeps the bytes",
          in(&bump, b) && ((unsigned char *)b)[99] == 7);
    void *n = realloc(nullptr, 24);
    check("realloc(null) allocates from the handler", in(&bump, n));
    void *aligned = nullptr;
    int status = posix_memalign(&aligned, 256, 10);
    check("posix_memalign is the handler's and aligned",
          status == 0 && in(&bump, aligned) && (uintptr_t)aligned % 256 == 0);
    void *big = aligned_alloc(64, 128);
    check("aligned_alloc is the handler's and aligned", in(&bump, big) && (uintptr_t)big % 64 == 0);
    check("malloc_size of a handler block is the handler's", malloc_size(b) == 300);
    check("malloc_size of a libc block is libc's", malloc_size(libc_block) >= 40);
    void *zone_block = malloc_zone_malloc(malloc_default_zone(), 50);
    check("malloc_zone_malloc is the handler's", in(&bump, zone_block));
    check("malloc_zone_from_ptr answers for a handler block", malloc_zone_from_ptr(zone_block) != nullptr);
    unsigned frees = bump.frees;
    malloc_zone_free(malloc_default_zone(), zone_block);
    free(b);
    free(z);
    check("free and malloc_zone_free of handler blocks reach the handler", bump.frees == frees + 3);
    free(libc_block);
    check("free of a libc block under a handler is libc's (not the handler's)", bump.frees == frees + 3);
    check("the handler saw the allocations", bump.allocs == 7 && bump.reallocs == 1);
    check("set_allocator returns the handler it replaced", smith_handlers_set_allocator(previous) == &bump.allocator);
    void *after = malloc(10);
    check("after the restore malloc is libc's", !in(&bump, after) && smith_handlers_allocator() == nullptr);
    free(after);
}

static void *other_thread(void *result) {
    void *block = malloc(10);
    *(bool *)result = smith_handlers_allocator() == nullptr;
    free(block);
    return nullptr;
}

static void test_thread(void) {
    Bump bump = make_bump();
    SmithAllocator *previous = smith_handlers_set_allocator(&bump.allocator);
    bool none = false;
    pthread_t thread;
    pthread_create(&thread, nullptr, other_thread, &none);
    pthread_join(thread, nullptr);
    smith_handlers_set_allocator(previous);
    check("another thread has no handler: the table is per thread", none && bump.allocs == 0);
}

static void test_foreign(void) {
    Bump bump = make_bump();
    SmithAllocator *previous = smith_handlers_set_allocator(&bump.allocator);
    void *a = foreign_malloc(32);
    check("a library linked beside the program reaches the handler", in(&bump, a));
    void *b = foreign_realloc(a, 64);
    void *c = foreign_zone_malloc(16);
    check("... its realloc and zone malloc too", in(&bump, b) && in(&bump, c));
    foreign_free(b);
    check("... its free", bump.frees == 1);
    void *library = dlopen("./libforeign.dylib", RTLD_NOW);
    check("dlopen of the library works", library != nullptr);
    if (library != nullptr) {
        void *(*loaded_malloc)(size_t) = (void *(*)(size_t))(uintptr_t)dlsym(library, "foreign_malloc");
        void (*loaded_free)(void *) = (void (*)(void *))(uintptr_t)dlsym(library, "foreign_free");
        void *d = loaded_malloc(32);
        check("a library loaded with dlopen reaches the handler", in(&bump, d));
        loaded_free(d);
        check("... its free", bump.frees == 2);
    }
    smith_handlers_set_allocator(previous);
}

static double pair_nanoseconds(void) {
    constexpr int rounds = 5000000;
    struct timespec begin;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    for (int i = 0; i < rounds; i++) {
        void *volatile block = malloc(64);
        free(block);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    return ((double)(end.tv_sec - begin.tv_sec) * 1e9 + (double)(end.tv_nsec - begin.tv_nsec)) / rounds;
}

// A handler that is a bump allocator reset after each pair, so the numbers are the cost of the call.
static void *reset_alloc(SmithAllocator *self, size_t size, size_t align) {
    Bump *bump = (Bump *)self;
    bump->used = 0;
    return bump_alloc(self, size, align);
}

static void bench(void) {
    double none = pair_nanoseconds();
    Bump bump = make_bump();
    bump.allocator.alloc = reset_alloc;
    SmithAllocator *previous = smith_handlers_set_allocator(&bump.allocator);
    double handler = pair_nanoseconds();
    smith_handlers_set_allocator(previous);
    printf("malloc+free pair, no handler: %.1f ns; under a bump handler: %.1f ns\n", none, handler);
}

int main(int argc, char **argv) {
    static char
        output[1 << 14]; // stdio would take its buffer from the first handler in force, and keep it past its scope
    setvbuf(stdout, output, _IOFBF, sizeof output);
    if (argc > 1 && strcmp(argv[1], "bench") == 0) {
        bench();
        return 0;
    }
    test_family();
    test_thread();
    test_foreign();
    printf("%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
