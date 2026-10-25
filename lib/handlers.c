// The handlers runtime (issue #239): one dylib that every smith program links, whose `__interpose` tuples send the
// allocator family to the allocator of the calling thread, so that code that calls malloc (smith's, a prebuilt
// library's, one loaded with dlopen) gets what the program set. No handler set is libc's own function. The functions
// call libc's by their names: a dylib's own calls are not interposed by its tuples (under ASan, linked before this
// dylib, they reach ASan's), so nothing here asks dlsym for them, which would itself allocate. The handler is found
// through a thread-local read only once `smith_handlers_ready` says the image's thread-local storage exists (dyld and
// ASan allocate before).
#include "smith/handlers.h"

#include <errno.h>
#include <malloc/malloc.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static atomic_bool smith_handlers_ready;
static atomic_uint smith_handlers_active; // threads with an allocator set: 0 is the fast path, libc with no lookup
static thread_local SmithAllocator *smith_handlers_table;

__attribute__((constructor)) static void smith_handlers_start(void) {
    atomic_store(&smith_handlers_ready, true);
}

__attribute__((noinline)) static SmithAllocator *smith_handlers_current(void) {
    if (atomic_load_explicit(&smith_handlers_active, memory_order_relaxed) == 0) return nullptr;
    if (!atomic_load_explicit(&smith_handlers_ready, memory_order_relaxed)) return nullptr;
    return smith_handlers_table;
}

SmithAllocator *smith_handlers_set_allocator(SmithAllocator *allocator) {
    SmithAllocator *previous = smith_handlers_table;
    smith_handlers_table = allocator;
    if (previous == nullptr && allocator != nullptr) atomic_fetch_add(&smith_handlers_active, 1);
    if (previous != nullptr && allocator == nullptr) atomic_fetch_sub(&smith_handlers_active, 1);
    return previous;
}

SmithAllocator *smith_handlers_allocator(void) {
    return smith_handlers_current();
}

constexpr size_t smith_handlers_align = 16;

static bool smith_handlers_owns(SmithAllocator *allocator, const void *block) {
    return allocator != nullptr && block != nullptr && allocator->owns(allocator, block);
}

static void *smith_handlers_malloc(size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    return allocator != nullptr ? allocator->alloc(allocator, size, smith_handlers_align) : malloc(size);
}

static void *smith_handlers_calloc(size_t count, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return calloc(count, size);
    size_t bytes = 0;
    if (__builtin_mul_overflow(count, size, &bytes)) {
        errno = ENOMEM;
        return nullptr;
    }
    void *block = allocator->alloc(allocator, bytes, smith_handlers_align);
    if (block != nullptr) memset(block, 0, bytes);
    return block;
}

static void *smith_handlers_realloc(void *block, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return realloc(block, size);
    if (block == nullptr) return allocator->alloc(allocator, size, smith_handlers_align);
    if (smith_handlers_owns(allocator, block)) return allocator->realloc(allocator, block, size, smith_handlers_align);
    return realloc(block, size);
}

static void smith_handlers_free(void *block) {
    SmithAllocator *allocator = smith_handlers_current();
    if (smith_handlers_owns(allocator, block)) {
        allocator->free(allocator, block);
    } else {
        free(block);
    }
}

static int smith_handlers_posix_memalign(void **result, size_t align, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return posix_memalign(result, align, size);
    if (align < sizeof(void *) || (align & (align - 1)) != 0) return EINVAL;
    void *block = allocator->alloc(allocator, size, align);
    if (block == nullptr) return ENOMEM;
    *result = block;
    return 0;
}

static void *smith_handlers_aligned_alloc(size_t align, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return aligned_alloc(align, size);
    return allocator->alloc(allocator, size, align);
}

static void *smith_handlers_valloc(size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    return allocator != nullptr ? allocator->alloc(allocator, size, 16384) : valloc(size);
}

static size_t smith_handlers_malloc_size(const void *block) {
    SmithAllocator *allocator = smith_handlers_current();
    if (!smith_handlers_owns(allocator, block)) return malloc_size(block);
    return allocator->size != nullptr ? allocator->size(allocator, block) : 0;
}

// The zone functions are one passthrough: a block of the current allocator answers whatever zone is named, anything
// else is the zone's own (spike 3 of #208: CoreFoundation and Objective-C ask the zone about a pointer, and free
// through it).
static void *smith_handlers_zone_malloc(malloc_zone_t *zone, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    return allocator != nullptr ? allocator->alloc(allocator, size, smith_handlers_align)
                                : malloc_zone_malloc(zone, size);
}

static void *smith_handlers_zone_calloc(malloc_zone_t *zone, size_t count, size_t size) {
    return smith_handlers_current() != nullptr ? smith_handlers_calloc(count, size)
                                               : malloc_zone_calloc(zone, count, size);
}

static void *smith_handlers_zone_valloc(malloc_zone_t *zone, size_t size) {
    return smith_handlers_current() != nullptr ? smith_handlers_valloc(size) : malloc_zone_valloc(zone, size);
}

static void *smith_handlers_zone_memalign(malloc_zone_t *zone, size_t align, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    return allocator != nullptr ? allocator->alloc(allocator, size, align) : malloc_zone_memalign(zone, align, size);
}

static void *smith_handlers_zone_realloc(malloc_zone_t *zone, void *block, size_t size) {
    SmithAllocator *allocator = smith_handlers_current();
    if (allocator == nullptr) return malloc_zone_realloc(zone, block, size);
    if (block == nullptr || smith_handlers_owns(allocator, block)) return smith_handlers_realloc(block, size);
    return malloc_zone_realloc(zone, block, size);
}

static void smith_handlers_zone_free(malloc_zone_t *zone, void *block) {
    SmithAllocator *allocator = smith_handlers_current();
    if (smith_handlers_owns(allocator, block)) {
        allocator->free(allocator, block);
    } else {
        malloc_zone_free(zone, block);
    }
}

static malloc_zone_t *smith_handlers_zone_from_ptr(const void *block) {
    return smith_handlers_owns(smith_handlers_current(), block) ? malloc_default_zone() : malloc_zone_from_ptr(block);
}

typedef struct {
    uintptr_t replacement;
    uintptr_t replacee;
} SmithInterpose;

// clang-format off
__attribute__((used, section("__DATA,__interpose"))) static const SmithInterpose smith_handlers_tuples[] = { // NOLINT(smith-constexpr): addresses are not constant expressions
    {(uintptr_t)&smith_handlers_malloc, (uintptr_t)&malloc},
    {(uintptr_t)&smith_handlers_calloc, (uintptr_t)&calloc},
    {(uintptr_t)&smith_handlers_realloc, (uintptr_t)&realloc},
    {(uintptr_t)&smith_handlers_free, (uintptr_t)&free},
    {(uintptr_t)&smith_handlers_posix_memalign, (uintptr_t)&posix_memalign},
    {(uintptr_t)&smith_handlers_aligned_alloc, (uintptr_t)&aligned_alloc},
    {(uintptr_t)&smith_handlers_valloc, (uintptr_t)&valloc},
    {(uintptr_t)&smith_handlers_malloc_size, (uintptr_t)&malloc_size},
    {(uintptr_t)&smith_handlers_zone_malloc, (uintptr_t)&malloc_zone_malloc},
    {(uintptr_t)&smith_handlers_zone_calloc, (uintptr_t)&malloc_zone_calloc},
    {(uintptr_t)&smith_handlers_zone_valloc, (uintptr_t)&malloc_zone_valloc},
    {(uintptr_t)&smith_handlers_zone_memalign, (uintptr_t)&malloc_zone_memalign},
    {(uintptr_t)&smith_handlers_zone_realloc, (uintptr_t)&malloc_zone_realloc},
    {(uintptr_t)&smith_handlers_zone_free, (uintptr_t)&malloc_zone_free},
    {(uintptr_t)&smith_handlers_zone_from_ptr, (uintptr_t)&malloc_zone_from_ptr},
};
// clang-format on
