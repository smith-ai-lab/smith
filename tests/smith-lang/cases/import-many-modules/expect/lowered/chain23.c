#include "chain22.h"
#include "chain21.h"

__INT32_TYPE__ chain23_value(void) __asm__("_chain23::value");
__INT32_TYPE__ chain23_value(void) {
return 23;
}
__INT32_TYPE__ chain23_depth(void) __asm__("_chain23::depth");
__INT32_TYPE__ chain23_depth(void) {
return chain22_depth() + 1 + chain21_value() * 0;
}
