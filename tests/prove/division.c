// A proof the solver cannot finish in time: 64-bit division and multiplication.
#include <smith/contract.h>

#include <stdint.h>

uint64_t divmod(uint64_t x, uint64_t y) {
    smith_requires(y != 0);
    uint64_t q = x / y;
    uint64_t r = x % y;
    smith_ensures(q * y + r == x);
    return q;
}
