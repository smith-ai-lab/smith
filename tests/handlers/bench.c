// The cost of a malloc/free pair (issue #885, #239): with no handler, and under a bump handler that is reset after each
// pair (the arena's cost). Built with -fsanitize=address it is the pair under ASan. Prints nanoseconds per pair.
#include "smith/handlers.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static unsigned char space[1 << 12];

static void *bump_alloc(SmithAllocator *self, size_t size, size_t align) {
    (void)self, (void)align;
    return size <= sizeof space ? space : nullptr;
}

static void *bump_realloc(SmithAllocator *self, void *block, size_t size, size_t align) {
    (void)block;
    return bump_alloc(self, size, align);
}

static void bump_free(SmithAllocator *self, void *block) {
    (void)self, (void)block;
}

static bool bump_owns(SmithAllocator *self, const void *block) {
    (void)self;
    return block == space;
}

static double pair_nanoseconds(void) {
    constexpr int rounds = 20000000;
    struct timespec begin;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    for (int i = 0; i < rounds; i++) {
        void *volatile block = malloc(64);
        free(block);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    return ((double)(end.tv_sec - begin.tv_sec) * 1e9 + (double)(end.tv_nsec - begin.tv_nsec)) / rounds;
}

int main(void) {
    double best_none = 1e9;
    double best_handler = 1e9;
    SmithAllocator bump = {bump_alloc, bump_realloc, bump_free, bump_owns, nullptr};
    for (int run = 0; run < 5; run++) {
        double none = pair_nanoseconds();
        SmithAllocator *previous = smith_handlers_set_allocator(&bump);
        double handler = pair_nanoseconds();
        smith_handlers_set_allocator(previous);
        if (none < best_none) best_none = none;
        if (handler < best_handler) best_handler = handler;
    }
    printf("pair, no handler: %.2f ns; under a bump handler: %.2f ns\n", best_none, best_handler);
    return 0;
}
