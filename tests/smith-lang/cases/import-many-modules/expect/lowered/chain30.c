#include "chain29.h"
#include "chain28.h"

__INT32_TYPE__ chain30_value(void) __asm__("_chain30::value");
__INT32_TYPE__ chain30_value(void) {
return 30;
}
__INT32_TYPE__ chain30_depth(void) __asm__("_chain30::depth");
__INT32_TYPE__ chain30_depth(void) {
return chain29_depth() + 1 + chain28_value() * 0;
}
