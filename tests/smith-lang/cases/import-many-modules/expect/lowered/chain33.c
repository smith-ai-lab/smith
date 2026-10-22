#include "chain32.h"
#include "chain31.h"

__INT32_TYPE__ chain33_value(void) __asm__("_chain33::value");
__INT32_TYPE__ chain33_value(void) {
return 33;
}
__INT32_TYPE__ chain33_depth(void) __asm__("_chain33::depth");
__INT32_TYPE__ chain33_depth(void) {
return chain32_depth() + 1 + chain31_value() * 0;
}
