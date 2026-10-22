#include "guard.h"
#include <stdio.h>
int main(void) {
printf("%d %g\n",__extension__({extern __INT32_TYPE__ guard_pick_one__i32(__INT32_TYPE__ a,__INT32_TYPE__ b)__asm__("_guard::pick_one(__INT32_TYPE__)");guard_pick_one__i32(3,9);}),__extension__({extern double guard_pick_one__f64(double a,double b)__asm__("_guard::pick_one(double)");guard_pick_one__f64(2.5,1.5);}));
return 0;
}
extern __INT32_TYPE__ guard_pick_one__i32(__INT32_TYPE__ a,__INT32_TYPE__ b)__asm__("_guard::pick_one(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ guard_pick_one__i32(__INT32_TYPE__ a,__INT32_TYPE__ b){}
extern double guard_pick_one__f64(double a,double b)__asm__("_guard::pick_one(double)");
[[gnu::weak]][[gnu::visibility("hidden")]]double guard_pick_one__f64(double a,double b){}
