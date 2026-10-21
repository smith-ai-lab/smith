#include <stdio.h>

#include "math/small.h"

unsigned cpp_twice(unsigned x);

int main(void) {
    printf("%u %u %u %u\n", math_small_twice(4), math_small_clamp(500, 1, 9), math_small_clamp_twice(70),
           math_small_regular(3));
    printf("%u\n", cpp_twice(21));
    return 0;
}
