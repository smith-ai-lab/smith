#include "kind.h"
#include <stdio.h>
typedef struct {
__INT32_TYPE__ x;
__INT32_TYPE__ y;
} Point;
int main(void) {
float f = 1.5f;
__UINT64_TYPE__ big = 9;
__INT32_TYPE__ small = -3;
__UINT8_TYPE__ byte = 7;
Point p = {10, 20};
printf("%d %d %d %d %d\n", kind_describe(float, f), kind_describe(__UINT64_TYPE__, big), kind_describe(__INT32_TYPE__ *, &small),
kind_describe(__UINT8_TYPE__, byte), kind_describe(Point, p));
printf("%d %d %d\n", kind_describe(__INT32_TYPE__, small), kind_floats_only(float, f), kind_floats_only(__INT32_TYPE__, small));
printf("%d %d %d\n", kind_bytes(__UINT8_TYPE__), kind_bytes(__UINT64_TYPE__), kind_bytes(Point));
return 0;
}
