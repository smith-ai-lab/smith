#include "chain33.h"
#include "chain32.h"

__INT32_TYPE__ chain34_value(void) __asm__("_chain34::value");
__INT32_TYPE__ chain34_value(void) {
return 34;
}
__INT32_TYPE__ chain34_depth(void) __asm__("_chain34::depth");
__INT32_TYPE__ chain34_depth(void) {
return chain33_depth() + 1 + chain32_value() * 0;
}
