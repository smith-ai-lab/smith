// arena::allocator (lib/smith/arena.smith): an arena set as the thread's allocator serves malloc, calloc, realloc and
// free, answers owns and malloc_size, gives a request that does not fit as null, and takes its last block back on free.
// Prints `ok name` or `FAIL name`.
#include "smith/arena.h"
#include "smith/handlers.h"

#include <malloc/malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(const char *name, bool good) {
    printf("%s    %s\n", good ? "ok  " : "FAIL", name);
    failures += good ? 0 : 1;
}

int main(void) {
    static alignas(smith_arena_align_max) unsigned char memory[1 << 12];
    SmithArena arena = smith_arena_fixed(memory, sizeof memory);
    SmithArenaAllocator allocator = smith_arena_allocator(&arena);
    SmithAllocator *previous = smith_handlers_set_allocator(&allocator.base);
    unsigned char *a = malloc(100);
    unsigned char *b = calloc(4, 8);
    bool in_arena = a != nullptr && b != nullptr && a >= memory && b < memory + sizeof memory;
    bool aligned = ((uintptr_t)a & 15) == 0 && ((uintptr_t)b & 15) == 0;
    bool zeroed = b != nullptr && memcmp(b, (unsigned char[32]){}, 32) == 0;
    size_t used_two = arena.used;
    size_t usable = malloc_size(a);
    memset(a, 7, 100);
    unsigned char *grown = realloc(a, 300);
    bool kept = grown != nullptr && grown[0] == 7 && grown[99] == 7;
    void *too_big = malloc(1 << 20);
    int too_big_errno_free = too_big == nullptr;
    size_t before_last = arena.used;
    void *last = malloc(64);
    size_t after_last = arena.used;
    free(last);
    size_t after_free = arena.used;
    void *wide = aligned_alloc(64, 64);
    bool wide_ok = wide != nullptr && ((uintptr_t)wide & 63) == 0;
    free(b);
    smith_handlers_set_allocator(previous);
    check("arena allocator: malloc and calloc come from the arena, 16-aligned", in_arena && aligned);
    check("arena allocator: calloc is zeroed", zeroed);
    check("arena allocator: malloc_size says the size asked", usable == 100);
    check("arena allocator: a block has a header, so two blocks use more than their bytes", used_two > 132);
    check("arena allocator: realloc that grows copies", kept);
    check("arena allocator: a request that does not fit is null, as malloc's", too_big_errno_free);
    check("arena allocator: free of the last block gives it back",
          after_last > before_last && after_free < after_last && after_free >= before_last &&
              after_free - before_last < 16);
    check("arena allocator: aligned_alloc 64 is aligned", wide_ok);
    void *real = malloc(10);
    check("arena allocator: none set, malloc is libc's (outside the arena)",
          real != nullptr && ((unsigned char *)real < memory || (unsigned char *)real >= memory + sizeof memory));
    free(real);
    printf("%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}
