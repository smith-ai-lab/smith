#include <stdio.h>
#include <stdlib.h>
#include "shapes.h"

static_assert(shapes_dot(1) == 1 + 4 + 9 + 16);
static_assert(shapes_second(3) == 6);
static constexpr __INT32_TYPE__ main_chosen = shapes_second(shapes_dot(1) - 28);
void smith_failure(__UINT8_TYPE__ code);
void smith_failure(__UINT8_TYPE__ code) {
exit(code);
}
int main() {
printf("%d %d\n", main_chosen, shapes_dot(2));
return 0;
}
