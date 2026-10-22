// smith_assert: a statement the prover proves where the function is part of a proof, as `assert` was.
#include <smith/contract.h>

#include <stdint.h>

int32_t next(int32_t x) {
    smith_requires(0 <= x && x < 100);
    smith_assert(x + 1 <= 100);
    int32_t result = x + 1;
    smith_ensures(result <= 100);
    return result;
}

int32_t too_sure(int32_t x) {
    smith_requires(0 <= x && x < 100);
    smith_assert(x < 50);
    int32_t result = x;
    smith_ensures(result >= 0);
    return result;
}
