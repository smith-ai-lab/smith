// A loop with no constant bound, and nothing to unroll.
#include <smith/contract.h>

#include <stdint.h>

int32_t drain(int32_t n) {
    smith_requires(0 <= n);
    while (n > 0) n--;
    smith_ensures(n == 0);
    return n;
}
