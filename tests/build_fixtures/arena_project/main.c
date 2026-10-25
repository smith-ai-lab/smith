#include <smith/arena.h>

#include <stdio.h>

typedef struct {
    int id;
    double weight;
} Item;

int main(void) {
    alignas(smith_arena_align_max) static unsigned char memory[1024];
    SmithArena arena = smith_arena_make(memory, sizeof memory);
    Item *items = smith_arena_push_array(&arena, sizeof(Item), 4, alignof(Item));
    items[3].id = 7;
    printf("item %d, %zu of %zu bytes used\n", items[3].id, arena.used, arena.capacity);
    return 0;
}
