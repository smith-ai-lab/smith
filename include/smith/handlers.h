// The handlers of the runtime that every smith program links (runtime/handlers.c): the allocator of the current thread,
// which malloc, calloc, realloc, free, posix_memalign, aligned_alloc, valloc, malloc_size and the malloc_zone_ family
// reach, from smith code, from a prebuilt library and from one loaded with dlopen. No allocator set (the default) is
// libc's. Issue #239; knowledge/handlers.md. Plain C: a C++ file includes it inside extern "C" { } (the grammar for
// .smith, which parses smith's hand-written C, does not read the closing of such a block in a header).
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct SmithAllocator SmithAllocator;

// An allocator is a struct whose first member is a SmithAllocator, passed as &x.allocator. `owns` says whether a block
// came from it (a free or realloc of any other block goes to libc); `size` is the usable size of a block it owns, and
// may be null (malloc_size then says 0 for its blocks).
struct SmithAllocator {
    void *(*alloc)(SmithAllocator *self, size_t size, size_t align);
    void *(*realloc)(SmithAllocator *self, void *block, size_t size, size_t align);
    void (*free)(SmithAllocator *self, void *block);
    bool (*owns)(SmithAllocator *self, const void *block);
    size_t (*size)(SmithAllocator *self, const void *block);
};

// Sets the allocator of the calling thread (null: libc's) and returns the one it had, as signal() does; restoring it is
// the caller's, with `defer`.
SmithAllocator *smith_handlers_set_allocator(SmithAllocator *allocator);

// The allocator of the calling thread, null when none is set.
SmithAllocator *smith_handlers_allocator(void);
