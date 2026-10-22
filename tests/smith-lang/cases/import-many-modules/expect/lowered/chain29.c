#include "chain28.h"
#include "chain27.h"

__INT32_TYPE__ chain29_value(void) __asm__("_chain29::value");
__INT32_TYPE__ chain29_value(void) {
return 29;
}
__INT32_TYPE__ chain29_depth(void) __asm__("_chain29::depth");
__INT32_TYPE__ chain29_depth(void) {
return chain28_depth() + 1 + chain27_value() * 0;
}
