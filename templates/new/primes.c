#include "primes.h"

#include <assert.h>

int32_t primes_sum(size_t count) {
    constexpr int32_t primes[] = {2, 3, 5, 7, 11};
    assert(count <= _Countof(primes));
    int32_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += primes[i];
    }
    return sum;
}
