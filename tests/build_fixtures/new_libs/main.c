#include <smith/arena.h>
#include <smith/categorical.h>
#include <smith/mem.h>
#include <smith/random.h>

#include <stdio.h>

int main(void) {
    SmithKey keys[2];
    smith_key_split_n(smith_key_make(42), keys, 2);
    alignas(smith_arena_align_max) static unsigned char memory[1024];
    SmithArena arena = smith_arena_fixed(memory, sizeof memory);
    SmithArenaAllocator allocator = smith_arena_allocator(&arena);
    SmithAllocator *outside = smith_mem_set_allocator(&allocator.base); // the table is in the allocator in force
    SmithCategoricalTable categorical = smith_categorical_table_make((const double[]){1, 1, 2}, 3);
    smith_mem_set_allocator(outside);
    size_t index = smith_categorical_table_sample(keys[0], categorical);
    float x = smith_normal_float_sample(keys[1], (SmithNormalFloat){.mean = 0, .deviation = 1});
    puts(index < 3 && x > -6.0f && x < 6.0f ? "libraries ok" : "libraries wrong");
    return 0;
}
