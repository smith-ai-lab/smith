#include "primes.h"

#include <smith/contract.h>

int32_t primes_sum(size_t count) {
    smith_requires(count <= 5);
    constexpr int32_t primes[] = {2, 3, 5, 7, 11};
    int32_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += primes[i];
    }
    smith_ensures(sum <= 28);
    return sum;
}
