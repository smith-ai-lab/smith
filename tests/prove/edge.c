// A proved function that runs into the edge of the program: a library function with no contract, called by a helper.
// The message names the call and the function to mark trusted.
#include <smith/contract.h>

#include <stdint.h>
#include <stdio.h>

static void show(int32_t value) {
    printf("%d\n", value);
}

int32_t shown_twice(int32_t x) {
    smith_requires(x >= 0 && x < 1000);
    show(x);
    int32_t y = x * 2;
    smith_ensures(y == x * 2);
    return y;
}
