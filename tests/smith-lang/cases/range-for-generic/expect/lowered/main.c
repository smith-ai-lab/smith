#include <stdio.h>
#include <stdlib.h>
#include "loops.h"

void smith_failure(__UINT8_TYPE__ code);
void smith_failure(__UINT8_TYPE__ code) {
exit(code);
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ ints[4] = {1, 2, 3, 4};
double reals[3] = {1.5, 2.5, 3.5};struct smith_array_i32_n {
__INT32_TYPE__ * data; __SIZE_TYPE__ count [1]; } ints_view = ints;struct smith_array_f64_n {
double * data; __SIZE_TYPE__ count [1]; } reals_view = reals;
printf("%g %g\n",smith_call_failed,smith_call_failed);
printf("%d %d\n",smith_call_failed,smith_call_failed);smith_call_failed;
printf("%d %d %d %d\n", ints[0], ints[1], ints[2], ints[3]);
return 0;
}
