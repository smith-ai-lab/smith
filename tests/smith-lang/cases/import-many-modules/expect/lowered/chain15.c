#include "chain14.h"
#include "chain13.h"

__INT32_TYPE__ chain15_value(void) __asm__("_chain15::value");
__INT32_TYPE__ chain15_value(void) {
return 15;
}
__INT32_TYPE__ chain15_depth(void) __asm__("_chain15::depth");
__INT32_TYPE__ chain15_depth(void) {
return chain14_depth() + 1 + chain13_value() * 0;
}
