#include "pick.h"
#include <stdio.h>
typedef struct {
__INT32_TYPE__ x;
__INT32_TYPE__ y;
} Point;
static double half(double v) {
return v / 2;
}
int main(void) {
__INT32_TYPE__ a = 3;
__INT32_TYPE__ b = 8;
double d = 2.5;
Point p = {10, 20};
Point *pp = &p;
__INT32_TYPE__ numbers[3] = {7, 8, 9};
__UINT8_TYPE__ byte = 200;
const __INT32_TYPE__ fixed = 4;
printf("%d %g\n", pick_larger(a, b), pick_larger(d, 1.5));
printf("%d %d %d\n", pick_larger(p.x, pp->y), pick_larger(p.x, 3), pick_larger(numbers[1], fixed));
printf("%d %g %zu\n", pick_larger(a, 100), pick_larger(half(9), d), pick_size_of_value(byte));
printf("%d %zu\n", pick_first_of(numbers, 3), pick_size_of_value((__UINT16_TYPE__)3));
__INT32_TYPE__ slot = 0;
pick_set(&slot, 42);
printf("%d %d\n", slot, pick_twice(__INT32_TYPE__, slot));
return 0;
}
