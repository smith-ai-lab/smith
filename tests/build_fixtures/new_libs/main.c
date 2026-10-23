#include <smith/arena.h>
#include <smith/random.h>

#include <stdio.h>

int main(void) {
    SmithKey key = smith_key_make(42);
    alignas(smith_arena_align_max) static unsigned char memory[1024];
    SmithArena arena = smith_arena_make(memory, sizeof memory);
    SmithCategorical categorical = smith_categorical_make(&arena, (const double[]){1, 1, 2}, 3);
    size_t index = smith_categorical_sample(key, categorical);
    float x = smith_normal_float_sample(smith_key_derive(key, 1), (SmithNormalFloat){.mean = 0, .deviation = 1});
    puts(index < 3 && x > -6.0f && x < 6.0f ? "libraries ok" : "libraries wrong");
    return 0;
}
