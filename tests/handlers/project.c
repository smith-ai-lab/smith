// A program built by the conventions (`smith build`), which links the handlers runtime by itself: a handler set here is
// reached by malloc, with no flag of the program's. Prints the pointer's owner.
#include "smith/handlers.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    SmithAllocator allocator;
    char space[256];
    unsigned allocs;
} Fixed;

static void *fixed_alloc(SmithAllocator *self, size_t size, size_t align) {
    (void)align;
    Fixed *fixed = (Fixed *)self;
    fixed->allocs += 1;
    return size <= sizeof fixed->space ? fixed->space : nullptr;
}

static void *fixed_realloc(SmithAllocator *self, void *block, size_t size, size_t align) {
    (void)block;
    return fixed_alloc(self, size, align);
}

static void fixed_free(SmithAllocator *self, void *block) {
    (void)self;
    (void)block;
}

static bool fixed_owns(SmithAllocator *self, const void *block) {
    return block == ((Fixed *)self)->space;
}

int main(void) {
    Fixed fixed = {.allocator = {fixed_alloc, fixed_realloc, fixed_free, fixed_owns, nullptr}};
    // volatile: an optimizer knows that malloc returns new memory (never `fixed.space`) and may drop a pair; the
    // handler is behind a call it cannot see through, which is the point of the test and the one rule it teaches (issue
    // #239)
    void *volatile before = malloc(16);
    SmithAllocator *previous = smith_handlers_set_allocator(&fixed.allocator);
    void *volatile during = malloc(16);
    free(during);
    smith_handlers_set_allocator(previous);
    void *volatile after = malloc(16);
    printf("handler %d, before %d, after %d, allocs %u\n", during == fixed.space, before == fixed.space,
           after == fixed.space, fixed.allocs);
    free(before);
    free(after);
    return 0;
}
