#include <stdio.h>

__INT32_TYPE__ main(void) {
__INT32_TYPE__ (*add)(__INT32_TYPE__, __INT32_TYPE__) =(__INT32_TYPE__ a, __INT32_TYPE__ b) { return a + b; };
__INT32_TYPE__ (*mul)(__INT32_TYPE__, __INT32_TYPE__) =(__INT32_TYPE__ a, __INT32_TYPE__ b) { return a * b; };
double (*half)(double) =(double x) { return x / 2.0; };
printf("%d %d %.1f\n", add(2, 3), mul(2, 3), half(5.0));
return 0;
}
