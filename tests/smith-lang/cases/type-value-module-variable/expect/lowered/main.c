#include <stdio.h>

static constexpr Type main_Big = __UINT32_TYPE__;
static constexpr Type main_Small = float;
static main_Big main_total = 7;
__INT32_TYPE__ main(void) {
main_Big x = 1;
static_assert(sizeof(main_Big) == 4);
main_Small y = 2.5;
printf("%u %u %.1f\n", (__UINT32_TYPE__)x, (__UINT32_TYPE__)main_total, (double)y);
return 0;
}
