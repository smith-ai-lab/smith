#include <smith/arena.h>
#include <smith/random.h>

#include <stdio.h>

// Your own distribution, written like smith's: built only from the public primitives.
static double exponential_sample(SmithKey key, double rate) {
    return -smith_log_double(1.0 - smith_key_unit_double(key)) / rate;
}

int main(void) {
    SmithKey parts[3];
    smith_key_split_n(smith_key_make(42), parts, 3); // each key is used once
    SmithKey many[1000];
    smith_key_split_n(parts[0], many, 1000);
    double mean = 0;
    for (size_t i = 0; i < 1000; i++)
        mean += exponential_sample(many[i], 2.0) / 1000.0;
    float x = smith_normal_float_sample(parts[1], (SmithNormalFloat){.mean = 0, .deviation = 1});
    alignas(smith_arena_align_max) static unsigned char memory[1024];
    SmithArena arena = smith_arena_make(memory, sizeof memory);
    SmithCategorical categorical = smith_categorical_make(&arena, (const double[]){1, 1, 2}, 3);
    printf("mean %.1f, normal %d, index %zu\n", mean, x > -6.0f && x < 6.0f, smith_categorical_sample(parts[2], categorical) < 3 ? (size_t)1 : (size_t)0);
    return 0;
}
