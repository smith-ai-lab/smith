// proves: smith_arena_measure smith_arena_fits smith_arena_reset
// link:
// smith's arena: the three proved functions, and the ensures its trusted pushes promise (callers assume it, so the
// harness runs them on arenas over real memory).
#include "arena_lowered.c" // `smith cc --smith-lower lib/std/arena.smith`, written by tests/prove_sound_test.smith
#include "sound.h"

static void arena_make_random(SmithArena *arena, uint8_t *memory, size_t capacity) {
    *arena = (SmithArena){.memory = memory, .capacity = capacity};
    arena->used = (size_t)sound_between(0, (int64_t)capacity + 2); // past the capacity sometimes: a rejected input
    arena->touched = arena->used;
    if (sound_next() % 64 == 0) arena->used = (size_t)-1 - sound_below(3);
}

static size_t align_random(void) {
    const size_t options[] = {1, 2, 4, 8, 16, 32, 64, 3, 0, 24, 128, 17};
    return sound_next() % 3 == 0 ? options[sound_next() % (sizeof options / sizeof options[0])]
                                  : (size_t)1 << sound_below(7);
}

static size_t size_random(size_t capacity) {
    if (sound_next() % 16 == 0) return (size_t)-1 - sound_below(3); // the sums that overflow
    return (size_t)sound_between(0, (int64_t)capacity + 8);
}

SOUND_MAIN({
    static _Alignas(smith_arena_align_max) uint8_t memory[1024];
    SOUND_RUN("measure", sound_count, {
        SmithArena arena;
        arena_make_random(&arena, memory, sizeof memory);
        const size_t size = size_random(arena.capacity), align = align_random();
        size_t padding = 0;
        SOUND_NOTE("measure(used %zu of %zu, size %zu, align %zu)", arena.used, arena.capacity, size, align);
        (void)smith_arena_measure(&arena, size, align, &padding);
    });
    SOUND_RUN("measure (padding is the arena's own field)", sound_count, {
        SmithArena arena;
        arena_make_random(&arena, memory, sizeof memory);
        const size_t size = size_random(arena.capacity), align = align_random();
        SOUND_NOTE("measure(used %zu, size %zu, align %zu, &a field of the arena)", arena.used, size, align);
        size_t *const fields[] = {&arena.scopes, &arena.used, &arena.touched, &arena.capacity};
        (void)smith_arena_measure(&arena, size, align, fields[sound_next() % 4]);
    });
    SOUND_RUN("fits", sound_count, {
        SmithArena arena;
        arena_make_random(&arena, memory, sizeof memory);
        const size_t size = size_random(arena.capacity), align = align_random();
        SOUND_NOTE("fits(used %zu of %zu, size %zu, align %zu)", arena.used, arena.capacity, size, align);
        (void)smith_arena_fits(&arena, size, align);
    });
    SOUND_RUN("reset", sound_count, {
        SmithArena arena;
        arena_make_random(&arena, memory, sizeof memory);
        arena.scopes = sound_next() % 8 == 0 ? 1 : 0;
        SOUND_NOTE("reset(used %zu of %zu, scopes %zu)", arena.used, arena.capacity, arena.scopes);
        smith_arena_reset(&arena);
    });
    SOUND_RUN("push (trusted: its ensures)", sound_count, {
        SmithArena arena;
        arena_make_random(&arena, memory, sizeof memory);
        const size_t size = (size_t)sound_between(0, 64), align = align_random();
        if (smith_arena_fits(&arena, size, align)) {
            SOUND_NOTE("push(used %zu of %zu, size %zu, align %zu)", arena.used, arena.capacity, size, align);
            (void)smith_arena_push(&arena, size, align);
        }
    });
    SOUND_RUN("push_array (trusted: its ensures)", sound_count, {
        SmithArena arena;
        arena_make_random(&arena, memory, sizeof memory);
        const size_t size = (size_t)sound_between(1, 16), count = (size_t)sound_between(0, 8), align = align_random();
        if (smith_arena_fits(&arena, size * count, align)) {
            SOUND_NOTE("push_array(used %zu of %zu, size %zu, count %zu, align %zu)", arena.used, arena.capacity, size,
                       count, align);
            (void)smith_arena_push_array(&arena, size, count, align);
        }
    });
})
