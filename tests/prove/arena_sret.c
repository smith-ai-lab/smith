// A trusted library function given a writable pointer frees nothing: a caller that returns a struct through sret
// stores into it after pushing on an arena, and that store is defined (#74: game's replay_make).
#include <smith/arena.h>
#include <smith/contract.h>

#include <stdint.h>

typedef struct {
    uint8_t *entries;
    uint64_t capacity;
    uint64_t count;
    uint64_t pad[7];
} Replay;

Replay replay_make(SmithArena *arena, uint64_t capacity);

Replay replay_make(SmithArena *arena, uint64_t capacity) {
    smith_requires(arena != nullptr);
    smith_requires(arena->used <= arena->capacity);
    smith_requires(capacity > 0 && capacity <= 1000);
    Replay replay = {.entries = smith_arena_push_array(arena, 24, capacity, 8), .capacity = capacity};
    return replay;
}
