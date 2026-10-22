#include "chain36.h"
#include "chain35.h"

__INT32_TYPE__ chain37_value(void) __asm__("_chain37::value");
__INT32_TYPE__ chain37_value(void) {
return 37;
}
__INT32_TYPE__ chain37_depth(void) __asm__("_chain37::depth");
__INT32_TYPE__ chain37_depth(void) {
return chain36_depth() + 1 + chain35_value() * 0;
}
