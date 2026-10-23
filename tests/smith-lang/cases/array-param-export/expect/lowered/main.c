#include "arr.h"
#include <stdio.h>

__INT32_TYPE__ main(__INT32_TYPE__ argc, char **argv) {
__INT32_TYPE__ xs[4] = {7, 8, 9, 10};
float m[3][2] = {};
printf("%d %g\n", arr_pick(xs, 2), (double)arr_corner(m));
return 0;
}
