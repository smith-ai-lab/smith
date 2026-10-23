// A function with a smith_requires and no smith_ensures is inlined where it is called, so a caller that may break its
// requires is refused at the requires, naming it (#74: this used to stop the prover with signal 11).
#include <smith/contract.h>

#include <stdint.h>

static uint32_t helper(uint32_t x) {
    smith_requires(x < 10);
    return x + 1;
}

uint32_t caller(uint32_t x) {
    smith_requires(x < 100);
    uint32_t y = helper(x);
    smith_ensures(true);
    return y;
}

uint32_t careful(uint32_t x) {
    smith_requires(x < 10);
    uint32_t y = helper(x);
    smith_ensures(y <= 10);
    return y;
}
