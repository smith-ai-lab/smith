#include "geo/a.h"
#include <stdio.h>

__INT32_TYPE__ main() {
auto p = geo_a_make(2, 3.0f);
p[0] += 1;
printf("%d %f %d\n", p[0], (double)p[1], (__INT32_TYPE__)countof(p));
return 0;
}
