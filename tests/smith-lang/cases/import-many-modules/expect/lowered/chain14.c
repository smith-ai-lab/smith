#include "chain13.h"
#include "chain12.h"

__INT32_TYPE__ chain14_value(void) __asm__("_chain14::value");
__INT32_TYPE__ chain14_value(void) {
return 14;
}
__INT32_TYPE__ chain14_depth(void) __asm__("_chain14::depth");
__INT32_TYPE__ chain14_depth(void) {
return chain13_depth() + 1 + chain12_value() * 0;
}
