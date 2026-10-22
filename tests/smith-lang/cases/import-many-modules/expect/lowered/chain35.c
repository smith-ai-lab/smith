#include "chain34.h"
#include "chain33.h"

__INT32_TYPE__ chain35_value(void) __asm__("_chain35::value");
__INT32_TYPE__ chain35_value(void) {
return 35;
}
__INT32_TYPE__ chain35_depth(void) __asm__("_chain35::depth");
__INT32_TYPE__ chain35_depth(void) {
return chain34_depth() + 1 + chain33_value() * 0;
}
