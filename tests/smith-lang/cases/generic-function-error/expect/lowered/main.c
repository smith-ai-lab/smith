#include "apply.h"

static __INT32_TYPE__ main_inc(__INT32_TYPE__ x) {
return x + 1;
}
static __INT32_TYPE__ main_plus(__INT32_TYPE__ a, __INT32_TYPE__ b) {
return a + b;
}
int main(void) {
__INT32_TYPE__ (*pointer)(__INT32_TYPE__) = main_inc;
__INT32_TYPE__ number = 3;
__INT32_TYPE__ a =smith_call_failed;
__INT32_TYPE__ b =smith_call_failed;
__INT32_TYPE__ c =smith_call_failed;
__INT32_TYPE__ d =smith_call_failed;
__INT32_TYPE__ e =smith_call_failed;
__INT32_TYPE__ f =smith_call_failed;
__INT32_TYPE__ ok =smith_call_failed;
return a + b + c + d + e + f + ok;
}
