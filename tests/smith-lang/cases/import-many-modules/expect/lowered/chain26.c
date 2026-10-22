#include "chain25.h"
#include "chain24.h"

__INT32_TYPE__ chain26_value(void) __asm__("_chain26::value");
__INT32_TYPE__ chain26_value(void) {
return 26;
}
__INT32_TYPE__ chain26_depth(void) __asm__("_chain26::depth");
__INT32_TYPE__ chain26_depth(void) {
return chain25_depth() + 1 + chain24_value() * 0;
}
