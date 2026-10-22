#include "chain31.h"
#include "chain30.h"

__INT32_TYPE__ chain32_value(void) __asm__("_chain32::value");
__INT32_TYPE__ chain32_value(void) {
return 32;
}
__INT32_TYPE__ chain32_depth(void) __asm__("_chain32::depth");
__INT32_TYPE__ chain32_depth(void) {
return chain31_depth() + 1 + chain30_value() * 0;
}
