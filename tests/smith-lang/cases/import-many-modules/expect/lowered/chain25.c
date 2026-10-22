#include "chain24.h"
#include "chain23.h"

__INT32_TYPE__ chain25_value(void) __asm__("_chain25::value");
__INT32_TYPE__ chain25_value(void) {
return 25;
}
__INT32_TYPE__ chain25_depth(void) __asm__("_chain25::depth");
__INT32_TYPE__ chain25_depth(void) {
return chain24_depth() + 1 + chain23_value() * 0;
}
