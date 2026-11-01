#include <stdio.h>

static __INT32_TYPE__ main_apply(__INT32_TYPE__ (*f)(__INT32_TYPE__), __INT32_TYPE__ x) {
return f(x);
}
__INT32_TYPE__ main(void) {
__INT32_TYPE__ doubled = main_apply((__INT32_TYPE__ a) -> I32 { return a * 2; }, 4);
__INT32_TYPE__ (*triple)(__INT32_TYPE__) =(__INT32_TYPE__ a) { return a * 3; };
printf("%d %d\n", doubled, triple(5));
return 0;
}
