// Tests of <smith/arena.h>, run by tests/arena.sh: with no argument, the checks below; with an argument, one stop:
//   stop  a push past the capacity (built with -DNDEBUG, it must still stop with a message)
//   oob   a write just past a push (built with -fsanitize=address, ASan must catch it)
//   big   a 64 GiB reservation made into an arena, and a few pushes: prints how much memory that made resident
//   bigoob, bigfar  the same arena, a write one byte past a push and 60,000 bytes past it (ASan must catch both)
//   stale  a read of a push after a reset (ASan must catch it)
//   carvelazy  two 1 GiB arenas carved out of a 64 GiB one: resident growth stays small
//   carveoob, carvefar, carvestale  the same three ASan catches in a carved arena
//   carvestop  a carve past the parent's capacity stops
#include <smith/arena.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include <mach/mach.h>

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
    TestRecord *record = smith_arena_push(&arena, sizeof(TestRecord), alignof(TestRecord));
    assert(((uintptr_t)record & (alignof(TestRecord) - 1)) == 0);
    assert(record->count == 0 && record->value == 0);
    TestRecord *records = smith_arena_push_array(&arena, sizeof(TestRecord), 5, alignof(TestRecord));
    assert(((uintptr_t)records & (alignof(TestRecord) - 1)) == 0);
    for (size_t i = 0; i < 5; i++)
        assert(records[i].count == 0 && records[i].value == 0);
    long double *wide = smith_arena_push(&arena, sizeof(long double), alignof(long double));
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

static size_t test_resident(void) {
    mach_task_basic_info_data_t info = {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    kern_return_t result = task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count);
    assert(result == KERN_SUCCESS);
    (void)result;
    return info.resident_size;
}

constexpr size_t test_reservation = (size_t)64 << 30;

static SmithArena test_big_arena(void) {
    void *memory =
        mmap(nullptr, test_reservation, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    assert(memory != MAP_FAILED);
    return smith_arena_make(memory, test_reservation);
}

static int test_big(void) {
    size_t before = test_resident();
    SmithArena arena = test_big_arena();
    unsigned char *bytes = smith_arena_push(&arena, 10, 1);
    bytes[9] = 1;
    size_t grown = test_resident() - before;
    printf("resident growth %zu KiB\n", grown >> 10);
    return grown < ((size_t)16 << 20) ? 0 : 1;
}

// Carved arenas are separate, aligned, laid out after what the parent used, and the parent goes on after them.
static void test_carve(void) {
    static TestMemory memory;
    SmithArena parent = smith_arena_make(memory.bytes, sizeof memory.bytes);
    unsigned char *before = smith_arena_push(&parent, 10, 1);
    SmithArena first = smith_arena_carve(&parent, 1000);
    SmithArena second = smith_arena_carve(&parent, 2000);
    unsigned char *after = smith_arena_push(&parent, 10, 1);
    assert(first.capacity == 1000 && second.capacity == 2000 && first.used == 0 && second.used == 0);
    assert((uintptr_t)first.memory % smith_arena_align_max == 0 &&
           (uintptr_t)second.memory % smith_arena_align_max == 0);
    assert(first.memory > before && second.memory >= first.memory + 1000 && after >= second.memory + 2000);
    assert(parent.used >= 3010);
    unsigned char *a = smith_arena_push_array(&first, sizeof(unsigned char), 900, alignof(unsigned char));
    unsigned char *b = smith_arena_push_array(&second, sizeof(unsigned char), 1900, alignof(unsigned char));
    memset(a, 1, 900);
    memset(b, 2, 1900);
    after[9] = 3;
    smith_arena_reset(&first); // a reset gives back its own memory only
    assert(b[1899] == 2 && after[9] == 3);
    unsigned char *again = smith_arena_push_array(&first, sizeof(unsigned char), 900, alignof(unsigned char));
    assert(again == a && again[0] == 0); // zeroed again
}

static SmithArena test_carved_arena(size_t capacity) {
    static SmithArena parent;
    parent = test_big_arena();
    return smith_arena_carve(&parent, capacity);
}

static int test_carve_lazy(void) {
    size_t before = test_resident();
    SmithArena parent = test_big_arena();
    SmithArena first = smith_arena_carve(&parent, (size_t)1 << 30);
    SmithArena second = smith_arena_carve(&parent, (size_t)1 << 30);
    unsigned char *a = smith_arena_push(&first, 10, 1);
    unsigned char *b = smith_arena_push(&second, 10, 1);
    a[9] = 1;
    b[9] = 2;
    unsigned char *c = smith_arena_push(&parent, 10, 1); // after both
    c[9] = 3;
    size_t grown = test_resident() - before;
    printf("resident growth %zu KiB after carving 2 GiB\n", grown >> 10);
    return grown < ((size_t)16 << 20) && c >= second.memory + ((size_t)1 << 30) ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "big") == 0) return test_big();
    if (argc > 1 && strcmp(argv[1], "carvelazy") == 0) return test_carve_lazy();
    if (argc > 1 && (strcmp(argv[1], "carveoob") == 0 || strcmp(argv[1], "carvefar") == 0)) {
        SmithArena arena = test_carved_arena((size_t)1 << 30);
        volatile unsigned char *bytes = smith_arena_push(&arena, 10, 1);
        bytes[strcmp(argv[1], "carveoob") == 0 ? 10 : 60000] = 1;
        printf("not reached\n");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "carvestale") == 0) {
        SmithArena arena = test_carved_arena((size_t)1 << 30);
        volatile unsigned char *bytes = smith_arena_push(&arena, 10, 1);
        smith_arena_reset(&arena);
        printf("not reached %d\n", bytes[0]);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "carvestop") == 0) {
        static TestMemory memory;
        SmithArena parent = smith_arena_make(memory.bytes, 1024);
        SmithArena child = smith_arena_carve(&parent, 2048);
        printf("not reached %zu\n", child.capacity);
        return 0;
    }
    if (argc > 1 && (strcmp(argv[1], "bigoob") == 0 || strcmp(argv[1], "bigfar") == 0)) {
        SmithArena arena = test_big_arena();
        volatile unsigned char *bytes = smith_arena_push(&arena, 10, 1);
        bytes[strcmp(argv[1], "bigoob") == 0 ? 10 : 60000] = 1;
        printf("not reached\n");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "stale") == 0) {
        static TestMemory memory;
        SmithArena arena = smith_arena_make(memory.bytes, sizeof memory.bytes);
        volatile unsigned char *bytes = smith_arena_push(&arena, 10, 1);
        smith_arena_reset(&arena);
        printf("not reached %d\n", bytes[0]);
        return 0;
    }
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
    test_carve();
    printf("arena: all checks passed\n");
    return 0;
}
