// Tests of <smith/arena.h> that need a process and a build of their own, run by tests/arena_c_test.smith with one
// argument (the behavior checks are lib/smith/arena_test.smith, run by build.c's `test-units`):
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
    return smith_arena_fixed(memory, test_reservation);
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
        SmithArena parent = smith_arena_fixed(memory.bytes, 1024);
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
        SmithArena arena = smith_arena_fixed(memory.bytes, sizeof memory.bytes);
        volatile unsigned char *bytes = smith_arena_push(&arena, 10, 1);
        smith_arena_reset(&arena);
        printf("not reached %d\n", bytes[0]);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "stop") == 0) {
        static TestMemory memory;
        SmithArena arena = smith_arena_fixed(memory.bytes, 128);
        unsigned char *first = smith_arena_push(&arena, 100, 1);
        unsigned char *second = smith_arena_push(&arena, 100, 1); // does not fit: stops
        printf("not reached %p %p\n", (void *)first, (void *)second);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "oob") == 0) {
        static TestMemory memory;
        SmithArena arena = smith_arena_fixed(memory.bytes, sizeof memory.bytes);
        volatile unsigned char *bytes = smith_arena_push(&arena, 10, 1);
        bytes[10] = 1; // one past the push
        printf("not reached\n");
        return 0;
    }
    fprintf(stderr, "usage: arena <mode>\n");
    return 2;
}
