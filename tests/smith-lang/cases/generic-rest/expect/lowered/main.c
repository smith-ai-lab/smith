#include "arguments.h"
#include <stdio.h>

int main() {
__INT32_TYPE__ a =smith_call_failed;
__INT32_TYPE__ b =__extension__({extern __INT32_TYPE__ arguments_total__(void)__asm__("_arguments::total()");arguments_total__();});
__INT32_TYPE__ c =smith_call_failed;
__INT32_TYPE__ d =smith_call_failed;
__INT32_TYPE__ e =smith_call_failed;
__INT32_TYPE__ f =smith_call_failed;
__INT32_TYPE__ g =smith_call_failed;
__INT32_TYPE__ h =__extension__({extern __INT32_TYPE__ arguments_shapes__(void)__asm__("_arguments::shapes()");arguments_shapes__();});
__INT32_TYPE__ k =smith_call_failed;
printf("%d %d %d | %d | %d %d | %d %d | %d\n", a, b, c, d, e, f, g, h, k);
return 0;
}
extern __INT32_TYPE__ arguments_total__(void)__asm__("_arguments::total()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ arguments_total__(void){__INT32_TYPE__ sum=0;return sum;}
extern __INT32_TYPE__ arguments_shapes__(void)__asm__("_arguments::shapes()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ arguments_shapes__(void){__INT32_TYPE__ sum=0;return sum;}
