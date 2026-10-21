#include <stdio.h>

#include "smith/arena.h"
#include "smith/text.h"

// The body of an export that has none in the module, by the C name the module chose.
uint64_t smith_arena_checksum(const uint8_t *data, size_t size) {
    uint64_t sum = 0;
    for (size_t i = 0; i < size; i++)
        sum = sum * 31 + data[i];
    return sum;
}

int main(void) {
    SmithArena arena = smith_arena_create(100);
    uint64_t first = smith_arena_take(&arena, 30);
    printf("%llu %llu %llu\n", (unsigned long long)first, (unsigned long long)smith_arena_limit,
           (unsigned long long)smith_text_fill(64));
    return 0;
}
