// @NAME@. `smith run` builds and runs it.
#include "primes.h"

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    int32_t sum = primes_sum(5);
    assert(sum == 28);
    printf("the first 5 primes add up to %" PRId32 "\n", sum);
    puts("hello from @NAME@");
    return 0;
}
