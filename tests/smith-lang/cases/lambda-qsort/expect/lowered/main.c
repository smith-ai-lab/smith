#include <stdio.h>
#include <stdlib.h>

__INT32_TYPE__ main(void) {
__INT32_TYPE__ values[5] = {3, 1, 4, 1, 5};
qsort(values, 5, sizeof(__INT32_TYPE__),(const void *a, const void *b) -> int {
const __INT32_TYPE__ *left = (const __INT32_TYPE__ *)a;
const __INT32_TYPE__ *right = (const __INT32_TYPE__ *)b;
return *left - *right;
});
for (__INT32_TYPE__ i = 0; i < 5; i++) {
printf("%d ", values[i]);
}
printf("\n");
return 0;
}
