// A function with only smith_assert is in the default set: the assert is proved from what is known at that point, and
// callers still inline the function (it has no smith_ensures).
#include <smith/contract.h>

#include <stdint.h>

int32_t masked(int32_t x) {
    int32_t low = x & 7;
    smith_assert(low < 8);
    return low;
}

int32_t unmasked(int32_t x) {
    smith_assert(x < 1000);
    return x;
}

int main(void) {
    return masked(3) + unmasked(5);
}
