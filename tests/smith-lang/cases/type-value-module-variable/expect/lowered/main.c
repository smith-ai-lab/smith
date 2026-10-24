#include <stdio.h>

static constexpr type main_big = __UINT32_TYPE__;
static constexpr type main_small = float;
static main_big main_total = 7;
__INT32_TYPE__ main() {
main_big x = 1;
static_assert(sizeof(main_big) == 4);
main_small y = 2.5;
printf("%u %u %.1f\n", (__UINT32_TYPE__)x, (__UINT32_TYPE__)main_total, (double)y);
return 0;
}
