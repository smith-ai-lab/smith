#include "chain23.h"
#include "chain22.h"

__INT32_TYPE__ chain24_value(void) __asm__("_chain24::value");
__INT32_TYPE__ chain24_value(void) {
return 24;
}
__INT32_TYPE__ chain24_depth(void) __asm__("_chain24::depth");
__INT32_TYPE__ chain24_depth(void) {
return chain23_depth() + 1 + chain22_value() * 0;
}
