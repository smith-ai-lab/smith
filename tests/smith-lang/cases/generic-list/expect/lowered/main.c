#include "lists.h"
#include <stdio.h>

int main(void) {
__INT32_TYPE__ a =smith_call_failed;
__INT32_TYPE__ b =__extension__({extern __INT32_TYPE__ lists_total__(void)__asm__("_lists::total()");lists_total__();});
__INT32_TYPE__ c =smith_call_failed;
__INT32_TYPE__ d =smith_call_failed;
__INT32_TYPE__ e =smith_call_failed;
__INT32_TYPE__ f =smith_call_failed;
__INT32_TYPE__ g =smith_call_failed;
__INT32_TYPE__ h =__extension__({extern __INT32_TYPE__ lists_shapes__(void)__asm__("_lists::shapes()");lists_shapes__();});
__INT32_TYPE__ k =smith_call_failed;
printf("%d %d %d | %d | %d %d | %d %d | %d\n", a, b, c, d, e, f, g, h, k);
return 0;
}
extern __INT32_TYPE__ lists_total__(void)__asm__("_lists::total()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ lists_total__(void){__INT32_TYPE__ sum=0;return sum;}
extern __INT32_TYPE__ lists_shapes__(void)__asm__("_lists::shapes()");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ lists_shapes__(void){[[maybe_unused]]auto both_0_0=7;__INT32_TYPE__ sum=0;{sum+=(__INT32_TYPE__)both_0_0*(__INT32_TYPE__)1;}{sum+=1000;}return sum+(__INT32_TYPE__)0;}
