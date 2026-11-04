#include <stdint.h>
#include <stdio.h>

#include "arr.h"

int main(void) {
    int32_t xs[4] = {7, 8, 9, 10};
    int32_t m[3][2] = {{1, 2}, {3, 4}, {5, 6}};
    printf("%d %d %d\n", arr_pick(xs, 2), arr_corner(m), arr_pick_last(xs));
    return 0;
}
