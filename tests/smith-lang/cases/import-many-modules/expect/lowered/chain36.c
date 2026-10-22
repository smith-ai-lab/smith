#include "chain35.h"
#include "chain34.h"

__INT32_TYPE__ chain36_value(void) __asm__("_chain36::value");
__INT32_TYPE__ chain36_value(void) {
return 36;
}
__INT32_TYPE__ chain36_depth(void) __asm__("_chain36::depth");
__INT32_TYPE__ chain36_depth(void) {
return chain35_depth() + 1 + chain34_value() * 0;
}
