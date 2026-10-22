#include "chain21.h"
#include "chain20.h"

__INT32_TYPE__ chain22_value(void) __asm__("_chain22::value");
__INT32_TYPE__ chain22_value(void) {
return 22;
}
__INT32_TYPE__ chain22_depth(void) __asm__("_chain22::depth");
__INT32_TYPE__ chain22_depth(void) {
return chain21_depth() + 1 + chain20_value() * 0;
}
