#include "chain17.h"
#include "chain16.h"

__INT32_TYPE__ chain18_value(void) __asm__("_chain18::value");
__INT32_TYPE__ chain18_value(void) {
return 18;
}
__INT32_TYPE__ chain18_depth(void) __asm__("_chain18::depth");
__INT32_TYPE__ chain18_depth(void) {
return chain17_depth() + 1 + chain16_value() * 0;
}
