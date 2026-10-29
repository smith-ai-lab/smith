#include <stdio.h>

static __INT32_TYPE__ main_fill(__INT32_TYPE__ n) {
__INT32_TYPE__ xs[8] __attribute__((uninitialized));
for (__INT32_TYPE__ i = 0; i < 8; i++)
xs[i] = n + i;
return xs[7];
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ total __attribute__((uninitialized));
total = main_fill(3);
__INT32_TYPE__ *p = nullptr;
__INT32_TYPE__ second __attribute__((uninitialized));
p = &second;
*p = 4;
for (__INT32_TYPE__ k __attribute__((uninitialized)); (k = 5) < 0;)
total += k;
printf("%d %d\n", total, second);
return 0;
}
