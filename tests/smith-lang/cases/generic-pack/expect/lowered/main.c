#include "args.h"
#include <stdio.h>

static __INT32_TYPE__ main_add1(__INT32_TYPE__ a) {
return a + 1;
}
static __INT32_TYPE__ main_plus(__INT32_TYPE__ a, __INT32_TYPE__ b) {
return a + b;
}
static __INT32_TYPE__ main_add3(__INT32_TYPE__ a, __INT32_TYPE__ b, __INT32_TYPE__ c) {
return a + b + c;
}
int main(void) {
__SIZE_TYPE__ n0 =smith_call_failed;
__SIZE_TYPE__ n1 =smith_call_failed;
__SIZE_TYPE__ n3 =smith_call_failed;
__INT32_TYPE__ r1 =smith_call_failed;
__INT32_TYPE__ r2 =smith_call_failed;
__INT32_TYPE__ r3 =smith_call_failed;
double s0 =smith_call_failed;
double s1 =smith_call_failed;
double s2 =smith_call_failed;
__SIZE_TYPE__ z0 =__extension__({extern __SIZE_TYPE__ args_size_of_first__(void)__asm__("_args::size_of_first()");args_size_of_first__();});
__SIZE_TYPE__ z1 =smith_call_failed;
__SIZE_TYPE__ z2 =smith_call_failed;
__INT32_TYPE__ f1 =smith_call_failed;
__INT32_TYPE__ f2 =smith_call_failed;
__SIZE_TYPE__ c1 =smith_call_failed;
__SIZE_TYPE__ c2 =smith_call_failed;
__SIZE_TYPE__ b =smith_call_failed;
printf("%zu %zu %zu | %d %d %d | %.1f %.1f %.1f | %zu %zu %zu | %d %d | %zu %zu | %zu\n", n0, n1, n3, r1, r2, r3,
s0, s1, s2, z0, z1, z2, f1, f2, c1, c2, b);
return 0;
}
extern __SIZE_TYPE__ args_size_of_first__(void)__asm__("_args::size_of_first()");
[[gnu::weak]][[gnu::visibility("hidden")]]__SIZE_TYPE__ args_size_of_first__(void){{return 0;}}
