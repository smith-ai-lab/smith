#include "chain27.h"
#include "chain26.h"

__INT32_TYPE__ chain28_value(void) __asm__("_chain28::value");
__INT32_TYPE__ chain28_value(void) {
return 28;
}
__INT32_TYPE__ chain28_depth(void) __asm__("_chain28::depth");
__INT32_TYPE__ chain28_depth(void) {
return chain27_depth() + 1 + chain26_value() * 0;
}
