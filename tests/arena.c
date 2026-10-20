// Tests of <smith/arena.h>, run by tests/arena.sh: with no argument, the checks below; with an argument, one stop:
//   stop  a push past the capacity (built with -DNDEBUG, it must still stop with a message)
//   oob   a write just past a push (built with -fsanitize=address, ASan must catch it)
#include <smith/arena.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

constexpr size_t test_capacity = 4096;

typedef struct {
    alignas(smith_arena_align_max) unsigned char bytes[test_capacity];
} TestMemory;

static void test_zeroed(void) {
    static TestMemory memory;
    memset(memory.bytes, 0xAA, sizeof memory.bytes);
    SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
    unsigned char *bytes = smith_arena_push(&arena, 100, 1);
    for (size_t i = 0; i < 100; i++)
        assert(bytes[i] == 0);
    memset(bytes, 0xBB, 100);
    smith_arena_reset(&arena);
    unsigned char *again = smith_arena_push(&arena, 100, 1);
    for (size_t i = 0; i < 100; i++)
        assert(again[i] == 0); // a push after a reset is zeroed too
}

static void test_alignment(void) {
    static TestMemory memory;
    SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
    for (size_t align = 1; align <= smith_arena_align_max; align *= 2) {
        unsigned char *odd = smith_arena_push(&arena, 1, 1); // knock the cursor off every alignment
        assert(odd != nullptr);
        void *aligned = smith_arena_push(&arena, 3, align);
        assert(((uintptr_t)aligned & (align - 1)) == 0);
    }
}

typedef struct {
    double value;
    int count;
} TestRecord;

static void test_typed_and_array(void) {
    static TestMemory memory;
    SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
    TestRecord *record = smith_arena_push(&arena, sizeof(TestRecord), alignof(TestRecord));
    assert(record->count == 0 && record->value == 0);
    TestRecord *records = smith_arena_push_array(&arena, sizeof(TestRecord), 10, alignof(TestRecord));
    for (size_t i = 0; i < 10; i++) {
        assert(records[i].count == 0);
        records[i].count = (int)i; // all ten are writable
    }
    assert(((uintptr_t)records & (alignof(TestRecord) - 1)) == 0);
    unsigned char *none = smith_arena_push_array(&arena, 8, 0, 8); // an empty array is a valid push
    assert(none != nullptr);
}

static void test_macros(void) {
    static TestMemory memory;
    memset(memory.bytes, 0xAA, sizeof memory.bytes);
    SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
    unsigned char *odd = smith_arena_push(&arena, 1, 1);
    assert(odd != nullptr);
    TestRecord *record = SMITH_ARENA_PUSH(&arena, TestRecord);
    assert(((uintptr_t)record & (alignof(TestRecord) - 1)) == 0);
    assert(record->count == 0 && record->value == 0);
    TestRecord *records = SMITH_ARENA_PUSH_ARRAY(&arena, TestRecord, 5);
    assert(((uintptr_t)records & (alignof(TestRecord) - 1)) == 0);
    for (size_t i = 0; i < 5; i++)
        assert(records[i].count == 0 && records[i].value == 0);
    long double *wide = SMITH_ARENA_PUSH(&arena, long double);
    assert(((uintptr_t)wide & (alignof(long double) - 1)) == 0 && *wide == 0);
}

static void test_fits(void) {
    static TestMemory memory;
    SmithArena arena = smith_arena_make(memory.bytes, 128);
    assert(smith_arena_fits(&arena, 128, 1));
    assert(!smith_arena_fits(&arena, 129, 1));
    assert(!smith_arena_fits(&arena, SIZE_MAX, 1));       // no overflow in the sum
    unsigned char *head = smith_arena_push(&arena, 8, 8); // leaves the cursor on a granule, with or without ASan
    assert(head != nullptr);
    size_t left = arena.capacity - arena.used;
    assert(smith_arena_fits(&arena, left, 1));
    assert(!smith_arena_fits(&arena, left + 1, 1));
    unsigned char *rest = smith_arena_push(&arena, left, 1); // exactly the room that fits
    assert(rest != nullptr);
    assert(arena.used == arena.capacity);
    assert(smith_arena_fits(&arena, 0, 1));
    assert(!smith_arena_fits(&arena, 1, 1));
}

static void test_reset_and_scopes(void) {
    static TestMemory memory;
    SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
    unsigned char *base = smith_arena_push(&arena, 10, 1);
    assert(base != nullptr);
    size_t base_used = arena.used;
    SmithArenaScope outer = smith_arena_scope_begin(&arena);
    unsigned char *first = smith_arena_push(&arena, 20, 1);
    assert(first != nullptr);
    size_t outer_used = arena.used;
    SmithArenaScope inner = smith_arena_scope_begin(&arena);
    assert(arena.scopes == 2);
    unsigned char *second = smith_arena_push(&arena, 30, 1);
    assert(second != nullptr);
    smith_arena_scope_end(inner);
    assert(arena.used == outer_used && arena.scopes == 1);
    unsigned char *third = smith_arena_push(&arena, 30, 1);
    assert(third == second); // the inner scope's memory is handed out again
    smith_arena_scope_end(outer);
    assert(arena.used == base_used && arena.scopes == 0);
    smith_arena_reset(&arena);
    assert(arena.used == 0);
}

// The same pushes lay out the same way in two arenas, whatever their addresses.
static void test_determinism(void) {
    static TestMemory first_memory;
    static TestMemory second_memory;
    SmithArena first = smith_arena_make(first_memory.bytes, sizeof first_memory.bytes);
    SmithArena second = smith_arena_make(second_memory.bytes, sizeof second_memory.bytes);
    const size_t sizes[] = {1, 7, 64, 3, 100, 8};
    const size_t aligns[] = {1, 4, 64, 2, 16, 8};
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        unsigned char *a = smith_arena_push(&first, sizes[i], aligns[i]);
        unsigned char *b = smith_arena_push(&second, sizes[i], aligns[i]);
        assert(a - first.memory == b - second.memory);
    }
    assert(first.used == second.used);
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "stop") == 0) {
        static TestMemory memory;
        SmithArena arena = smith_arena_make(memory.bytes, 128);
        unsigned char *first = smith_arena_push(&arena, 100, 1);
        unsigned char *second = smith_arena_push(&arena, 100, 1); // does not fit: stops
        printf("not reached %p %p\n", (void *)first, (void *)second);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "oob") == 0) {
        static TestMemory memory;
        SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
        volatile unsigned char *bytes = smith_arena_push(&arena, 10, 1);
        bytes[10] = 1; // one past the push
        printf("not reached\n");
        return 0;
    }
    test_zeroed();
    test_alignment();
    test_typed_and_array();
    test_macros();
    test_fits();
    test_reset_and_scopes();
    test_determinism();
    printf("arena: all checks passed\n");
    return 0;
}
