#include "chain15.h"
#include "chain14.h"

__INT32_TYPE__ chain16_value(void) __asm__("_chain16::value");
__INT32_TYPE__ chain16_value(void) {
return 16;
}
__INT32_TYPE__ chain16_depth(void) __asm__("_chain16::depth");
__INT32_TYPE__ chain16_depth(void) {
return chain15_depth() + 1 + chain14_value() * 0;
}
