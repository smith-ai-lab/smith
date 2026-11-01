#include <stdio.h>

__INT32_TYPE__ main(void) {
__INT32_TYPE__ (*outer)(__INT32_TYPE__) =(__INT32_TYPE__ a) -> I32 {
__INT32_TYPE__ (*inner)(__INT32_TYPE__) =(__INT32_TYPE__ b) { return b + 1; };
return inner(a) * 2;
};
printf("%d\n", outer(4));
return 0;
}
