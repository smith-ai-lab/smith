#include "chain26.h"
#include "chain25.h"

__INT32_TYPE__ chain27_value(void) __asm__("_chain27::value");
__INT32_TYPE__ chain27_value(void) {
return 27;
}
__INT32_TYPE__ chain27_depth(void) __asm__("_chain27::depth");
__INT32_TYPE__ chain27_depth(void) {
return chain26_depth() + 1 + chain25_value() * 0;
}
