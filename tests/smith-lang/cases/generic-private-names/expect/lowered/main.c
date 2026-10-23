#include "store.h"
#include <stdio.h>

int main(void) {
__INT64_TYPE__ a =__extension__({extern __INT64_TYPE__ store_total__i32(__INT32_TYPE__ a,__INT32_TYPE__ b)__asm__("_store::total(__INT32_TYPE__)");store_total__i32(4,5);});
__INT64_TYPE__ b =__extension__({extern __INT64_TYPE__ store_total__f64(double a,double b)__asm__("_store::total(double)");store_total__f64(1.5,2.5);});
printf("%lld %lld %lld\n", (long long)a, (long long)b, (long long)store_current_weight());
return 0;
}
extern __INT64_TYPE__ store_total__i32(__INT32_TYPE__ a,__INT32_TYPE__ b)__asm__("_store::total(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT64_TYPE__ store_total__i32(__INT32_TYPE__ a,__INT32_TYPE__ b){store__Tally tally=store__start();tally=store__step(tally,(__INT64_TYPE__)__extension__({extern __INT32_TYPE__ store__twice__i32(__INT32_TYPE__ value)__asm__("_store::twice(__INT32_TYPE__)");store__twice__i32(a);}),store__mode_add);tally=store__step(tally,(__INT64_TYPE__)b,store__mode_scale);return tally.total+store__size_large+(__INT64_TYPE__)tally.count;}
extern __INT64_TYPE__ store_total__f64(double a,double b)__asm__("_store::total(double)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT64_TYPE__ store_total__f64(double a,double b){store__Tally tally=store__start();tally=store__step(tally,(__INT64_TYPE__)__extension__({extern double store__twice__f64(double value)__asm__("_store::twice(double)");store__twice__f64(a);}),store__mode_add);tally=store__step(tally,(__INT64_TYPE__)b,store__mode_scale);return tally.total+store__size_large+(__INT64_TYPE__)tally.count;}
extern __INT32_TYPE__ store__twice__i32(__INT32_TYPE__ value)__asm__("_store::twice(__INT32_TYPE__)");
[[gnu::weak]][[gnu::visibility("hidden")]]__INT32_TYPE__ store__twice__i32(__INT32_TYPE__ value){return value+value;}
extern double store__twice__f64(double value)__asm__("_store::twice(double)");
[[gnu::weak]][[gnu::visibility("hidden")]]double store__twice__f64(double value){return value+value;}
