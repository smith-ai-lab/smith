#include "later.h"
#include <stdio.h>

static void main_show(__INT32_TYPE__ a, __INT64_TYPE__ b) {
printf("show %d %ld\n", a, (long)b);
}
static void main_scale(double x) {
printf("scale %.1f\n", x);
}
__INT32_TYPE__ main(void) {
later_Slot a;
later_Slot b;
later_Slot c;smith_call_failed;smith_call_failed;smith_call_failed;
a.entry(a.data);
b.entry(b.data);
c.entry(c.data);
printf("%d %.1f %d\n",__extension__({extern __INT32_TYPE__ later_doubled__i32(__INT32_TYPE__ x)__asm__("_later::doubled(__INT32_TYPE__)");later_doubled__i32(21);}), (double)__extension__({extern float later_doubled__f32(float x)__asm__("_later::doubled(float)");later_doubled__f32(1.5f);}),smith_call_failed);
return 0;
}
static __INT32_TYPE__ later_doubled__i32__twice(__INT32_TYPE__ v){}extern __INT32_TYPE__ later_doubled__i32(__INT32_TYPE__ x)__asm__("_later::doubled(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ later_doubled__i32(__INT32_TYPE__ x){return later_doubled__i32__twice(x);}
static float later_doubled__f32__twice(float v){}extern float later_doubled__f32(float x)__asm__("_later::doubled(float)");
[[gnu::weak]][[gnu::visibility("hidden")]]float later_doubled__f32(float x){return later_doubled__f32__twice(x);}
