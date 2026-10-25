#include <smith/contract.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int32_t clamp(int32_t x, int32_t low, int32_t high) {
    smith_requires(low <= high);
    const int32_t old = x; // kept only for the contract below
    int32_t result = x < low ? low : x > high ? high : x;
    smith_ensures(low <= result && result <= high && (result == old || result == low || result == high));
    return result;
}

static int32_t twice(int32_t count) {
    int32_t total = 0;
    for (int32_t i = 0; i < count; i++) {
        smith_invariant(total == i * 2);
        total += 2;
    }
    return total;
}

int main(int argc, char **argv) {
    (void)argc;
    int32_t x = atoi(argv[1]);
    int32_t low = atoi(argv[2]);
    int32_t high = atoi(argv[3]);
    printf("%d %d\n", clamp(x, low, high), twice(high));
    return 0;
}
