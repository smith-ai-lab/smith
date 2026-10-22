#include "chain19.h"
#include "chain18.h"

__INT32_TYPE__ chain20_value(void) __asm__("_chain20::value");
__INT32_TYPE__ chain20_value(void) {
return 20;
}
__INT32_TYPE__ chain20_depth(void) __asm__("_chain20::depth");
__INT32_TYPE__ chain20_depth(void) {
return chain19_depth() + 1 + chain18_value() * 0;
}
