// @NAME@. `smith run` builds and runs it.
#include "primes.h"

#include <inttypes.h>
#include <smith/contract.h>
#include <stdint.h>
#include <stdio.h>

// The edge of the program: the proof stops here, since printf has no contract.
static void report(int32_t sum) {
    smith_trusted();
    (void)printf( // NOLINT(smith-effects): smith::console has no C call that formats; the lint goes with #240
        "the first 5 primes add up to %" PRId32 "\nhello from @NAME@\n", sum);
}

int main(void) {
    int32_t sum = primes_sum(5);
    smith_assert(sum <= 28);
    report(sum);
    return 0;
}
