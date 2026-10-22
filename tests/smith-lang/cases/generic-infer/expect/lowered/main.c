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
printf("%d %g\n",smith_call_failed,smith_call_failed);
printf("%d %d %d\n",smith_call_failed,smith_call_failed,smith_call_failed);
printf("%d %g %zu\n",smith_call_failed,smith_call_failed,smith_call_failed);
printf("%d %zu\n",smith_call_failed,smith_call_failed);
__INT32_TYPE__ slot = 0;smith_call_failed;
printf("%d %d\n", slot,__extension__({extern __INT32_TYPE__ pick_twice__i32(__INT32_TYPE__ value)__asm__("_pick::twice(__INT32_TYPE__)");pick_twice__i32(slot);}));
return 0;
}
extern __INT32_TYPE__ pick_twice__i32(__INT32_TYPE__ value)__asm__("_pick::twice(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ pick_twice__i32(__INT32_TYPE__ value){return __extension__({extern __INT32_TYPE__ pick_larger__i32(__INT32_TYPE__ a,__INT32_TYPE__ b)__asm__("_pick::larger(__INT32_TYPE__)");pick_larger__i32(value,value);});}
extern __INT32_TYPE__ pick_larger__i32(__INT32_TYPE__ a,__INT32_TYPE__ b)__asm__("_pick::larger(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ pick_larger__i32(__INT32_TYPE__ a,__INT32_TYPE__ b){return a>b?a:b;}
