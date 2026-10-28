#include "pool.h"
#include <stdio.h>
typedef char *Text;
constexpr Type Element = double;
constexpr Type kinds[3] = {__UINT16_TYPE__, __INT64_TYPE__, Text};
typedef struct {
constexpr Type of;
__INT32_TYPE__ count;
} Column;
constexpr Column column = {.of = __UINT8_TYPE__, .count = 3};
int main() {
constexpr Type T = __INT32_TYPE__;
T a = 7;
Element b = 2.5;
constexpr Type Second = kinds[1];
constexpr Type Of = column.of;
Second c = 1;
Of d = 200;
T *p = &a;
(void)p;
printf("%d %g %lld %d\n", a, b, (long long)c, d);
printf("%zu %zu %zu\n",__extension__({extern __SIZE_TYPE__ pool_width__T(void)__asm__("_pool::width(T)");pool_width__T();}),__extension__({extern __SIZE_TYPE__ pool_width__kinds_x_0_x(void)__asm__("_pool::width(kinds [ 0 ])");pool_width__kinds_x_0_x();}),__extension__({extern __SIZE_TYPE__ pool_width__column_x_of(void)__asm__("_pool::width(column . of)");pool_width__column_x_of();}));
printf("%d\n", (int)(Second)1);
return 0;
}
extern __SIZE_TYPE__ pool_width__T(void)__asm__("_pool::width(T)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ pool_width__T(void){return sizeof(T);}
extern __SIZE_TYPE__ pool_width__kinds_x_0_x(void)__asm__("_pool::width(kinds [ 0 ])");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ pool_width__kinds_x_0_x(void){return sizeof(kinds[0]);}
extern __SIZE_TYPE__ pool_width__column_x_of(void)__asm__("_pool::width(column . of)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ pool_width__column_x_of(void){return sizeof(column.of);}
