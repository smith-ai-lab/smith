// @NAME@. `smith build run` builds and runs it.
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    constexpr int32_t primes[] = {2, 3, 5, 7, 11};
    int32_t sum = 0;
    for (size_t i = 0; i < _Countof(primes); i++) {
        sum += primes[i];
    }
    assert(sum == 28);
    printf("the first %zu primes add up to %" PRId32 "\n", _Countof(primes), sum);
    puts("hello from @NAME@");
    return 0;
}
