#include "chain30.h"
#include "chain29.h"

__INT32_TYPE__ chain31_value(void) __asm__("_chain31::value");
__INT32_TYPE__ chain31_value(void) {
return 31;
}
__INT32_TYPE__ chain31_depth(void) __asm__("_chain31::depth");
__INT32_TYPE__ chain31_depth(void) {
return chain30_depth() + 1 + chain29_value() * 0;
}
