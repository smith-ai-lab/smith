#include "chain12.h"
#include "chain11.h"

__INT32_TYPE__ chain13_value(void) __asm__("_chain13::value");
__INT32_TYPE__ chain13_value(void) {
return 13;
}
__INT32_TYPE__ chain13_depth(void) __asm__("_chain13::depth");
__INT32_TYPE__ chain13_depth(void) {
return chain12_depth() + 1 + chain11_value() * 0;
}
