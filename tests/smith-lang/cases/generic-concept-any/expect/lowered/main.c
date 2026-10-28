#include <stdio.h>
#include "anything.h"

__INT32_TYPE__ main(void) {
double d = 2.5;
__INT16_TYPE__ s = 3;
__INT32_TYPE__ xs[3] = {7, 8, 9};struct smith_array_i32_n {
__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } ys = xs;
printf("%zu %zu %zu\n",smith_call_failed,smith_call_failed,smith_call_failed);
printf("%d %g\n",smith_call_failed,smith_call_failed);
printf("%zu %zu\n",smith_call_failed,__extension__({extern __SIZE_TYPE__ anything_count__(void)__asm__("_anything::count()");anything_count__();}));
printf("%zu\n",smith_call_failed);
return 0;
}
extern __SIZE_TYPE__ anything_count__(void)__asm__("_anything::count()");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ anything_count__(void){return 0;}
