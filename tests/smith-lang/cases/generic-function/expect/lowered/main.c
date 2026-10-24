#include "apply.h"
#include <stdio.h>
#include <stdlib.h>

static __INT32_TYPE__ main_inc(__INT32_TYPE__ x) {
return x + 1;
}
static double main_half(double x) {
return x / 2;
}
static __INT32_TYPE__ main_plus(__INT32_TYPE__ a, __INT32_TYPE__ b) {
return a + b;
}
int main() {
__INT32_TYPE__ a =smith_call_failed;
__INT32_TYPE__ b =smith_call_failed;
double c =smith_call_failed;
__INT32_TYPE__ d =smith_call_failed;
__INT32_TYPE__ e =smith_call_failed;
__SIZE_TYPE__ f =smith_call_failed;
double g =smith_call_failed;
printf("%d %d %.2f %d %d %zu %.1f\n", a, b, c, d, e, f, g);
return 0;
}
