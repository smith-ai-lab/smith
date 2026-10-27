#include <stdio.h>

static __SIZE_TYPE__ main_cells(float m[static 3][2]) {
return countof(m) * countof(m[0]);
}
__INT32_TYPE__ main(void) {
float m[3][2] = {};
printf("%zu\n", main_cells(m));
return 0;
}
