#include <smith/contract.h>

#include <stdint.h>

int32_t checked(int32_t x) {
    smith_requires(x > 0);
    int32_t y = x * 2;
    smith_ensures(y > x);
    for (int32_t i = 0; i < 2; i++) smith_invariant(i < 2);
    return y;
}
