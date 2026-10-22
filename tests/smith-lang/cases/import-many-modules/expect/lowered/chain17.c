#include "chain16.h"
#include "chain15.h"

__INT32_TYPE__ chain17_value(void) __asm__("_chain17::value");
__INT32_TYPE__ chain17_value(void) {
return 17;
}
__INT32_TYPE__ chain17_depth(void) __asm__("_chain17::depth");
__INT32_TYPE__ chain17_depth(void) {
return chain16_depth() + 1 + chain15_value() * 0;
}
