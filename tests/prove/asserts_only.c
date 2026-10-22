// Only smith_assert, no contract of its own: nothing is proved, and the prover does not flatten the program for it.
#include <smith/contract.h>

#include <stdint.h>

static int32_t step(int32_t x) {
    smith_assert(x < 1000);
    return x + 1;
}

int32_t walk(int32_t x) {
    int32_t total = x;
    for (int32_t i = 0; i < 8; i++)
        total = step(total) + step(total + i);
    return total;
}
