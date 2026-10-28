#include "order.h"
#include <stdio.h>
#include <stdlib.h>

void smith_failure(__UINT8_TYPE__ code);
void smith_failure(__UINT8_TYPE__ code) {
exit(code);
}
int main(void) {
__INT32_TYPE__ ints[3] = {5, 2, 9};
double reals[3] = {2.5, 9.5, 1.5};
__SIZE_TYPE__ a =smith_call_failed;
__SIZE_TYPE__ b =smith_call_failed;
__SIZE_TYPE__ c =smith_call_failed;
__SIZE_TYPE__ d =smith_call_failed;
printf("%zu %zu %zu %zu\n", a, b, c, d);
return 0;
}
