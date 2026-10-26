#include <stdio.h>
#include "a.h"

static __INT32_TYPE__ main_pick(__INT32_TYPE__ x) {
__INT32_TYPE__ y = x;
if (y == 2) return 1;
while (y > 5) y -= 1;
for (;;) break;
return a_pass(y);
}
__INT32_TYPE__ main(void) {
printf("%d %d\n", main_pick(2), main_pick(9));
return 0;
}
