#include <stdio.h>

__INT32_TYPE__ main(void) {
__INT32_TYPE__ factor = 3;
__INT32_TYPE__ (*scale)(__INT32_TYPE__) =(__INT32_TYPE__ a) -> I32 { return a * factor; };
printf("%d\n", scale(2));
return 0;
}
