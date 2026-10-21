// Signed overflow is undefined behaviour, which UBSan's check turns into a branch the prover must show unreachable.
#include <smith/contract.h>

#include <stdint.h>

int32_t add_unchecked(int32_t a, int32_t b) {
    smith_requires(0 <= a);
    return a + b;
}

int32_t add(int32_t a, int32_t b) {
    smith_requires(0 <= a && a <= 1000000);
    smith_requires(0 <= b && b <= 1000000);
    int32_t sum = a + b;
    smith_ensures(a <= sum && b <= sum);
    return sum;
}

// A promise the code does not keep.
int32_t add_wrong(int32_t a, int32_t b) {
    smith_requires(0 <= a && a <= 1000000);
    smith_requires(0 <= b && b <= 1000000);
    int32_t sum = a + b;
    smith_ensures(sum < a + b);
    return sum;
}
