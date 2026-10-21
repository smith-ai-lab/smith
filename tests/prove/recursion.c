// Recursion is not proved: it is an error, with the way out.
#include <smith/contract.h>

#include <stdint.h>

int32_t countdown(int32_t n) {
    smith_requires(0 <= n && n < 100);
    if (n == 0) return 0;
    return countdown(n - 1);
}
