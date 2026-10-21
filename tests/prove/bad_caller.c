// Calls clamp (tests/prove/clamp.c) with bounds the wrong way round.
#include <stdint.h>

int32_t clamp(int32_t x, int32_t low, int32_t high);

int32_t pinned(int32_t x) {
    return clamp(x, 10, 5);
}
