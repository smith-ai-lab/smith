// Calls clamp (tests/prove/clamp.c) with bounds the wrong way round. It has a contract of its own, so it is proved.
#include <smith/contract.h>

#include <stdint.h>

int32_t clamp(int32_t x, int32_t low, int32_t high);

int32_t pinned(int32_t x) {
    smith_requires(x >= 0);
    return clamp(x, 10, 5);
}
