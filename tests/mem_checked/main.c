// mem::checked with an arena as its parent, from tests/mem_checked_test.smith. No argument: an arena reset while a
// block of it is still held, then a read of the block (ASan: use-after-poison). "double": a double free (the report
// and an abort). "leak": a leak, written by smith_mem_checked_report.
#include <smith/arena.h>
#include <smith/mem.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    static alignas(smith_arena_align_max) unsigned char memory[4096];
    SmithArena arena = smith_arena_fixed(memory, sizeof memory);
    SmithArenaAllocator parent = smith_arena_allocator(&arena);
    SmithChecked checked = smith_mem_checked(&parent.base);
    SmithAllocator *before = smith_mem_set_allocator(&checked.base);
    volatile int *block = smith_mem_alloc(sizeof(int), alignof(int));
    *block = 1;
    if (argc > 1 && strcmp(argv[1], "double") == 0) {
        smith_mem_free((void *)block);
        smith_mem_free((void *)block);
    }
    if (argc > 1 && strcmp(argv[1], "leak") == 0) {
        smith_mem_set_allocator(before);
        printf("live: %zu\n", smith_mem_checked_report(&checked));
        return 0;
    }
    smith_arena_reset(&arena); // everything after a reset is poisoned
    int stale = *block;        // use-after-poison
    smith_mem_set_allocator(before);
    return stale;
}
