#include "pool.h"
#include <stdio.h>
typedef char *Text;
constexpr type element = double;
constexpr type kinds[3] = {__UINT16_TYPE__, __INT64_TYPE__, Text};
typedef struct {
constexpr type of;
__INT32_TYPE__ count;
} Column;
constexpr Column column = {.of = __UINT8_TYPE__, .count = 3};
int main(void) {
constexpr type t = __INT32_TYPE__;
t a = 7;
element b = 2.5;
constexpr type second = kinds[1];
constexpr type of = column.of;
second c = 1;
of d = 200;
t *p = &a;
(void)p;
printf("%d %g %lld %d\n", a, b, (long long)c, d);
printf("%zu %zu %zu\n",__extension__({extern __SIZE_TYPE__ pool_width__t(void)__asm__("_pool::width(t)");pool_width__t();}),__extension__({extern __SIZE_TYPE__ pool_width__kinds_x_0_x(void)__asm__("_pool::width(kinds [ 0 ])");pool_width__kinds_x_0_x();}),__extension__({extern __SIZE_TYPE__ pool_width__column_x_of(void)__asm__("_pool::width(column . of)");pool_width__column_x_of();}));
printf("%d\n", (int)(second)1);
return 0;
}
extern __SIZE_TYPE__ pool_width__t(void)__asm__("_pool::width(t)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ pool_width__t(void){return sizeof(t);}
extern __SIZE_TYPE__ pool_width__kinds_x_0_x(void)__asm__("_pool::width(kinds [ 0 ])");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ pool_width__kinds_x_0_x(void){return sizeof(kinds[0]);}
extern __SIZE_TYPE__ pool_width__column_x_of(void)__asm__("_pool::width(column . of)");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ pool_width__column_x_of(void){return sizeof(column.of);}
