#include "../include/smith/arena.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Sanitizer builds start each push on an ASan granule and leave a poisoned gap after it, and poison the memory past the
// cursor, so ASan catches a read or write outside the pushes. The poison is lazy. An arena may be a 64 GiB reservation,
// and poisoning all of it would commit its shadow (an eighth of it, 8 GiB) before the program pushed anything. So the
// arena keeps a high-water mark `touched`, with the invariant that [used, touched) is poisoned and the memory past
// `touched` has not been poisoned by this arena. Every push keeps touched >= used + window by poisoning [touched,
// used + window) (capped at the capacity); reset and the end of a scope poison [used, touched) again. Shadow memory
// then grows with what the program has used, not with the capacity. The window is 64 KiB: an overflow past the top
// of the arena lands in poison unless it runs more than 64 KiB past the last push, which is longer than the overruns
// that come from off-by-one, off-by-a-row and short-array mistakes, while the shadow it costs is 8 KiB.
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>

constexpr size_t smith_arena_poison_granule = 8;               // ASan tracks memory in 8-byte granules
constexpr size_t smith_arena_poison_gap = 16;                  // poisoned bytes left after each push
constexpr size_t smith_arena_poison_window = (size_t)64 << 10; // poisoned past the cursor after each push

// Poisons [used, touched) again, after a reset or the end of a scope.
static void smith_arena_poison_unused(const SmithArena *arena) {
    assert(arena->used <= arena->touched && arena->touched <= arena->capacity);
    ASAN_POISON_MEMORY_REGION(arena->memory + arena->used, arena->touched - arena->used);
}

// Keeps touched >= used + window, poisoning what is new.
static void smith_arena_poison_ahead(SmithArena *arena) {
    size_t room = arena->capacity - arena->used;
    size_t target = arena->used + (room < smith_arena_poison_window ? room : smith_arena_poison_window);
    if (target <= arena->touched) return;
    ASAN_POISON_MEMORY_REGION(arena->memory + arena->touched, target - arena->touched);
    arena->touched = target;
}
#endif

static_assert((smith_arena_align_max & (smith_arena_align_max - 1)) == 0); // a power of two

[[maybe_unused]] static bool smith_arena_align_valid(size_t align) {
    return align > 0 && align <= smith_arena_align_max && (align & (align - 1)) == 0;
}

// The alignment a push really uses: a sanitizer build starts every push on a granule.
static size_t smith_arena_effective_align(size_t align) {
#if __has_feature(address_sanitizer)
    if (align < smith_arena_poison_granule) return smith_arena_poison_granule;
#endif
    return align;
}

// Whether size bytes at align fit; the bytes of padding go to *padding. Written so that no sum can overflow.
static bool smith_arena_measure(const SmithArena *arena, size_t size, size_t align, size_t *padding) {
    assert(arena != nullptr);
    assert(arena->used <= arena->capacity);
    assert(smith_arena_align_valid(align));
    align = smith_arena_effective_align(align);
    *padding = (align - (arena->used & (align - 1))) & (align - 1);
    size_t room = arena->capacity - arena->used;
    return *padding <= room && size <= room - *padding;
}

// A push that cannot be served stops here in every build, so a build with NDEBUG never writes past the memory.
[[noreturn]] static void smith_arena_stop(const SmithArena *arena, size_t size, size_t align) {
    fprintf(stderr, "smith: arena out of capacity: push of %zu bytes (align %zu) with %zu of %zu bytes used\n", size,
            align, arena->used, arena->capacity);
    fflush(stderr);
    abort();
}

SmithArena smith_arena_make(void *memory, size_t capacity) {
    assert(memory != nullptr);
    assert(((uintptr_t)memory & (smith_arena_align_max - 1)) == 0);
    assert(capacity > 0);
    return (SmithArena){.memory = memory, .capacity = capacity}; // nothing is poisoned until the first push
}

bool smith_arena_fits(const SmithArena *arena, size_t size, size_t align) {
    size_t padding = 0;
    return smith_arena_measure(arena, size, align, &padding);
}

void *smith_arena_push(SmithArena *arena, size_t size, size_t align) {
    size_t padding = 0;
    if (!smith_arena_measure(arena, size, align, &padding)) smith_arena_stop(arena, size, align);

    unsigned char *result = arena->memory + arena->used + padding;
    arena->used += padding + size;
#if __has_feature(address_sanitizer)
    size_t gap = arena->capacity - arena->used;
    arena->used += gap < smith_arena_poison_gap ? gap : smith_arena_poison_gap;
    smith_arena_poison_ahead(arena);           // poisons the padding, the new memory and the gap...
    ASAN_UNPOISON_MEMORY_REGION(result, size); // ...and the push alone is made addressable
#endif
    memset(result, 0, size);

    assert(arena->used <= arena->capacity);
    return result;
}

void *smith_arena_push_array(SmithArena *arena, size_t size, size_t count, size_t align) {
    assert(arena != nullptr);
    if (size > 0 && count > SIZE_MAX / size) {
        fprintf(stderr, "smith: arena out of capacity: %zu items of %zu bytes overflow size_t (capacity %zu)\n", count,
                size, arena->capacity);
        fflush(stderr);
        abort();
    }
    return smith_arena_push(arena, size * count, align);
}

SmithArena smith_arena_carve(SmithArena *parent, size_t capacity) {
    assert(parent != nullptr && capacity > 0);
    assert(parent->scopes == 0); // ending a scope would give the carved memory back
    size_t start = (parent->used + smith_arena_align_max - 1) & ~(smith_arena_align_max - 1);
    if (start > parent->capacity || capacity > parent->capacity - start)
        smith_arena_stop(parent, capacity, smith_arena_align_max);
    unsigned char *memory = parent->memory + start;
    parent->used = start + capacity;
#if __has_feature(address_sanitizer)
    // The parent poisoned a window past its old cursor, which the carved memory now covers: it is the new arena's, and
    // it poisons for itself. The parent's poison resumes past the carve.
    if (parent->touched > start)
        ASAN_UNPOISON_MEMORY_REGION(memory, (parent->touched < parent->used ? parent->touched : parent->used) - start);
    if (parent->touched < parent->used) parent->touched = parent->used;
#endif
    return smith_arena_make(memory, capacity);
}

void smith_arena_reset(SmithArena *arena) {
    assert(arena != nullptr);
    assert(arena->used <= arena->capacity);
    assert(arena->scopes == 0); // a scope still open would restore a cursor past the reset
    arena->used = 0;
#if __has_feature(address_sanitizer)
    smith_arena_poison_unused(arena);
#endif
}

SmithArenaScope smith_arena_scope_begin(SmithArena *arena) {
    assert(arena != nullptr);
    assert(arena->used <= arena->capacity);
    assert(arena->scopes < SIZE_MAX);
    SmithArenaScope scope = {.arena = arena, .used = arena->used, .depth = arena->scopes};
    arena->scopes += 1;
    return scope;
}

void smith_arena_scope_end(SmithArenaScope scope) {
    assert(scope.arena != nullptr);
    assert(scope.arena->scopes > 0);                // more ends than begins
    assert(scope.depth == scope.arena->scopes - 1); // ends come last in, first out
    assert(scope.used <= scope.arena->used);
    scope.arena->scopes -= 1;
    scope.arena->used = scope.used;
#if __has_feature(address_sanitizer)
    smith_arena_poison_unused(scope.arena);
#endif
}
